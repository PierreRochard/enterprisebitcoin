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
sudo apt-get install build-essential cmake ninja-build pkg-config libpq-dev libpqxx-dev
```

Database
==

Create a PostgreSQL user and database, then load the schemas:

```postgresql
CREATE USER USER WITH ENCRYPTED PASSWORD 'PASSWORD';
GRANT pg_read_all_data TO USER;
GRANT pg_write_all_data TO USER;
ALTER USER USER CREATEDB;
CREATE DATABASE bitcoin;
```

```shell
psql bitcoin < src/enterprise/schema.sql
psql bitcoin < src/enterprise/utxo_stats_schemas.sql
```

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
PGDB=bitcoin
PGUSER=USER
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
