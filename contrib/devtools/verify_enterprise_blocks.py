#!/usr/bin/env python3
# Copyright (c) 2026-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Verify enterprise blocks table rows against bitcoind RPC data."""

from __future__ import annotations

import argparse
import base64
import csv
from dataclasses import dataclass, field
import http.client
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
from typing import Any


CHAIN_TO_ENTERPRISE_NETWORK = {
    "main": "mainnet",
    "test": "testnet",
    "testnet4": "testnet4",
    "signet": "signet",
    "regtest": "regtest",
}

DEFAULT_RPC_PORTS = {
    "main": 8332,
    "test": 18332,
    "testnet4": 48332,
    "signet": 38332,
    "regtest": 18443,
}

GETBLOCKSTATS_FIELDS = [
    "height",
    "blockhash",
    "txs",
    "ins",
    "outs",
    "total_out",
    "totalfee",
    "subsidy",
]


@dataclass
class Mismatch:
    height: int | None
    field: str
    sql_value: Any
    rpc_value: Any

    def format(self) -> str:
        return "\t".join(map(str, [self.height, self.field, self.sql_value, self.rpc_value]))


@dataclass
class VerificationSummary:
    name: str
    rows_compared: int = 0
    fields_checked: int = 0
    mismatches: list[Mismatch] = field(default_factory=list)
    details: dict[str, Any] = field(default_factory=dict)

    @property
    def ok(self) -> bool:
        return not self.mismatches


class RpcClient:
    def __init__(
        self,
        *,
        host: str,
        port: int,
        cookie_file: Path | None = None,
        rpc_user: str | None = None,
        rpc_password: str | None = None,
        timeout: int = 120,
    ) -> None:
        self.host = host
        self.port = port
        self.cookie_file = cookie_file
        self.rpc_user = rpc_user
        self.rpc_password = rpc_password
        self.timeout = timeout

    def _auth_header(self) -> str:
        if self.rpc_user is not None and self.rpc_password is not None:
            auth = f"{self.rpc_user}:{self.rpc_password}"
        elif self.cookie_file is not None:
            auth = self.cookie_file.read_text(encoding="utf8").strip()
        else:
            raise ValueError("RPC authentication requires a cookie file or rpc user/password")
        return "Basic " + base64.b64encode(auth.encode()).decode()

    def call(self, method: str, params: list[Any] | None = None) -> Any:
        return self.batch([(method, params or [])])[0]

    def batch(self, calls: list[tuple[str, list[Any]]]) -> list[Any]:
        payload = json.dumps([
            {"jsonrpc": "1.0", "id": index, "method": method, "params": params}
            for index, (method, params) in enumerate(calls)
        ])
        conn = http.client.HTTPConnection(self.host, self.port, timeout=self.timeout)
        conn.request(
            "POST",
            "/",
            body=payload,
            headers={
                "Authorization": self._auth_header(),
                "Content-Type": "application/json",
            },
        )
        response = conn.getresponse()
        body = response.read()
        if response.status != 200:
            raise RuntimeError(f"RPC HTTP {response.status}: {body[:500]!r}")
        replies = json.loads(body)
        replies.sort(key=lambda item: item["id"])
        results = []
        for reply in replies:
            if reply.get("error"):
                raise RuntimeError(reply["error"])
            results.append(reply["result"])
        return results


class PsqlClient:
    def __init__(self, *, psql: str, pg_env: dict[str, str]) -> None:
        self.psql = psql
        self.pg_env = pg_env

    def query(self, sql: str) -> str:
        env = os.environ.copy()
        env.update(self.pg_env)
        return subprocess.check_output(
            [self.psql, "-X", "-v", "ON_ERROR_STOP=1", "-At", "-c", sql],
            env=env,
            text=True,
        )

    def query_rows(self, sql: str) -> list[list[str]]:
        output = self.query(f"COPY ({sql}) TO STDOUT WITH (FORMAT csv, DELIMITER E'\\t')")
        return list(csv.reader(output.splitlines(), delimiter="\t"))


