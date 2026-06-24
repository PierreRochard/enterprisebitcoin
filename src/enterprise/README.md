Enterprise SQL Export
==

This build can export block, mempool, and periodic UTXO statistics to
PostgreSQL when `WITH_ENTERPRISE_SQL` is enabled.

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

The exporter reads connection settings from `.env` in the working directory:

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
