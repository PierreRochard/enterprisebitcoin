#!/usr/bin/env python3
# Copyright (c) 2026-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Verify the change-flow block columns and their backfill end to end.

Mines a three-input self-transfer that pools 150 BTC back to the same script,
then a same-day forward that passes 149 BTC of that coin to a new script. Every
block row's self_change_* and forward_* columns must match values recomputed
from getblock verbosity 3. The columns are then cleared and must be backfilled
to the same values.

Requires BITCOIN_ENTERPRISE_TEST_CONFIG to point at a dotenv file for a
throwaway PostgreSQL 17 or 18 database whose name starts with
enterprisebitcoin_test_. The test drops and recreates the enterprise tables in
it; never point it at production.
"""

import os
from decimal import Decimal
from pathlib import Path
import subprocess
import sys

from test_framework.test_framework import BitcoinTestFramework, SkipTest
from test_framework.script_util import key_to_p2wpkh_script
from test_framework.util import assert_equal, assert_greater_than
from test_framework.wallet import MiniWallet
from test_framework.wallet_util import generate_keypair

REPO_ROOT = next(
    parent
    for parent in Path(__file__).resolve().parents
    if (parent / "contrib" / "devtools" / "verify_enterprise_blocks.py").is_file()
)
sys.path.insert(0, str(REPO_ROOT / "contrib" / "devtools"))

from verify_enterprise_blocks import (  # noqa: E402
    find_psql,
    pg_env_from_config,
    pg_subprocess_env,
    PsqlClient,
    read_dotenv,
)

SCHEMA = REPO_ROOT / "src" / "enterprise" / "schema.sql"
COLUMNS = ["self_change_outputs_count", "self_change_value_sats", "forward_outputs_count", "forward_value_sats"]
FORWARD_MIN_VALUE = 100 * 100_000_000
FORWARD_MAX_AGE_BLOCKS = 144


def sats(value):
    return int(Decimal(str(value)) * 100_000_000)


def expected_change_flows(block):
    """The four columns for a verbosity-3 block, from the definitions in change_flows.h."""
    self_count = self_value = forward_count = forward_value = 0
    for tx in block["tx"][1:]:
        spent = [(v["prevout"]["scriptPubKey"]["hex"], sats(v["prevout"]["value"]), v["prevout"]["height"])
                 for v in tx["vin"]]
        scripts = {script for script, _, _ in spent}
        outs = [(o["scriptPubKey"]["hex"], sats(o["value"])) for o in tx["vout"]]
        for script, value in outs:
            if script in scripts:
                self_count += 1
                self_value += value
        if len(spent) == 1 and len(outs) == 2:
            script, value, height = spent[0]
            kept_script, kept = outs[0] if outs[0][1] >= outs[1][1] else outs[1]
            if (value >= FORWARD_MIN_VALUE and block["height"] - height < FORWARD_MAX_AGE_BLOCKS
                    and kept * 1000 >= value * 990 and kept_script != script):
                forward_count += 1
                forward_value += kept
    return [str(n) for n in (self_count, self_value, forward_count, forward_value)]


class EnterpriseChangeFlowsTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.supports_cli = False
        self.enterprise_config = os.environ.get("BITCOIN_ENTERPRISE_TEST_CONFIG")
        self.extra_args = [[
            "-enterpriseindex=1",
            f"-enterpriseconfig={self.enterprise_config or 'missing-enterprise-test-config'}",
        ]]

    def setup_network(self):
        if not self.enterprise_config:
            raise SkipTest("BITCOIN_ENTERPRISE_TEST_CONFIG is not set")
        config = Path(self.enterprise_config)
        if not config.exists():
            raise SkipTest(f"enterprise test config does not exist: {config}")
        values = read_dotenv(config)
        if not values.get("PGDB", "").casefold().startswith("enterprisebitcoin_test_"):
            raise SkipTest("destructive enterprise test database must use the enterprisebitcoin_test_ prefix")
        host = values.get("PGHOST", "")
        if not (host.startswith("/") or host.casefold() in {"127.0.0.1", "::1", "localhost"}):
            raise SkipTest("enterprise test requires a loopback PostgreSQL host or a unix socket directory")
        try:
            self.psql = PsqlClient(psql=find_psql(os.environ.get("PSQL")), pg_env=pg_env_from_config(config),
                                   read_only=False)
            version = int(self.sql("SHOW server_version_num"))
        except (OSError, subprocess.CalledProcessError, ValueError) as err:
            raise SkipTest(f"PostgreSQL test dependencies are unavailable: {err}") from err
        if not 170000 <= version < 190000:
            raise SkipTest(f"PostgreSQL 17 or 18 test server required, found {version}")

        prices_kind = self.sql("""
            SELECT COALESCE((
                SELECT c.relkind::text FROM pg_class c JOIN pg_namespace n ON n.oid = c.relnamespace
                WHERE n.nspname = 'public' AND c.relname = 'prices'
            ), '')
        """)
        if prices_kind:
            self.sql(f"DROP {dict(m='MATERIALIZED VIEW', v='VIEW').get(prices_kind, 'TABLE')} public.prices CASCADE")
        self.sql("""
            DROP TABLE IF EXISTS public.enterprise_schema_migrations CASCADE;
            DROP TABLE IF EXISTS public.enterprise_block_ingest CASCADE;
            DROP TABLE IF EXISTS public.enterprise_block_gaps CASCADE;
        """)
        subprocess.run(
            [self.psql.psql, "-X", "-v", "ON_ERROR_STOP=1", "-f", str(SCHEMA)],
            check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
            env=pg_subprocess_env(self.psql.pg_env, read_only=False),
        )
        super().setup_network()

    def sql(self, query):
        return self.psql.query(query).strip()

    def rows(self):
        return self.psql.query_rows(f"""
            SELECT height, {", ".join(COLUMNS)}
            FROM blocks
            WHERE network = 'regtest'
            ORDER BY height
        """)

    def expected_rows(self):
        node = self.nodes[0]
        return [[str(height)] + expected_change_flows(node.getblock(node.getblockhash(height), 3))
                for height in range(node.getblockcount() + 1)]

    def wait_for_rows_through_tip(self):
        tip = self.nodes[0].getblockcount()
        self.wait_until(lambda: self.sql(f"""
            SELECT count(*) FROM blocks WHERE network = 'regtest' AND height <= {tip}
        """) == str(tip + 1), timeout=120)

    def run_test(self):
        node = self.nodes[0]
        wallet = MiniWallet(node)
        self.generate(wallet, 110)

        self.log.info("Pool three coinbase coins back to the wallet's own script: self-change")
        pooled = wallet.send_self_transfer_multi(
            from_node=node, utxos_to_spend=[wallet.get_utxo() for _ in range(3)], num_outputs=1)
        self.generate(node, 1)
        pooled_value = pooled["new_utxos"][0]["value"]
        assert_greater_than(sats(pooled_value), 149 * 100_000_000)

        self.log.info("Forward 149 BTC of the fresh coin to a new script: a same-day forward")
        _, pubkey = generate_keypair()
        forward = wallet.send_to(from_node=node, scriptPubKey=key_to_p2wpkh_script(pubkey),
                                 amount=149 * 100_000_000)
        self.generate(node, 1)
        forward_height = node.getblockcount()
        assert_equal(node.decoderawtransaction(forward["hex"])["vin"][0]["txid"], pooled["txid"])

        self.wait_for_rows_through_tip()
        expected = self.expected_rows()
        assert_equal(self.rows(), expected)
        forward_row = expected[forward_height]
        assert_equal(forward_row[3:], ["1", str(149 * 100_000_000)])
        assert_equal(forward_row[1], "1")  # the change back to the wallet's script
        assert_equal(expected[forward_height - 1][1:], ["1", str(sats(pooled_value)), "0", "0"])

        self.log.info("Clear the change-flow columns and verify the writer backfills them")
        self.sql(f"""
            UPDATE blocks SET {", ".join(f"{column} = NULL" for column in COLUMNS)}
            WHERE network = 'regtest';
        """)
        self.restart_node(0)
        self.wait_until(lambda: self.sql("""
            SELECT count(*) FROM blocks WHERE network = 'regtest' AND self_change_value_sats IS NULL
        """) == "0", timeout=120)
        assert_equal(self.rows(), expected)


if __name__ == "__main__":
    EnterpriseChangeFlowsTest(__file__).main()