def read_dotenv(path: Path) -> dict[str, str]:
    values = {}
    for raw_line in path.read_text(encoding="utf8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        values[key] = value
    return values


def pg_env_from_config(path: Path | None) -> dict[str, str]:
    values = read_dotenv(path) if path is not None else os.environ
    mapping = {
        "PGUSER": "PGUSER",
        "PGPASSWORD": "PGPASSWORD",
        "PGDB": "PGDATABASE",
        "PGDATABASE": "PGDATABASE",
        "PGHOST": "PGHOST",
        "PGPORT": "PGPORT",
    }
    pg_env = {}
    for source, target in mapping.items():
        if values.get(source):
            pg_env[target] = values[source]
    missing = [name for name in ["PGUSER", "PGPASSWORD", "PGDATABASE", "PGHOST", "PGPORT"] if name not in pg_env]
    if missing:
        raise ValueError(f"missing Postgres settings: {', '.join(missing)}")
    return pg_env


def find_psql(explicit: str | None) -> str:
    if explicit:
        return explicit
    found = shutil.which("psql")
    if found:
        return found
    homebrew = "/usr/local/opt/postgresql@16/bin/psql"
    if Path(homebrew).exists():
        return homebrew
    return "psql"


def read_rpc_port(datadir: Path, chain: str | None) -> int | None:
    candidates = [datadir / "bitcoin.conf"]
    if chain:
        candidates.append(datadir / chain / "bitcoin.conf")
    for path in candidates:
        if not path.exists():
            continue
        for raw_line in path.read_text(encoding="utf8").splitlines():
            line = raw_line.strip()
            if line.startswith("rpcport="):
                return int(line.split("=", 1)[1])
    return None


def find_cookie_file(datadir: Path | None, chain: str | None, explicit: Path | None) -> Path | None:
    if explicit is not None:
        return explicit
    if datadir is None:
        return None
    candidates = [datadir / ".cookie"]
    if chain:
        candidates.append(datadir / chain / ".cookie")
    candidates.extend(datadir / name / ".cookie" for name in DEFAULT_RPC_PORTS)
    for path in candidates:
        if path.exists():
            return path
    return None


def sql_literal(value: str) -> str:
    return "'" + value.replace("'", "''") + "'"


def parse_header_rows(raw_rows: list[list[str]]) -> list[dict[str, Any]]:
    parsed = []
    for record in raw_rows:
        parsed.append({
            "height": int(record[0]),
            "hash": record[1],
            "hash_prev_block": record[2],
            "merkle_root": record[3],
            "time": int(record[4]),
            "median_time": int(record[5]),
            "transactions_count": int(record[6]),
            "version": int(record[7]),
            "bits": int(record[8]),
            "nonce": int(record[9]),
            "difficulty": float(record[10]),
            "chain_work": record[11],
        })
    return parsed


def parse_body_rows(raw_rows: list[list[str]]) -> list[dict[str, Any]]:
    parsed = []
    for record in raw_rows:
        parsed.append({
            "height": int(record[0]),
            "hash": record[1],
            "transactions_count": int(record[2]),
            "inputs_count": int(record[3]),
            "outputs_count": int(record[4]),
            "total_input_value": int(record[5]),
            "total_output_value": int(record[6]),
            "total_fees": int(record[7]),
            "total_size": int(record[8]),
            "total_vsize": int(record[9]),
            "total_weight": int(record[10]),
            "subsidy": int(record[11]),
            "coinbase": int(record[12]),
        })
    return parsed


def fetch_header_rows(psql: PsqlClient, *, network: str, min_height: int | None, max_height: int | None) -> list[dict[str, Any]]:
    predicates = [f"network = {sql_literal(network)}"]
    if min_height is not None:
        predicates.append(f"height >= {min_height}")
    if max_height is not None:
        predicates.append(f"height <= {max_height}")
    sql = f"""
        SELECT height, hash, hash_prev_block, merkle_root,
               EXTRACT(EPOCH FROM time)::bigint,
               EXTRACT(EPOCH FROM median_time)::bigint,
               transactions_count, version, bits, nonce, difficulty, chain_work
        FROM blocks
        WHERE {" AND ".join(predicates)}
        ORDER BY height
    """
    return parse_header_rows(psql.query_rows(sql))


def fetch_body_rows(psql: PsqlClient, *, network: str, min_height: int, max_height: int) -> list[dict[str, Any]]:
    sql = f"""
        SELECT height, hash, transactions_count, inputs_count, outputs_count,
               total_input_value, total_output_value, total_fees,
               total_size, total_vsize, total_weight, subsidy, coinbase
        FROM blocks
        WHERE network = {sql_literal(network)}
          AND height >= {min_height}
          AND height <= {max_height}
        ORDER BY height
    """
    return parse_body_rows(psql.query_rows(sql))


def compare_header_rows(
    *,
    rpc: RpcClient,
    rows: list[dict[str, Any]],
    active_height: int,
    batch_size: int,
) -> VerificationSummary:
    summary = VerificationSummary(name="headers")
    if not rows:
        return summary
    compare_max = min(rows[-1]["height"], active_height)
    by_height = {row["height"]: row for row in rows if row["height"] <= compare_max}
    if rows[0]["height"] == 0:
        expected_rows = compare_max + 1
        if len(by_height) != expected_rows:
            summary.mismatches.append(Mismatch(None, "coverage_count", len(by_height), expected_rows))

    for base in range(rows[0]["height"], compare_max + 1, batch_size):
        heights = list(range(base, min(compare_max + 1, base + batch_size)))
        hashes = rpc.batch([("getblockhash", [height]) for height in heights])
        headers = rpc.batch([("getblockheader", [block_hash]) for block_hash in hashes])
        for height, header in zip(heights, headers):
            row = by_height.get(height)
            if row is None:
                summary.mismatches.append(Mismatch(height, "missing_sql_row", None, header["hash"]))
                continue
            expected_prev = header.get("previousblockhash", "0" * 64)
            comparisons = {
                "height": (row["height"], int(header["height"])),
                "hash": (row["hash"], header["hash"]),
                "hash_prev_block": (row["hash_prev_block"], expected_prev),
                "merkle_root": (row["merkle_root"], header["merkleroot"]),
                "time": (row["time"], int(header["time"])),
                "median_time": (row["median_time"], int(header["mediantime"])),
                "transactions_count": (row["transactions_count"], int(header["nTx"])),
                "version": (row["version"], int(header["version"])),
                "bits": (row["bits"], int(header["bits"], 16)),
                "nonce": (row["nonce"], int(header["nonce"])),
                "chain_work": (row["chain_work"], header["chainwork"]),
            }
            for field_name, (sql_value, rpc_value) in comparisons.items():
                summary.fields_checked += 1
                if sql_value != rpc_value:
                    summary.mismatches.append(Mismatch(height, field_name, sql_value, rpc_value))
            summary.fields_checked += 1
            if not math.isclose(row["difficulty"], float(header["difficulty"]), rel_tol=1e-12, abs_tol=1e-9):
                summary.mismatches.append(Mismatch(height, "difficulty", row["difficulty"], header["difficulty"]))
            summary.rows_compared += 1

    summary.details.update({
        "min_height": rows[0]["height"],
        "max_height": rows[-1]["height"],
        "compare_max": compare_max,
    })
    return summary


def compare_body_rows(
    *,
    rpc: RpcClient,
    rows: list[dict[str, Any]],
    batch_size: int,
    getblock_samples: int,
) -> VerificationSummary:
    summary = VerificationSummary(name="body")
    if not rows:
        return summary
    for base in range(0, len(rows), batch_size):
        batch = rows[base:base + batch_size]
        stats_batch = rpc.batch([("getblockstats", [row["height"], GETBLOCKSTATS_FIELDS]) for row in batch])
        for row, stats in zip(batch, stats_batch):
            comparisons = {
                "height": (row["height"], int(stats["height"])),
                "hash": (row["hash"], stats["blockhash"]),
                "transactions_count": (row["transactions_count"], int(stats["txs"])),
                "inputs_count": (row["inputs_count"], int(stats["ins"]) + 1),
                "outputs_count": (row["outputs_count"], int(stats["outs"])),
                "total_input_value": (row["total_input_value"], int(stats["total_out"]) + int(stats["totalfee"])),
                "total_output_value_minus_coinbase": (row["total_output_value"] - row["coinbase"], int(stats["total_out"])),
                "total_fees": (row["total_fees"], int(stats["totalfee"])),
                "subsidy": (row["subsidy"], int(stats["subsidy"])),
            }
            for field_name, (sql_value, rpc_value) in comparisons.items():
                summary.fields_checked += 1
                if sql_value != rpc_value:
                    summary.mismatches.append(Mismatch(row["height"], field_name, sql_value, rpc_value))
            summary.rows_compared += 1

    if getblock_samples:
        for mismatch in compare_getblock_samples(rpc=rpc, rows=rows, sample_count=getblock_samples):
            summary.mismatches.append(mismatch)
            summary.fields_checked += 1
        summary.details["getblock_samples"] = min(getblock_samples, len(rows))

    summary.details.update({
        "min_height": rows[0]["height"],
        "max_height": rows[-1]["height"],
    })
    return summary


def sample_heights(min_height: int, max_height: int, sample_count: int) -> list[int]:
    if sample_count <= 0:
        return []
    total = max_height - min_height + 1
    if sample_count >= total:
        return list(range(min_height, max_height + 1))
    return sorted({min_height + (total - 1) * index // (sample_count - 1) for index in range(sample_count)})


def compare_getblock_samples(*, rpc: RpcClient, rows: list[dict[str, Any]], sample_count: int) -> list[Mismatch]:
    by_height = {row["height"]: row for row in rows}
    heights = sample_heights(rows[0]["height"], rows[-1]["height"], sample_count)
    hashes = rpc.batch([("getblockhash", [height]) for height in heights])
    blocks = rpc.batch([("getblock", [block_hash, 1]) for block_hash in hashes])
    mismatches = []
    for height, block in zip(heights, blocks):
        row = by_height[height]
        comparisons = {
            "getblock_hash": (row["hash"], block["hash"]),
            "getblock_transactions_count": (row["transactions_count"], int(block["nTx"])),
            "getblock_txid_list_length": (row["transactions_count"], len(block["tx"])),
            "total_size": (row["total_size"], int(block["size"])),
            "total_weight": (row["total_weight"], int(block["weight"])),
            "total_vsize": (row["total_vsize"], int(block["weight"]) // 4),
        }
        for field_name, (sql_value, rpc_value) in comparisons.items():
            if sql_value != rpc_value:
                mismatches.append(Mismatch(height, field_name, sql_value, rpc_value))
    return mismatches


def check_sql_consistency(psql: PsqlClient, *, network: str) -> VerificationSummary:
    summary = VerificationSummary(name="sql_consistency")
    network_sql = sql_literal(network)
    rows = psql.query_rows(f"""
        SELECT 'tx_json_mismatches', count(*)
        FROM blocks
        WHERE network = {network_sql}
          AND transactions_count <> jsonb_array_length(transaction_data)
        UNION ALL
        SELECT 'zero_tx_rows', count(*)
        FROM blocks
        WHERE network = {network_sql}
          AND transactions_count = 0
    """)
    for name, count_raw in rows:
        count = int(count_raw)
        summary.fields_checked += 1
        summary.details[name] = count
        if count:
            summary.mismatches.append(Mismatch(None, name, count, 0))
    return summary


def verify(
    *,
    rpc: RpcClient,
    psql: PsqlClient,
    network: str | None,
    min_height: int | None,
    max_height: int | None,
    batch_size: int,
    check_body_tail: bool,
    getblock_samples: int,
    check_consistency: bool,
) -> list[VerificationSummary]:
    chain_info = rpc.call("getblockchaininfo")
    resolved_network = network or CHAIN_TO_ENTERPRISE_NETWORK[chain_info["chain"]]
    active_height = int(chain_info["blocks"])
    summaries = []
    rows = fetch_header_rows(psql, network=resolved_network, min_height=min_height, max_height=max_height)
    headers = compare_header_rows(rpc=rpc, rows=rows, active_height=active_height, batch_size=batch_size)
    headers.details.update({
        "network": resolved_network,
        "node_height": active_height,
        "headers": int(chain_info["headers"]),
        "pruned": bool(chain_info.get("pruned")),
        "pruneheight": chain_info.get("pruneheight"),
    })
    summaries.append(headers)

    if check_body_tail and rows:
        body_min = min_height if min_height is not None else rows[0]["height"]
        if chain_info.get("pruned"):
            body_min = max(body_min, int(chain_info["pruneheight"]))
        body_max = min(rows[-1]["height"], active_height)
        if max_height is not None:
            body_max = min(body_max, max_height)
        if body_min <= body_max:
            body_rows = fetch_body_rows(psql, network=resolved_network, min_height=body_min, max_height=body_max)
            summaries.append(compare_body_rows(
                rpc=rpc,
                rows=body_rows,
                batch_size=batch_size,
                getblock_samples=getblock_samples,
            ))

    if check_consistency:
        summaries.append(check_sql_consistency(psql, network=resolved_network))
    return summaries


def print_summary(summaries: list[VerificationSummary], max_mismatches: int) -> None:
    for summary in summaries:
        detail = " ".join(f"{key}={value}" for key, value in summary.details.items())
        print(
            f"{summary.name}: rows_compared={summary.rows_compared} "
            f"fields_checked={summary.fields_checked} mismatches={len(summary.mismatches)} {detail}".rstrip()
        )
        if summary.mismatches:
            print(f"{summary.name}: first_mismatches")
            for mismatch in summary.mismatches[:max_mismatches]:
                print(mismatch.format())


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--datadir", type=Path, help="Bitcoin datadir containing the RPC cookie.")
    parser.add_argument("--chain", choices=sorted(DEFAULT_RPC_PORTS), help="Chain name used for cookie lookup and default RPC port.")
    parser.add_argument("--rpcconnect", default="127.0.0.1", help="RPC host.")
    parser.add_argument("--rpcport", type=int, help="RPC port. Defaults to bitcoin.conf or the chain default.")
    parser.add_argument("--rpccookiefile", type=Path, help="RPC cookie file.")
    parser.add_argument("--rpcuser", help="RPC username.")
    parser.add_argument("--rpcpassword", help="RPC password.")
    parser.add_argument("--enterprise-config", type=Path, help="Dotenv file with PGDB, PGUSER, PGPASSWORD, PGHOST, and PGPORT.")
    parser.add_argument("--psql", help="Path to psql.")
    parser.add_argument("--network", choices=sorted(set(CHAIN_TO_ENTERPRISE_NETWORK.values())), help="Enterprise SQL network value.")
    parser.add_argument("--min-height", type=int)
    parser.add_argument("--max-height", type=int)
    parser.add_argument("--batch-size", type=int, default=512)
    parser.add_argument("--check-body-tail", action="store_true", help="Compare unpruned body totals using getblockstats.")
    parser.add_argument("--getblock-samples", type=int, default=0, help="Sample full getblock size/weight checks from the body range.")
    parser.add_argument("--skip-sql-consistency", action="store_true", help="Skip transaction_data length consistency checks.")
    parser.add_argument("--max-mismatches", type=int, default=20)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    start = time.time()
    rpc_port = args.rpcport
    if rpc_port is None and args.datadir is not None:
        rpc_port = read_rpc_port(args.datadir, args.chain)
    if rpc_port is None and args.chain is not None:
        rpc_port = DEFAULT_RPC_PORTS[args.chain]
    if rpc_port is None:
        raise SystemExit("--rpcport or --chain is required when rpcport cannot be read from bitcoin.conf")

    rpc = RpcClient(
        host=args.rpcconnect,
        port=rpc_port,
        cookie_file=find_cookie_file(args.datadir, args.chain, args.rpccookiefile),
        rpc_user=args.rpcuser,
        rpc_password=args.rpcpassword,
    )
    psql = PsqlClient(psql=find_psql(args.psql), pg_env=pg_env_from_config(args.enterprise_config))
    summaries = verify(
        rpc=rpc,
        psql=psql,
        network=args.network,
        min_height=args.min_height,
        max_height=args.max_height,
        batch_size=args.batch_size,
        check_body_tail=args.check_body_tail,
        getblock_samples=args.getblock_samples,
        check_consistency=not args.skip_sql_consistency,
    )
    print_summary(summaries, args.max_mismatches)
    print(f"elapsed_seconds={time.time() - start:.1f}")
    return 0 if all(summary.ok for summary in summaries) else 1


if __name__ == "__main__":
    sys.exit(main())
