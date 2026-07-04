#!/usr/bin/env python3
# Copyright (c) 2026-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Verify enterprise block SQL rows against RPC in an end-to-end node test.

This test requires BITCOIN_ENTERPRISE_TEST_CONFIG to point at a dotenv file for
a throwaway PostgreSQL database. The test drops and recreates enterprise tables
in that database.
"""

import os
from pathlib import Path
import subprocess
import sys

from test_framework.test_framework import BitcoinTestFramework, SkipTest
from test_framework.util import assert_equal, assert_greater_than, rpc_port
from test_framework.wallet import MiniWallet

REPO_ROOT = Path(__file__).resolve().parents[2]
DEVTOOLS = REPO_ROOT / "contrib" / "devtools"
sys.path.insert(0, str(DEVTOOLS))

from verify_enterprise_blocks import (  # noqa: E402
    find_psql,
    pg_env_from_config,
    PsqlClient,
    read_dotenv,
)


class EnterpriseBlocksVerifyTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.supports_cli = False
        self.enterprise_config = os.environ.get("BITCOIN_ENTERPRISE_TEST_CONFIG")
        config_arg = self.enterprise_config or "missing-enterprise-test-config"
        self.extra_args = [[
            "-enterpriseindex=1",
            f"-enterpriseconfig={config_arg}",
            "-enterprisespoolmax=16",
        ]]

    def skip_test_if_missing_module(self):
        if not self.enterprise_config:
            raise SkipTest("BITCOIN_ENTERPRISE_TEST_CONFIG is not set")
        self.enterprise_config_path = Path(self.enterprise_config)
        if not self.enterprise_config_path.exists():
            raise SkipTest(f"enterprise test config does not exist: {self.enterprise_config_path}")

        self.psql_path = find_psql(os.environ.get("PSQL"))
        try:
            dotenv_values = read_dotenv(self.enterprise_config_path)
            for key in ["PGUSER", "PGPASSWORD", "PGDB", "PGHOST", "PGPORT"]:
                if key not in dotenv_values:
                    raise ValueError(f"missing {key}")
                os.environ[key] = dotenv_values[key]
            pg_env = pg_env_from_config(self.enterprise_config_path)
            subprocess.run(
                [self.psql_path, "--version"],
                check=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )
        except (OSError, subprocess.CalledProcessError, ValueError) as err:
            raise SkipTest(f"PostgreSQL test dependencies are unavailable: {err}") from err

        self.psql = PsqlClient(psql=self.psql_path, pg_env=pg_env)
        self.reset_enterprise_schema()
        self.psql.query("DROP INDEX IF EXISTS blocks_network_height_idx")

    def reset_enterprise_schema(self):
        schema = REPO_ROOT / "src" / "enterprise" / "schema.sql"
        env = os.environ.copy()
        env.update(self.psql.pg_env)
        subprocess.run(
            [self.psql_path, "-X", "-v", "ON_ERROR_STOP=1", "-f", str(schema)],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            env=env,
        )

    def sql_scalar(self, sql):
        return self.psql.query(sql).strip()

    def wait_for_enterprise_height(self, height):
        expected_count = height + 1

        def synced():
            row = self.sql_scalar(f"""
                SELECT concat(
                    COALESCE(max(height), -1), ',',
                    count(DISTINCT height)
                )
                FROM blocks
                WHERE network = 'regtest'
                  AND height >= 0
                  AND height <= {height}
            """)
            max_height, count = [int(value) for value in row.split(",")]
            return max_height >= height and count == expected_count

        self.wait_until(synced, timeout=120)

    def wait_for_spool_empty(self):
        spool_dir = self.nodes[0].chain_path / "enterprise" / "block_spool"
        self.wait_until(lambda: len(list(spool_dir.glob("*.ebd"))) == 0, timeout=120)

    def assert_enterprise_column_exists(self, table, column):
        assert_equal(self.sql_scalar(f"""
            SELECT count(*)
            FROM information_schema.columns
            WHERE table_name = '{table}'
              AND column_name = '{column}'
        """), "1")

    def run_verifier(self, *, max_height, expect_success):
        command = [
            sys.executable,
            str(DEVTOOLS / "verify_enterprise_blocks.py"),
            "--datadir",
            str(self.nodes[0].datadir_path),
            "--chain",
            "regtest",
            "--rpcport",
            str(rpc_port(self.nodes[0].index)),
            "--enterprise-config",
            str(self.enterprise_config_path),
            "--network",
            "regtest",
            "--max-height",
            str(max_height),
            "--batch-size",
            "32",
            "--check-body-tail",
            "--getblock-samples",
            "8",
        ]
        result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        self.log.debug("verify_enterprise_blocks.py stdout:\n%s", result.stdout)
        if result.stderr:
            self.log.debug("verify_enterprise_blocks.py stderr:\n%s", result.stderr)
        if expect_success:
            assert_equal(result.returncode, 0)
        else:
            assert result.returncode != 0
        return result

    def run_test(self):
        node = self.nodes[0]
        wallet = MiniWallet(node)

        self.log.info("Mine a block containing a non-coinbase transaction")
        tx = wallet.create_self_transfer()
        wallet.sendrawtransaction(from_node=node, tx_hex=tx["hex"])
        block_hash = self.generate(node, 1)[0]
        tip_height = node.getblockcount()
        assert_greater_than(node.getblock(block_hash)["nTx"], 1)

        self.log.info("Wait for enterprise SQL writer to populate through the tip")
        self.wait_for_enterprise_height(tip_height)

        self.log.info("Verify the runtime path recreated the block height index")
        assert_equal(self.sql_scalar("""
            SELECT count(*)
            FROM pg_indexes
            WHERE tablename = 'blocks'
              AND indexname = 'blocks_network_height_idx'
        """), "1")

        self.log.info("Verify denomination classifier columns and missing-price fallback behavior")
        for column in [
            "btc_usd_price",
            "btc_usd_price_low",
            "btc_usd_price_high",
            "btc_usd_price_source",
            "denomination_eligible_outputs_count",
            "denomination_eligible_value_sats",
            "usd_denom_outputs_count",
            "usd_denom_value_sats",
            "usd_denom_confidence_sum",
            "sats_denom_outputs_count",
            "sats_denom_value_sats",
            "sats_denom_confidence_sum",
            "unknown_denom_outputs_count",
            "unknown_denom_value_sats",
            "ambiguous_denom_outputs_count",
            "ambiguous_denom_value_sats",
            "likely_change_outputs_count",
            "likely_change_value_sats",
            "denomination_classifier_version",
        ]:
            self.assert_enterprise_column_exists("blocks", column)
        for column in ["price_low", "price_high", "price_source"]:
            self.assert_enterprise_column_exists("prices", column)

        denomination_row = self.psql.query_rows(f"""
            SELECT denomination_classifier_version,
                   denomination_eligible_outputs_count,
                   unknown_denom_outputs_count,
                   usd_denom_outputs_count,
                   sats_denom_outputs_count,
                   ambiguous_denom_outputs_count,
                   likely_change_outputs_count,
                   btc_usd_price IS NULL,
                   btc_usd_price_low IS NULL,
                   btc_usd_price_high IS NULL,
                   btc_usd_price_source IS NULL
            FROM blocks
            WHERE network = 'regtest'
              AND height = {tip_height}
        """)
        assert_equal(len(denomination_row), 1)
        denomination_row = denomination_row[0]
        eligible_outputs = int(denomination_row[1])
        assert_greater_than(eligible_outputs, 0)
        assert_equal(denomination_row[0], "denomination-v1")
        assert_equal(int(denomination_row[2]), eligible_outputs)
        assert_equal(int(denomination_row[3]), 0)
        assert_equal(int(denomination_row[4]), 0)
        assert_equal(int(denomination_row[5]), 0)
        assert_equal(int(denomination_row[6]), 0)
        assert_equal(denomination_row[7], "t")
        assert_equal(denomination_row[8], "t")
        assert_equal(denomination_row[9], "t")
        assert_equal(denomination_row[10], "t")

        self.log.info("Run the reusable RPC-vs-Postgres verifier successfully")
        self.run_verifier(max_height=tip_height, expect_success=True)

        self.log.info("Corrupt transactions_count and verify the tool catches it")
        self.psql.query(f"""
            UPDATE blocks
            SET transactions_count = 0
            WHERE network = 'regtest'
              AND height = {tip_height}
        """)
        failed = self.run_verifier(max_height=tip_height, expect_success=False)
        assert "transactions_count" in failed.stdout

        self.log.info("Repair the test row and verify the tool is clean again")
        self.psql.query(f"""
            UPDATE blocks
            SET transactions_count = jsonb_array_length(transaction_data)
            WHERE network = 'regtest'
              AND height = {tip_height}
        """)
        self.run_verifier(max_height=tip_height, expect_success=True)

        self.log.info("Verify -reindex removes stale enterprise spool files")
        self.wait_for_spool_empty()
        self.stop_node(0)
        spool_dir = node.chain_path / "enterprise" / "block_spool"
        spool_dir.mkdir(parents=True, exist_ok=True)
        stale_spool = spool_dir / (
            "00000000000000000000-0000009999-"
            "0000000000000000000000000000000000000000000000000000000000000000"
            "-connect.ebd"
        )
        stale_spool.write_bytes(b"stale")
        self.start_node(0, extra_args=self.extra_args[0] + ["-reindex"])
        assert not stale_spool.exists()


if __name__ == "__main__":
    EnterpriseBlocksVerifyTest(__file__).main()
