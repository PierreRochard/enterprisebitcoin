Enterprise SQL Export
==

This build can export block, mempool, and periodic UTXO statistics to
PostgreSQL when `WITH_ENTERPRISE_SQL` is enabled.

When `-enterpriseindex=1` is enabled and `-prune` is not set explicitly, the
node defaults to `-prune=20000`, keeping block and undo files to a 20 GB target.
Set `-prune=<MiB>` explicitly to choose a different target.

The enterprise index also bounds its durable block spool. By default
`-enterprisespoolmax=1024` pauses validation when the spool exceeds 1 GiB, then
lets the Postgres writer drain before accepting more blocks. This keeps the
spool from recreating the disk pressure that pruning is meant to avoid.

Gap recovery is local-only. If a missing block row can still be read from the
local pruned block/undo store, the enterprise index queues that block through
the same durable spool. If the data has already been pruned, the gap is marked
unavailable; recovery is to rewind or run full `-reindex` so validation replays
the enterprise index.

Dependencies
==

macOS:

```shell
brew install cmake ninja pkg-config libpq libpqxx postgresql
```

Linux:

```shell
sudo apt-get update
sudo apt-get install build-essential cmake ninja-build pkg-config libpq-dev libpqxx-dev zlib1g-dev
```

Windows:

Install Visual Studio 2026 with the Desktop development with C++ workload,
CMake, and vcpkg. Install PostgreSQL when a local server or the `psql` client is
needed. The vcpkg manifest supplies libpqxx, libpq, zlib, and their runtime
dependencies.

Database
==

As a PostgreSQL administrator, create a dedicated login that owns its database:

```postgresql
CREATE ROLE enterprisebitcoin LOGIN PASSWORD 'PASSWORD';
CREATE DATABASE enterprisebitcoin OWNER enterprisebitcoin;
```

Connect as `enterprisebitcoin` to load the block schema. Load the UTXO schema
only when periodic UTXO exports are needed:

```shell
psql -X -v ON_ERROR_STOP=1 -U enterprisebitcoin -d enterprisebitcoin -f src/enterprise/schema.sql
psql -X -v ON_ERROR_STOP=1 -U enterprisebitcoin -d enterprisebitcoin -f src/enterprise/utxo_stats_schemas.sql
```

`schema.sql` drops and recreates the `blocks` and `mempool_entries` tables. Run
it only as a fresh-database bootstrap or when an intentional reset is
acceptable. It records the current enterprise schema marker and keeps `prices`
as an ordinary table. To preserve and reuse an existing PostgreSQL 17 database,
run `src/enterprise/migrations/20260711_pg17_reuse_v1.sql` through the guarded
Windows rollout documented in `doc/enterprise-pg17-windows.md`; never load
`schema.sql` into that reused database.

The exporter reads connection settings from a dotenv-style file. By default it
keeps backwards compatibility with `.env` in the working directory, then lets
`<datadir>/<chain>/enterprise.env` override it. Use `-enterpriseconfig=<file>`
to read one explicit file instead; relative paths are resolved under the
network datadir. Environment variables override file values.

The denomination classifier uses the block header time for BTC/USD lookup. Daily
`prices.day` rows match the header time's UTC date; timestamp rows use the
nearest row on that UTC date. Missing dates remain unpriced rather than silently
carrying stale price data forward.
`prices.price_low`, `prices.price_high`, and `prices.price_source` are optional;
when low/high are absent the exporter uses a +/-2.5% window around `price` and
records the source as `prices.price:fallback_2_5pct`. If no usable row exists,
eligible non-coinbase outputs are exported as unknown rather than failing the
block export. A zero source price is preserved as zero but is not a valid
classification window, so its eligible outputs remain unknown.

```shell
PGDB=enterprisebitcoin
PGUSER=enterprisebitcoin
PGPASSWORD=PASSWORD
PGHOST=127.0.0.1
PGPORT=5432
```

Build
==

```shell
cmake -S . -B build -GNinja \
  -DBUILD_GUI=OFF \
  -DENABLE_WALLET=OFF \
  -DWITH_ENTERPRISE_SQL=ON
cmake --build build --target bitcoind
```

On Windows, launch a Visual Studio developer PowerShell, set `VCPKG_ROOT`, and
use the checked-in Visual Studio preset:

```powershell
$env:VCPKG_ROOT = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\vcpkg'
cmake -B build-windows-enterprise --preset vs2026 `
  -DVCPKG_MANIFEST_NO_DEFAULT_FEATURES=ON `
  '-DVCPKG_MANIFEST_FEATURES=tests;enterprise-sql' `
  -DBUILD_GUI=OFF `
  -DENABLE_WALLET=OFF `
  -DWITH_ZMQ=OFF `
  -DWITH_ENTERPRISE_SQL=ON
cmake --build build-windows-enterprise --config Release `
  --target bitcoind bitcoin-cli test_bitcoin --parallel
ctest --test-dir build-windows-enterprise -C Release --output-on-failure --parallel
cmake --install build-windows-enterprise --config Release `
  --prefix build-windows-enterprise/stage
```

vcpkg stages the required PostgreSQL, OpenSSL, and zlib DLLs beside the Release
executables in `build-windows-enterprise/bin/Release` and in the install prefix.
The dynamic `vs2026` preset also requires the matching Microsoft Visual C++ x64
Redistributable on the target host. Use `vs2026-static` when distributing to a
host where that runtime is not already installed.
