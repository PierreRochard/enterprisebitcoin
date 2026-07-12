#!/usr/bin/env python3
# Copyright (c) 2026-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Verify additive enterprise migration and block SQL rows end to end.

This test requires BITCOIN_ENTERPRISE_TEST_CONFIG to point at a dotenv file for
a throwaway PostgreSQL database. The test intentionally drops and recreates
enterprise tables in that disposable database; never point it at production.
"""

import os
from pathlib import Path
import random
import re
import subprocess
import sys
import time
from datetime import datetime, timezone
from decimal import Decimal

from test_framework.messages import CTxOut
from test_framework.script import CScript, OP_RETURN
from test_framework.test_node import FailedToStartError
from test_framework.test_framework import BitcoinTestFramework, SkipTest
from test_framework.util import assert_approx, assert_equal, assert_greater_than, rpc_port
from test_framework.wallet import MiniWallet

REPO_ROOT = next(
    parent
    for parent in Path(__file__).resolve().parents
    if (parent / "contrib" / "devtools" / "verify_enterprise_blocks.py").is_file()
)
DEVTOOLS = REPO_ROOT / "contrib" / "devtools"
sys.path.insert(0, str(DEVTOOLS))

from verify_enterprise_blocks import (  # noqa: E402
    find_psql,
    pg_env_from_config,
    pg_subprocess_env,
    PsqlClient,
    read_dotenv,
)

MIGRATION_VERSION = "20260711_pg17_reuse_v1"
MIGRATION = REPO_ROOT / "src" / "enterprise" / "migrations" / f"{MIGRATION_VERSION}.sql"
MIGRATION_VERIFY = REPO_ROOT / "src" / "enterprise" / "migrations" / f"verify_{MIGRATION_VERSION}.sql"

DENOMINATION_COLUMNS = [
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
]


class EnterpriseBlocksVerifyTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.supports_cli = False
        self.enterprise_config = os.environ.get("BITCOIN_ENTERPRISE_TEST_CONFIG")
        config_arg = self.enterprise_config or "missing-enterprise-test-config"
        self.extra_args = [[
            "-enterpriseindex=1",
            f"-enterpriseconfig={config_arg}",
            "-enterprisebackfillheight=1000",
            "-enterprisemempoolexport=1",
            "-enterprisespoolmax=16",
        ]]

    def skip_test_if_missing_module(self):
        if not self.enterprise_config:
            raise SkipTest("BITCOIN_ENTERPRISE_TEST_CONFIG is not set")
        self.enterprise_config_path = Path(self.enterprise_config)
        if not self.enterprise_config_path.exists():
            raise SkipTest(f"enterprise test config does not exist: {self.enterprise_config_path}")
        for required_sql in [MIGRATION, MIGRATION_VERIFY]:
            if not required_sql.is_file():
                raise SkipTest(f"enterprise migration fixture does not exist: {required_sql}")

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

        database_name = dotenv_values["PGDB"]
        if not database_name.casefold().startswith("enterprisebitcoin_test_"):
            raise SkipTest(
                "destructive enterprise test database must use the enterprisebitcoin_test_ prefix"
            )
        if dotenv_values["PGHOST"].casefold() not in {"127.0.0.1", "::1", "localhost"}:
            raise SkipTest("destructive enterprise test requires a loopback PostgreSQL host")
        if dotenv_values["PGPORT"] != "55417":
            raise SkipTest("destructive enterprise outage test requires PostgreSQL port 55417")

        # This write-enabled client exists only for the guarded disposable
        # fixture. The production-facing verifier keeps its read-only default.
        self.psql = PsqlClient(psql=self.psql_path, pg_env=pg_env, read_only=False)
        data_directory = Path(self.sql_scalar("SHOW data_directory")).resolve()
        runtime_root = (REPO_ROOT / ".runtime").resolve()
        if not data_directory.is_relative_to(runtime_root):
            raise SkipTest(
                f"destructive enterprise test requires PostgreSQL data under {runtime_root}, "
                f"found {data_directory}"
            )
        expected_data_directory = (runtime_root / "postgres" / "test-cluster" / "data").resolve()
        if data_directory != expected_data_directory:
            raise SkipTest(
                "destructive enterprise outage test requires the dedicated PostgreSQL data "
                f"directory {expected_data_directory}, found {data_directory}"
            )
        self.pg_data_directory = data_directory
        pg_ctl_name = "pg_ctl.exe" if os.name == "nt" else "pg_ctl"
        self.pg_ctl_path = Path(self.psql_path).resolve().with_name(pg_ctl_name)
        if not self.pg_ctl_path.is_file():
            raise SkipTest(f"PostgreSQL control utility is unavailable: {self.pg_ctl_path}")
        pg_ctl_version = subprocess.run(
            [self.pg_ctl_path, "--version"],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        ).stdout
        if not re.search(r"\b17\.\d+\b", pg_ctl_version):
            raise SkipTest(
                "PostgreSQL 17 pg_ctl is required for the disposable outage test; "
                "set PSQL to the PostgreSQL 17 psql executable"
            )
        self.test_postgres_stopped = False
        server_version_num = int(self.sql_scalar("SHOW server_version_num"))
        if not 170000 <= server_version_num < 180000:
            raise SkipTest(f"PostgreSQL 17 test server required, found {server_version_num}")
        role_is_allowed = self.sql_scalar(
            "SELECT session_user = 'postgres' AND rolsuper FROM pg_roles WHERE rolname = session_user"
        )
        if role_is_allowed != "t":
            raise SkipTest("PostgreSQL test role must be the postgres superuser")
        self.prepare_legacy_schema()
        self.run_sql_file(MIGRATION)
        self.run_sql_file(MIGRATION)
        self.run_sql_file(MIGRATION_VERIFY)

        # Both the node and verifier must treat the explicit config as
        # authoritative even when the parent process has hostile libpq
        # routing/service defaults.
        os.environ["PGHOSTADDR"] = "not-a-valid-address"
        os.environ["PGSERVICE"] = "enterprise_hostile_service_must_not_exist"
        os.environ["PGOPTIONS"] = "-c enterprise_hostile_setting=1"

    def run_sql_file(self, path):
        if not path.is_file():
            raise AssertionError(f"required SQL fixture does not exist: {path}")
        subprocess.run(
            [
                self.psql_path,
                "-X",
                "-v",
                "ON_ERROR_STOP=1",
                "-v",
                f"expected_database={self.psql.pg_env['PGDATABASE']}",
                "-f",
                str(path),
            ],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            env=pg_subprocess_env(self.psql.pg_env, read_only=False),
        )

    def prepare_legacy_schema(self):
        relation_kind = self.sql_scalar("""
            SELECT COALESCE((
                SELECT relkind::text
                FROM pg_class
                WHERE oid = to_regclass('public.prices')
            ), '')
        """)
        if relation_kind == "m":
            self.psql.query("DROP MATERIALIZED VIEW public.prices CASCADE")
        elif relation_kind == "v":
            self.psql.query("DROP VIEW public.prices CASCADE")
        elif relation_kind:
            self.psql.query("DROP TABLE public.prices CASCADE")

        self.psql.query("""
            DROP TABLE IF EXISTS public.enterprise_schema_migrations CASCADE;
            DROP TABLE IF EXISTS public.enterprise_block_ingest CASCADE;
            DROP TABLE IF EXISTS public.enterprise_block_gaps CASCADE;
            DROP TABLE IF EXISTS public.enterprise_test_price_source CASCADE;
        """)
        self.run_sql_file(REPO_ROOT / "src" / "enterprise" / "schema.sql")

        drop_columns = ",\n".join(f"DROP COLUMN {column}" for column in DENOMINATION_COLUMNS)
        self.psql.query(f"""
            ALTER TABLE public.blocks
                ADD COLUMN enterprise_test_sentinel TEXT,
                {drop_columns};
            CREATE VIEW public.enterprise_test_blocks_view AS
                SELECT network, height, hash, enterprise_test_sentinel
                FROM public.blocks;
            DROP INDEX public.blocks_network_height_idx;
            DROP TABLE public.enterprise_block_ingest;
            DROP TABLE public.enterprise_block_gaps;
            DROP TABLE public.mempool_entries;
            DROP TABLE public.prices;

            CREATE TABLE public.enterprise_test_price_source (
                day DATE PRIMARY KEY,
                price NUMERIC NOT NULL
            );
            INSERT INTO public.enterprise_test_price_source(day, price)
            SELECT day::date, 50000::numeric
            FROM generate_series(
                DATE '2000-01-01',
                DATE '2100-01-01',
                INTERVAL '1 day'
            ) AS series(day);
            CREATE MATERIALIZED VIEW public.prices AS
                SELECT day, price FROM public.enterprise_test_price_source;
            CREATE UNIQUE INDEX idx_prices_day ON public.prices(day);
        """)

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

    def spool_files(self):
        spool_dir = self.nodes[0].chain_path / "enterprise" / "block_spool"
        return sorted(spool_dir.glob("*.ebd"))

    def spool_usage(self):
        return sum(path.stat().st_size for path in self.spool_files())

    def enterprise_backend_pids(self):
        value = self.sql_scalar("""
            SELECT COALESCE(string_agg(pid::text, ',' ORDER BY pid), '')
            FROM pg_stat_activity
            WHERE datname = current_database()
              AND application_name = 'enterprise-bitcoind'
              AND backend_type = 'client backend'
        """)
        return [int(pid) for pid in value.split(",") if pid]

    def wait_for_single_enterprise_backend_pid(self):
        writer_pid = None

        def single_backend():
            nonlocal writer_pid
            pids = self.enterprise_backend_pids()
            if len(pids) != 1:
                return False
            writer_pid = pids[0]
            return True

        self.wait_until(single_backend, timeout=30)
        return writer_pid

    def set_test_postgres_running(self, *, running):
        """Control only the dedicated repo-local PostgreSQL test cluster."""
        if running == (not self.test_postgres_stopped):
            return
        action = "start" if running else "stop"
        command = [
            str(self.pg_ctl_path),
            action,
            "-D",
            str(self.pg_data_directory),
            "-w",
            "-t",
            "30",
        ]
        postgres_log = None
        pg_ctl_log = None
        if running:
            # On Windows the spawned postgres process otherwise inherits
            # pg_ctl's captured stdout/stderr pipe, causing subprocess.run()
            # to wait forever after pg_ctl itself has exited successfully.
            postgres_log = self.pg_data_directory.parent / "functional-postgres.log"
            pg_ctl_log = self.pg_data_directory.parent / "functional-pgctl.log"
            command.extend([
                "-l",
                str(postgres_log),
                "-o",
                "-p 55417 -h 127.0.0.1",
            ])
        else:
            command.extend(["-m", "fast"])
        if pg_ctl_log is not None:
            with pg_ctl_log.open("a", encoding="utf8") as log_stream:
                result = subprocess.run(
                    command,
                    stdout=log_stream,
                    stderr=subprocess.STDOUT,
                    text=True,
                    env=pg_subprocess_env(self.psql.pg_env, read_only=False),
                )
            diagnostic = f"see {pg_ctl_log} and {postgres_log}"
        else:
            result = subprocess.run(
                command,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                env=pg_subprocess_env(self.psql.pg_env, read_only=False),
            )
            diagnostic = f"{result.stdout}\n{result.stderr}"
        if result.returncode != 0:
            raise AssertionError(
                f"pg_ctl {action} failed for the disposable port-55417 cluster: {diagnostic}"
            )
        self.test_postgres_stopped = not running
        if running:
            assert 170000 <= int(self.sql_scalar("SHOW server_version_num")) < 180000

    def start_node_with_transient_windows_io_retry(self, *, extra_args):
        """Retry once for Defender/LevelDB's transient post-shutdown .dbtmp handle."""
        node = self.nodes[0]
        time.sleep(1)
        try:
            self.start_node(0, extra_args=extra_args)
            return
        except FailedToStartError:
            debug_log = node.debug_log_path.read_text(encoding="utf8", errors="replace")
            transient_error = ".dbtmp: Unable to remove the file to be replaced"
            if os.name != "nt" or transient_error not in debug_log:
                raise
            node.wait_until_stopped(
                expect_error=True,
                expected_stderr=re.compile(r"Error opening block database", re.DOTALL),
            )
        self.log.warning("Retrying node start once after transient Windows LevelDB handle contention")
        time.sleep(2)
        self.start_node(0, extra_args=extra_args)

    def send_large_incompressible_transaction(self, *, node, wallet, seed):
        """Create a bounded nonstandard regtest transaction that resists spool compression."""
        transfer = wallet.create_self_transfer(fee=Decimal("0.02"))
        payload = random.Random(seed).randbytes(90 * 1024)
        transfer["tx"].vout.append(CTxOut(nValue=0, scriptPubKey=CScript([OP_RETURN, payload])))
        tx_hex = transfer["tx"].serialize().hex()
        assert_greater_than(len(tx_hex) // 2, 80 * 1024)
        # create_self_transfer() already removed the explicitly consumed UTXO
        # from MiniWallet. Use a short-lived bounded RPC and avoid
        # decoderawtransaction()+scan_tx(); expanding random data-carrier
        # script JSON is unnecessary and disproportionately slow on Windows.
        self.log.info("Submit bounded random transaction for spool seed %d", seed)
        rpc = node.create_new_rpc_connection(client_timeout=20)
        txid = rpc.sendrawtransaction(hexstring=tx_hex, maxfeerate=0)
        self.log.info("Accepted bounded random transaction %s", txid)
        return txid

    def assert_enterprise_column_exists(self, table, column):
        assert_equal(self.sql_scalar(f"""
            SELECT count(*)
            FROM information_schema.columns
            WHERE table_name = '{table}'
              AND column_name = '{column}'
        """), "1")

    def run_verifier(self, *, min_height=0, max_height, expect_success, check_consistency=True):
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
            "--min-height",
            str(min_height),
            "--max-height",
            str(max_height),
            "--batch-size",
            "32",
            "--check-body-tail",
            "--getblock-samples",
            "8",
        ]
        if check_consistency:
            command.append("--check-sql-consistency")
        result = subprocess.run(
            command,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            env=os.environ.copy(),
        )
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

        self.log.info("Verify the additive migration is present and preserved the legacy shape")
        assert_equal(self.sql_scalar(f"""
            SELECT count(*)
            FROM enterprise_schema_migrations
            WHERE version = '{MIGRATION_VERSION}'
              AND description = 'Additive PostgreSQL 17 reuse schema for enterprise denomination backfill'
        """), "1")
        assert_equal(self.sql_scalar("""
            SELECT count(*)
            FROM pg_class
            WHERE oid = 'public.prices'::regclass
              AND relkind = 'm'
        """), "1")
        assert_equal(self.sql_scalar("""
            SELECT count(*)
            FROM information_schema.columns
            WHERE table_schema = 'public'
              AND table_name = 'prices'
              AND column_name IN ('price_low', 'price_high', 'price_source')
        """), "0")
        assert_equal(self.sql_scalar("SELECT count(*) FROM prices"), "36526")
        self.assert_enterprise_column_exists("blocks", "enterprise_test_sentinel")
        assert_equal(self.sql_scalar("SELECT to_regclass('public.enterprise_test_blocks_view') IS NOT NULL"), "t")
        for column in DENOMINATION_COLUMNS:
            self.assert_enterprise_column_exists("blocks", column)
        for table in ["enterprise_block_ingest", "enterprise_block_gaps", "mempool_entries"]:
            assert_equal(self.sql_scalar(f"SELECT to_regclass('public.{table}') IS NOT NULL"), "t")
        assert_equal(self.sql_scalar("""
            SELECT count(*)
            FROM pg_index i
            JOIN pg_class c ON c.oid = i.indexrelid
            WHERE c.relname = 'blocks_network_height_idx'
              AND i.indisunique
              AND i.indisvalid
        """), "1")

        self.log.info("Verify enterpriseindex is exposed through getindexinfo")
        initial_tip = node.getblockcount()
        self.wait_until(
            lambda: node.getindexinfo("enterpriseindex").get("enterpriseindex", {}).get("synced") is True,
            timeout=120,
        )
        self.wait_for_enterprise_height(initial_tip)
        assert_equal(node.getindexinfo("enterpriseindex"), {
            "enterpriseindex": {
                "synced": True,
                "best_block_height": initial_tip,
            },
        })

        self.log.info("Mine a block containing a non-coinbase transaction")
        tx = wallet.create_self_transfer()
        txid = wallet.sendrawtransaction(from_node=node, tx_hex=tx["hex"])
        self.wait_until(lambda: self.sql_scalar(f"""
            SELECT count(*)
            FROM mempool_entries
            WHERE txid = '{txid}'
              AND network = 'regtest'
              AND removal_reason IS NULL
        """) == "1", timeout=30)
        block_hash = self.generate(node, 1)[0]
        tip_height = node.getblockcount()
        assert_greater_than(node.getblock(block_hash)["nTx"], 1)
        self.wait_until(lambda: self.sql_scalar(f"""
            SELECT removal_reason
            FROM mempool_entries
            WHERE txid = '{txid}'
              AND network = 'regtest'
        """) == "block", timeout=30)

        self.log.info("Wait for enterprise SQL writer to populate through the tip")
        self.wait_for_enterprise_height(tip_height)

        self.log.info("Verify the materialized-view price compatibility fallback")
        denomination_row = self.psql.query_rows(f"""
            SELECT denomination_classifier_version,
                   denomination_eligible_outputs_count,
                   denomination_eligible_value_sats,
                   unknown_denom_outputs_count,
                   unknown_denom_value_sats,
                   usd_denom_outputs_count,
                   usd_denom_value_sats,
                   sats_denom_outputs_count,
                   sats_denom_value_sats,
                   ambiguous_denom_outputs_count,
                   ambiguous_denom_value_sats,
                   likely_change_outputs_count,
                   likely_change_value_sats,
                   btc_usd_price,
                   btc_usd_price_low,
                   btc_usd_price_high,
                   btc_usd_price_source
            FROM blocks
            WHERE network = 'regtest'
              AND height = {tip_height}
        """)
        assert_equal(len(denomination_row), 1)
        denomination_row = denomination_row[0]
        eligible_outputs = int(denomination_row[1])
        eligible_value = int(denomination_row[2])
        assert_greater_than(eligible_outputs, 0)
        assert_equal(denomination_row[0], "denomination-v2")
        assert_equal(sum(int(denomination_row[index]) for index in [3, 5, 7, 9]), eligible_outputs)
        assert_equal(sum(int(denomination_row[index]) for index in [4, 6, 8, 10]), eligible_value)
        assert int(denomination_row[11]) <= int(denomination_row[3])
        assert int(denomination_row[12]) <= int(denomination_row[4])
        assert_approx(float(denomination_row[13]), 50000.0)
        assert_approx(float(denomination_row[14]), 48750.0)
        assert_approx(float(denomination_row[15]), 51250.0)
        assert_equal(denomination_row[16], "prices.price:fallback_2_5pct")

        self.log.info("Verify an observed zero price persists with unknown classifications")
        self.wait_for_spool_empty()
        zero_time = int(time.time()) + 2
        zero_day = datetime.fromtimestamp(zero_time, tz=timezone.utc).date()
        self.psql.query(f"""
            INSERT INTO enterprise_test_price_source(day, price)
            VALUES (DATE '{zero_day.isoformat()}', 0)
            ON CONFLICT (day) DO UPDATE SET price = EXCLUDED.price;
            REFRESH MATERIALIZED VIEW public.prices;
        """)
        try:
            node.setmocktime(zero_time)
            zero_tx = wallet.create_self_transfer()
            wallet.sendrawtransaction(from_node=node, tx_hex=zero_tx["hex"])
            zero_hash = self.generate(node, 1)[0]
            zero_height = node.getblockcount()
            self.wait_for_enterprise_height(zero_height)
            zero_row = self.psql.query_rows(f"""
                SELECT denomination_classifier_version,
                       btc_usd_price,
                       btc_usd_price_low,
                       btc_usd_price_high,
                       btc_usd_price_source,
                       denomination_eligible_outputs_count,
                       unknown_denom_outputs_count,
                       usd_denom_outputs_count,
                       sats_denom_outputs_count,
                       ambiguous_denom_outputs_count
                FROM blocks
                WHERE network = 'regtest' AND hash = '{zero_hash}'
            """)
            assert_equal(len(zero_row), 1)
            zero_row = zero_row[0]
            assert_equal(zero_row[0], "denomination-v2")
            assert_approx(float(zero_row[1]), 0.0)
            assert_approx(float(zero_row[2]), 0.0)
            assert_approx(float(zero_row[3]), 0.0)
            assert_equal(zero_row[4], "prices.price:fallback_2_5pct")
            assert_greater_than(int(zero_row[5]), 0)
            assert_equal(int(zero_row[6]), int(zero_row[5]))
            assert_equal(sum(int(value) for value in zero_row[7:10]), 0)

            self.log.info("Verify a current-day zero is not retained after prices refresh")
            self.psql.query(f"""
                INSERT INTO enterprise_test_price_source(day, price)
                VALUES (DATE '{zero_day.isoformat()}', 50000)
                ON CONFLICT (day) DO UPDATE SET price = EXCLUDED.price;
                REFRESH MATERIALIZED VIEW public.prices;
            """)
            node.setmocktime(zero_time + 1)
            refreshed_hash = self.generate(node, 1)[0]
            refreshed_height = node.getblockcount()
            self.wait_for_enterprise_height(refreshed_height)
            refreshed_row = self.psql.query_rows(f"""
                SELECT btc_usd_price,
                       btc_usd_price_low,
                       btc_usd_price_high,
                       btc_usd_price_source
                FROM blocks
                WHERE network = 'regtest' AND hash = '{refreshed_hash}'
            """)
            assert_equal(len(refreshed_row), 1)
            refreshed_row = refreshed_row[0]
            assert_approx(float(refreshed_row[0]), 50000.0)
            assert_approx(float(refreshed_row[1]), 48750.0)
            assert_approx(float(refreshed_row[2]), 51250.0)
            assert_equal(refreshed_row[3], "prices.price:fallback_2_5pct")
        finally:
            node.setmocktime(0)
            self.psql.query(f"""
                INSERT INTO enterprise_test_price_source(day, price)
                VALUES (DATE '{zero_day.isoformat()}', 50000)
                ON CONFLICT (day) DO UPDATE SET price = EXCLUDED.price;
                REFRESH MATERIALIZED VIEW public.prices;
            """)
        tip_height = node.getblockcount()

        self.log.info("Run the reusable RPC-vs-Postgres verifier successfully")
        self.run_verifier(max_height=tip_height, expect_success=True)

        self.log.info("Verify SQL consistency scans honor the requested height bounds")
        outside_height = tip_height - 1
        original_transactions_count = self.sql_scalar(f"""
            SELECT transactions_count FROM blocks
            WHERE network = 'regtest' AND height = {outside_height}
        """)
        self.psql.query(f"""
            UPDATE blocks
            SET transactions_count = 0
            WHERE network = 'regtest'
              AND height = {outside_height}
        """)
        self.run_verifier(min_height=tip_height, max_height=tip_height, expect_success=True)
        failed = self.run_verifier(min_height=outside_height, max_height=tip_height, expect_success=False)
        assert "transactions_count" in failed.stdout

        self.log.info("Repair the test row and verify the tool is clean again")
        self.psql.query(f"""
            UPDATE blocks
            SET transactions_count = {original_transactions_count}
            WHERE network = 'regtest'
              AND height = {outside_height}
        """)
        self.run_verifier(max_height=tip_height, expect_success=True)

        self.log.info("Exercise stale, current, wrong-hash, and missing-genesis replay cases")
        legacy_version_height = tip_height - 4
        stale_height = tip_height - 3
        current_height = tip_height - 2
        wrong_hash_height = tip_height - 1
        self.wait_for_spool_empty()
        self.stop_node(0)
        self.psql.query(f"""
            UPDATE blocks
            SET enterprise_test_sentinel = 'v1-preserved',
                denomination_classifier_version = 'denomination-v1',
                btc_usd_price_source = 'v1-row-needs-update'
            WHERE network = 'regtest' AND height = {legacy_version_height};
            UPDATE blocks
            SET enterprise_test_sentinel = 'stale-preserved',
                denomination_classifier_version = NULL,
                btc_usd_price_source = 'stale-row-needs-update'
            WHERE network = 'regtest' AND height = {stale_height};
            UPDATE blocks
            SET enterprise_test_sentinel = 'current-preserved',
                btc_usd_price_source = 'current-row-must-not-change'
            WHERE network = 'regtest' AND height = {current_height};
            UPDATE blocks
            SET enterprise_test_sentinel = 'wrong-hash-must-be-replaced',
                hash = repeat('f', 64)
            WHERE network = 'regtest' AND height = {wrong_hash_height};
            DELETE FROM blocks
            WHERE network = 'regtest' AND height = 0;
        """)

        self.log.info("Verify -reindex fails closed with pending enterprise spool files")
        spool_dir = node.chain_path / "enterprise" / "block_spool"
        spool_dir.mkdir(parents=True, exist_ok=True)
        stale_spool = spool_dir / (
            "00000000000000000000-0000009999-"
            "0000000000000000000000000000000000000000000000000000000000000000"
            "-connect.ebd"
        )
        stale_spool.write_bytes(b"stale")
        node.assert_start_raises_init_error(
            extra_args=self.extra_args[0] + ["-reindex"],
        )
        assert "archive or reconcile the pending block spool before restarting with -reindex" in (
            node.debug_log_path.read_text(encoding="utf8", errors="replace")
        )
        assert stale_spool.exists()
        archived_spool = stale_spool.with_suffix(stale_spool.suffix + ".archived")
        stale_spool.rename(archived_spool)
        assert archived_spool.exists()

        self.start_node(0, extra_args=self.extra_args[0] + ["-reindex"])

        # A previously populated SQL table can satisfy the database wait while
        # the node is still rebuilding its local block index. Wait for the
        # original regtest tip before sampling historical RPC hashes.
        self.wait_until(lambda: node.getblockcount() >= tip_height, timeout=120)
        self.wait_until(
            lambda: node.getindexinfo("enterpriseindex").get("enterpriseindex", {}).get("synced") is True,
            timeout=120,
        )
        self.wait_for_enterprise_height(tip_height)
        self.wait_for_spool_empty()

        assert_equal(self.sql_scalar(f"""
            SELECT concat(enterprise_test_sentinel, ',', denomination_classifier_version, ',', btc_usd_price_source)
            FROM blocks WHERE network = 'regtest' AND height = {legacy_version_height}
        """), "v1-preserved,denomination-v2,prices.price:fallback_2_5pct")
        assert_equal(self.sql_scalar(f"""
            SELECT concat(enterprise_test_sentinel, ',', denomination_classifier_version, ',', btc_usd_price_source)
            FROM blocks WHERE network = 'regtest' AND height = {stale_height}
        """), "stale-preserved,denomination-v2,prices.price:fallback_2_5pct")
        assert_equal(self.sql_scalar(f"""
            SELECT enterprise_test_sentinel
            FROM enterprise_test_blocks_view
            WHERE network = 'regtest' AND height = {stale_height}
        """), "stale-preserved")
        assert_equal(self.sql_scalar(f"""
            SELECT concat(enterprise_test_sentinel, ',', btc_usd_price_source)
            FROM blocks WHERE network = 'regtest' AND height = {current_height}
        """), "current-preserved,current-row-must-not-change")
        assert_equal(self.sql_scalar(f"""
            SELECT concat(hash, ',', enterprise_test_sentinel IS NULL)
            FROM blocks WHERE network = 'regtest' AND height = {wrong_hash_height}
        """), f"{node.getblockhash(wrong_hash_height)},t")
        assert_equal(self.sql_scalar("""
            SELECT hash FROM blocks WHERE network = 'regtest' AND height = 0
        """), node.getblockhash(0))
        assert_equal(self.sql_scalar(f"""
            SELECT count(*)
            FROM enterprise_block_ingest
            WHERE network = 'regtest'
              AND height = {stale_height}
              AND source = 'denomination-backfill'
              AND status = 'succeeded'
        """), "1")

        self.log.info("Exercise a same-height reorg and verify the SQL hash follows the active chain")
        old_tip_hash = self.generate(node, 1)[0]
        reorg_height = node.getblockcount()
        self.wait_until(lambda: self.sql_scalar(f"""
            SELECT count(*) FROM blocks
            WHERE network = 'regtest' AND height = {reorg_height} AND hash = '{old_tip_hash}'
        """) == "1", timeout=120)
        old_tip_time = node.getblockheader(old_tip_hash)["time"]
        node.invalidateblock(old_tip_hash)
        self.wait_until(lambda: self.sql_scalar(f"""
            SELECT count(*) FROM blocks
            WHERE network = 'regtest' AND hash = '{old_tip_hash}'
        """) == "0", timeout=120)
        node.setmocktime(old_tip_time + 1)
        replacement_hash = self.generate(node, 1)[0]
        node.setmocktime(0)
        assert replacement_hash != old_tip_hash
        self.wait_until(lambda: self.sql_scalar(f"""
            SELECT count(*) FROM blocks
            WHERE network = 'regtest' AND height = {reorg_height} AND hash = '{replacement_hash}'
        """) == "1", timeout=120)

        self.psql.query(f"""
            UPDATE blocks
            SET btc_usd_price_source = 'prices.price:fallback_2_5pct'
            WHERE network = 'regtest' AND height = {current_height}
        """)
        self.run_verifier(max_height=reorg_height, expect_success=True)
        assert_equal(node.getindexinfo("enterpriseindex"), {
            "enterpriseindex": {
                "synced": True,
                "best_block_height": reorg_height,
            },
        })

        self.log.info("Verify block, ingest, and gap writes commit atomically")
        self.wait_for_spool_empty()
        atomic_height = node.getblockcount() + 1
        self.psql.query(f"""
            INSERT INTO public.enterprise_block_gaps
                (network, height, expected_hash, status, source, last_error)
            VALUES
                ('regtest', {atomic_height}, repeat('0', 64), 'queued',
                 'atomic-rollback-test', 'must survive the rejected transaction')
            ON CONFLICT (network, height) DO UPDATE SET
                expected_hash = EXCLUDED.expected_hash,
                status = EXCLUDED.status,
                source = EXCLUDED.source,
                last_error = EXCLUDED.last_error;

            DROP TRIGGER IF EXISTS enterprise_test_reject_succeeded_ingest
                ON public.enterprise_block_ingest;
            DROP FUNCTION IF EXISTS public.enterprise_test_reject_succeeded_ingest();
            CREATE FUNCTION public.enterprise_test_reject_succeeded_ingest()
            RETURNS trigger
            LANGUAGE plpgsql
            AS $enterprise_test$
            BEGIN
                IF NEW.network = 'regtest'
                   AND NEW.height = {atomic_height}
                   AND NEW.event_type = 'connect'
                   AND NEW.status = 'succeeded' THEN
                    RAISE EXCEPTION 'intentional atomic ingest failure at height %', NEW.height;
                END IF;
                RETURN NEW;
            END;
            $enterprise_test$;
            CREATE TRIGGER enterprise_test_reject_succeeded_ingest
                BEFORE INSERT OR UPDATE ON public.enterprise_block_ingest
                FOR EACH ROW
                EXECUTE FUNCTION public.enterprise_test_reject_succeeded_ingest();
        """)
        gap_xmin_before = self.sql_scalar(f"""
            SELECT xmin::text
            FROM enterprise_block_gaps
            WHERE network = 'regtest' AND height = {atomic_height}
        """)
        atomic_hash = None
        try:
            atomic_hash = self.generate(node, 1, sync_fun=self.no_op)[0]
            assert_equal(node.getblockcount(), atomic_height)
            self.wait_until(
                lambda: any(atomic_hash in path.name for path in self.spool_files()),
                timeout=30,
            )
            self.wait_until(lambda: self.sql_scalar(f"""
                SELECT COALESCE((
                    SELECT status
                    FROM enterprise_block_ingest
                    WHERE network = 'regtest'
                      AND hash = '{atomic_hash}'
                      AND event_type = 'connect'
                ), '')
            """) == "failed", timeout=60)

            # The block insert and gap resolution precede the deliberately
            # rejected succeeded-ingest write in the same transaction. Both
            # must have rolled back while the durable spool remains available.
            assert_equal(self.sql_scalar(f"""
                SELECT count(*)
                FROM blocks
                WHERE network = 'regtest' AND height = {atomic_height}
            """), "0")
            assert_equal(self.sql_scalar(f"""
                SELECT concat(status, ',', expected_hash, ',', source, ',', xmin::text)
                FROM enterprise_block_gaps
                WHERE network = 'regtest' AND height = {atomic_height}
            """), (
                "queued," + "0" * 64 + ",atomic-rollback-test," + gap_xmin_before
            ))
            assert any(atomic_hash in path.name for path in self.spool_files())
        finally:
            self.psql.query("""
                SET lock_timeout = '5s';
                DROP TRIGGER IF EXISTS enterprise_test_reject_succeeded_ingest
                    ON public.enterprise_block_ingest;
                DROP FUNCTION IF EXISTS public.enterprise_test_reject_succeeded_ingest();
            """)

        # A second notification avoids waiting for the failed writer's retry
        # timer and exercises the retained spool through its normal path.
        self.generate(node, 1, sync_fun=self.no_op)
        atomic_recovery_tip = node.getblockcount()
        self.wait_for_enterprise_height(atomic_recovery_tip)
        self.wait_for_spool_empty()
        atomic_rows = self.psql.query_rows(f"""
            SELECT block_row.xmin::text,
                   ingest_row.xmin::text,
                   gap_row.xmin::text,
                   ingest_row.status,
                   gap_row.status,
                   gap_row.expected_hash,
                   ingest_row.last_error IS NULL,
                   gap_row.last_error IS NULL
            FROM blocks block_row
            JOIN enterprise_block_ingest ingest_row
              ON ingest_row.network = block_row.network
             AND ingest_row.hash = block_row.hash
             AND ingest_row.event_type = 'connect'
            JOIN enterprise_block_gaps gap_row
              ON gap_row.network = block_row.network
             AND gap_row.height = block_row.height
            WHERE block_row.network = 'regtest'
              AND block_row.height = {atomic_height}
              AND block_row.hash = '{atomic_hash}'
        """)
        assert_equal(len(atomic_rows), 1)
        atomic_row = atomic_rows[0]
        assert_equal(atomic_row[0], atomic_row[1])
        assert_equal(atomic_row[0], atomic_row[2])
        assert_equal(atomic_row[3], "succeeded")
        assert_equal(atomic_row[4], "resolved")
        assert_equal(atomic_row[5], atomic_hash)
        assert_equal(atomic_row[6], "t")
        assert_equal(atomic_row[7], "t")

        self.log.info("Verify the writer reuses one backend and reconnects after PostgreSQL restarts")
        writer_pid = self.wait_for_single_enterprise_backend_pid()
        self.generate(node, 2, sync_fun=self.no_op)
        persistent_session_tip = node.getblockcount()
        self.wait_for_enterprise_height(persistent_session_tip)
        self.wait_for_spool_empty()
        assert_equal(self.wait_for_single_enterprise_backend_pid(), writer_pid)

        reconnect_hash = None
        self.set_test_postgres_running(running=False)
        try:
            reconnect_hash = self.generate(node, 1, sync_fun=self.no_op)[0]
            self.wait_until(
                lambda: any(reconnect_hash in path.name for path in self.spool_files()),
                timeout=30,
            )
        finally:
            # The node intentionally remains running while its dedicated
            # PostgreSQL fixture is restored.
            self.set_test_postgres_running(running=True)
        reconnect_height = node.getblockcount()
        self.wait_for_enterprise_height(reconnect_height)
        self.wait_for_spool_empty()
        assert_equal(self.sql_scalar(f"""
            SELECT count(*) FROM blocks
            WHERE network = 'regtest'
              AND height = {reconnect_height}
              AND hash = '{reconnect_hash}'
        """), "1")
        reconnected_writer_pid = self.wait_for_single_enterprise_backend_pid()
        assert reconnected_writer_pid != writer_pid

        self.log.info("Exercise PostgreSQL outage, durable retry, and spool backpressure")
        self.wait_for_spool_empty()
        self.stop_node(0)
        outage_args = self.extra_args[0] + [
            "-enterprisemempoolexport=0",
            "-enterprisespoolmax=1",
        ]
        self.start_node_with_transient_windows_io_retry(extra_args=outage_args)
        self.wait_until(
            lambda: node.getindexinfo("enterpriseindex").get("enterpriseindex", {}).get("synced") is True,
            timeout=120,
        )
        pre_outage_height = node.getblockcount()

        outage_hashes = []
        self.set_test_postgres_running(running=False)
        try:
            for offset in range(1, 21):
                self.send_large_incompressible_transaction(
                    node=node,
                    wallet=wallet,
                    seed=pre_outage_height + offset,
                )
                # Backpressure intentionally blocks the enterprise validation
                # callback on the threshold-crossing block. Do not ask the
                # generic mining helper to drain that queue before PostgreSQL
                # is restored.
                block_hash = self.generate(node, 1, sync_fun=self.no_op)[0]
                outage_hashes.append(block_hash)
                self.wait_until(
                    lambda block_hash=block_hash: any(
                        block_hash in path.name for path in self.spool_files()
                    ),
                    timeout=30,
                )
                if self.spool_usage() > 1024 * 1024:
                    break
            assert self.spool_usage() > 1024 * 1024
            outage_tip = node.getblockcount()

            self.wait_until(
                lambda: "waiting for postgres writer to drain below 1 MiB" in node.debug_log_path.read_text(
                    encoding="utf8", errors="replace"
                ),
                timeout=60,
            )
            enterprise_height = node.getindexinfo("enterpriseindex")["enterpriseindex"]["best_block_height"]
            assert enterprise_height < outage_tip
            assert len(self.spool_files()) >= 2
            for block_hash in outage_hashes:
                assert any(block_hash in path.name for path in self.spool_files())

            # Stop while PostgreSQL is unavailable. The pending .ebd files
            # must survive both node and database restarts.
            self.stop_node(0)
            assert self.spool_usage() > 1024 * 1024
        finally:
            # A failed assertion must never leave the dedicated test cluster
            # stopped for another developer or test run.
            self.set_test_postgres_running(running=True)

        first_outage_height = pre_outage_height + 1
        first_outage_hash = outage_hashes[0]
        assert_equal(self.sql_scalar(f"""
            SELECT count(*) FROM blocks
            WHERE network = 'regtest' AND height = {first_outage_height}
        """), "0")

        self.log.info("Simulate an interrupted commit and reconcile its ingest and gap records")
        self.psql.query(f"""
            CREATE TEMP TABLE enterprise_test_covered_spool AS
            SELECT * FROM public.blocks
            WHERE network = 'regtest' AND height = {pre_outage_height};
            UPDATE enterprise_test_covered_spool
            SET hash = '{first_outage_hash}',
                height = {first_outage_height},
                enterprise_test_sentinel = 'covered-spool-reconcile',
                denomination_classifier_version = 'denomination-v2';
            INSERT INTO public.blocks
            SELECT * FROM enterprise_test_covered_spool;

            INSERT INTO public.enterprise_block_ingest
                (network, hash, height, event_type, status, source, spool_path,
                 attempts, last_error)
            VALUES
                ('regtest', '{first_outage_hash}', {first_outage_height}, 'connect',
                 'started', 'denomination-backfill', 'simulated-interrupted-spool',
                 7, 'simulated interruption after block commit')
            ON CONFLICT (network, hash, event_type) DO UPDATE SET
                height = EXCLUDED.height,
                status = EXCLUDED.status,
                source = EXCLUDED.source,
                spool_path = EXCLUDED.spool_path,
                attempts = EXCLUDED.attempts,
                last_error = EXCLUDED.last_error,
                completed_at = NULL;

            INSERT INTO public.enterprise_block_gaps
                (network, height, expected_hash, status, source, last_error)
            VALUES
                ('regtest', {first_outage_height}, repeat('0', 64), 'queued',
                 'simulated-outage', 'simulated incomplete gap')
            ON CONFLICT (network, height) DO UPDATE SET
                expected_hash = EXCLUDED.expected_hash,
                status = EXCLUDED.status,
                source = EXCLUDED.source,
                last_error = EXCLUDED.last_error;
        """)

        self.start_node_with_transient_windows_io_retry(extra_args=outage_args)
        self.wait_for_enterprise_height(outage_tip)
        self.wait_for_spool_empty()
        assert_equal(self.sql_scalar(f"""
            SELECT concat(status, ',', source, ',', attempts, ',',
                          completed_at IS NOT NULL, ',', last_error IS NULL)
            FROM enterprise_block_ingest
            WHERE network = 'regtest'
              AND hash = '{first_outage_hash}'
              AND event_type = 'connect'
        """), "succeeded,denomination-backfill,7,t,t")
        assert_equal(self.sql_scalar(f"""
            SELECT concat(status, ',', expected_hash, ',', source, ',', last_error IS NULL)
            FROM enterprise_block_gaps
            WHERE network = 'regtest' AND height = {first_outage_height}
        """), f"resolved,{first_outage_hash},denomination-backfill,t")
        assert_equal(self.sql_scalar(f"""
            SELECT enterprise_test_sentinel
            FROM blocks
            WHERE network = 'regtest' AND height = {first_outage_height}
        """), "covered-spool-reconcile")

        self.log.info("Remove the synthetic covered row and verify local gap replay restores it")
        self.stop_node(0)
        self.psql.query(f"""
            DELETE FROM blocks
            WHERE network = 'regtest' AND height = {first_outage_height};
        """)
        self.start_node_with_transient_windows_io_retry(extra_args=outage_args)
        self.wait_until(lambda: self.sql_scalar(f"""
            SELECT count(*)
            FROM blocks
            WHERE network = 'regtest'
              AND height = {first_outage_height}
              AND hash = '{first_outage_hash}'
              AND enterprise_test_sentinel IS NULL
              AND denomination_classifier_version = 'denomination-v2'
        """) == "1", timeout=120)
        self.wait_for_enterprise_height(outage_tip)
        self.wait_for_spool_empty()
        assert_equal(self.sql_scalar(f"""
            SELECT concat(status, ',', expected_hash, ',', source, ',', last_error IS NULL)
            FROM enterprise_block_gaps
            WHERE network = 'regtest' AND height = {first_outage_height}
        """), f"resolved,{first_outage_hash},denomination-backfill,t")
        self.run_verifier(max_height=outage_tip, expect_success=True)
        assert_equal(node.getindexinfo("enterpriseindex"), {
            "enterpriseindex": {
                "synced": True,
                "best_block_height": outage_tip,
            },
        })


if __name__ == "__main__":
    EnterpriseBlocksVerifyTest(__file__).main()
