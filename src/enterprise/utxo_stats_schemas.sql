CREATE TABLE utxo_snapshots (
    network TEXT NOT NULL,
    block_hash TEXT NOT NULL,
    block_height BIGINT NOT NULL,
    median_time TIMESTAMP,
    exported_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    PRIMARY KEY (network, block_hash),
    UNIQUE (network, block_height)
);

CREATE TABLE utxo_age (
    id SERIAL PRIMARY KEY,
    network TEXT NOT NULL,
    block_hash TEXT NOT NULL,
    block_height BIGINT,
    median_time TIMESTAMP,
    weeks_old BIGINT,
    utxo_count BIGINT,
    utxo_value BIGINT,
    utxo_size BIGINT,
    utxo_count_percent FLOAT,
    utxo_value_percent FLOAT,
    utxo_size_percent FLOAT,
    CONSTRAINT utxo_age_snapshot_fk
        FOREIGN KEY (network, block_hash)
        REFERENCES utxo_snapshots (network, block_hash)
        ON DELETE CASCADE
);
CREATE UNIQUE INDEX utxo_age_network_hash_weeks_old_idx ON utxo_age (network, block_hash, weeks_old);
CREATE INDEX utxo_age_block_height_idx ON utxo_age (block_height);

CREATE TABLE utxo_balances (
    id SERIAL PRIMARY KEY,
    network TEXT NOT NULL,
    block_hash TEXT NOT NULL,
    block_height BIGINT,
    median_time TIMESTAMP,
    lower_bound BIGINT,
    upper_bound BIGINT,
    utxo_count BIGINT,
    utxo_value BIGINT,
    utxo_size BIGINT,
    utxo_count_percent FLOAT,
    utxo_value_percent FLOAT,
    utxo_size_percent FLOAT,
    CONSTRAINT utxo_balances_snapshot_fk
        FOREIGN KEY (network, block_hash)
        REFERENCES utxo_snapshots (network, block_hash)
        ON DELETE CASCADE
);
CREATE UNIQUE INDEX utxo_balances_network_hash_bounds_idx ON utxo_balances (network, block_hash, lower_bound, upper_bound);
CREATE INDEX utxo_balances_block_height_idx ON utxo_balances (block_height);

CREATE TABLE utxo_balances_usd (
    id SERIAL PRIMARY KEY,
    network TEXT NOT NULL,
    block_hash TEXT NOT NULL,
    block_height BIGINT,
    median_time TIMESTAMP,
    lower_bound BIGINT,
    upper_bound BIGINT,
    utxo_count BIGINT,
    utxo_value BIGINT,
    utxo_size BIGINT,
    utxo_count_percent FLOAT,
    utxo_value_percent FLOAT,
    utxo_size_percent FLOAT,
    CONSTRAINT utxo_balances_usd_snapshot_fk
        FOREIGN KEY (network, block_hash)
        REFERENCES utxo_snapshots (network, block_hash)
        ON DELETE CASCADE
);
CREATE UNIQUE INDEX utxo_balances_usd_network_hash_bounds_idx ON utxo_balances_usd (network, block_hash, lower_bound, upper_bound);
CREATE INDEX utxo_balances_usd_block_height_idx ON utxo_balances_usd (block_height);

CREATE TABLE utxo_balances_percentiles (
    id SERIAL PRIMARY KEY,
    network TEXT NOT NULL,
    block_hash TEXT NOT NULL,
    block_height BIGINT,
    median_time TIMESTAMP,
    percentile BIGINT,
    utxo_value FLOAT,
    CONSTRAINT utxo_balances_percentiles_snapshot_fk
        FOREIGN KEY (network, block_hash)
        REFERENCES utxo_snapshots (network, block_hash)
        ON DELETE CASCADE
);
CREATE UNIQUE INDEX utxo_balances_percentiles_network_hash_percentile_idx ON utxo_balances_percentiles (network, block_hash, percentile);
CREATE INDEX utxo_balances_percentiles_block_height_idx ON utxo_balances_percentiles (block_height);

CREATE TABLE utxo_balances_usd_percentiles (
    id SERIAL PRIMARY KEY,
    network TEXT NOT NULL,
    block_hash TEXT NOT NULL,
    block_height BIGINT,
    median_time TIMESTAMP,
    percentile BIGINT,
    utxo_value FLOAT,
    CONSTRAINT utxo_balances_usd_percentiles_snapshot_fk
        FOREIGN KEY (network, block_hash)
        REFERENCES utxo_snapshots (network, block_hash)
        ON DELETE CASCADE
);
CREATE UNIQUE INDEX utxo_balances_usd_percentiles_network_hash_percentile_idx ON utxo_balances_usd_percentiles (network, block_hash, percentile);
CREATE INDEX utxo_balances_usd_percentiles_block_height_idx ON utxo_balances_usd_percentiles (block_height);

CREATE TABLE address_balance_buckets (
    id SERIAL PRIMARY KEY,
    network TEXT NOT NULL,
    block_hash TEXT NOT NULL,
    block_height BIGINT,
    median_time TIMESTAMP,
    lower_bound BIGINT,
    upper_bound BIGINT,
    address_count BIGINT,
    CONSTRAINT address_balance_buckets_snapshot_fk
        FOREIGN KEY (network, block_hash)
        REFERENCES utxo_snapshots (network, block_hash)
        ON DELETE CASCADE
);
CREATE UNIQUE INDEX address_balance_buckets_network_hash_bounds_idx ON address_balance_buckets (network, block_hash, lower_bound, upper_bound);
CREATE INDEX address_balance_buckets_block_height_idx ON address_balance_buckets (block_height);

CREATE TABLE address_balance_buckets_usd (
    id SERIAL PRIMARY KEY,
    network TEXT NOT NULL,
    block_hash TEXT NOT NULL,
    block_height BIGINT,
    median_time TIMESTAMP,
    lower_bound_cents BIGINT,
    upper_bound_cents BIGINT,
    address_count BIGINT,
    CONSTRAINT address_balance_buckets_usd_snapshot_fk
        FOREIGN KEY (network, block_hash)
        REFERENCES utxo_snapshots (network, block_hash)
        ON DELETE CASCADE
);
CREATE UNIQUE INDEX address_balance_buckets_usd_network_hash_bounds_idx ON address_balance_buckets_usd (network, block_hash, lower_bound_cents, upper_bound_cents);
CREATE INDEX address_balance_buckets_usd_block_height_idx ON address_balance_buckets_usd (block_height);

CREATE TABLE utxo_script_types (
    id SERIAL PRIMARY KEY,
    network TEXT NOT NULL,
    block_hash TEXT NOT NULL,
    block_height BIGINT,
    median_time TIMESTAMP,
    script_type TEXT,
    utxo_count BIGINT,
    utxo_value BIGINT,
    utxo_size BIGINT,
    utxo_count_percent FLOAT,
    utxo_value_percent FLOAT,
    utxo_size_percent FLOAT,
    CONSTRAINT utxo_script_types_snapshot_fk
        FOREIGN KEY (network, block_hash)
        REFERENCES utxo_snapshots (network, block_hash)
        ON DELETE CASCADE
);
CREATE UNIQUE INDEX utxo_script_types_network_hash_type_idx ON utxo_script_types (network, block_hash, script_type);
CREATE INDEX utxo_script_types_block_height_idx ON utxo_script_types (block_height);
