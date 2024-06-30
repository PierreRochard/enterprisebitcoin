

CREATE TABLE utxo_age (
    id SERIAL PRIMARY KEY,
    block_height BIGINT,
    median_time TIMESTAMP WITH TIME ZONE,
    weeks_old BIGINT,
    utxo_count BIGINT,
    utxo_value BIGINT,
    utxo_size BIGINT,
    utxo_count_percent DOUBLE PRECISION,
    utxo_value_percent DOUBLE PRECISION,
    utxo_size_percent DOUBLE PRECISION
);

CREATE UNIQUE INDEX utxo_age_block_height_weeks_old_idx ON utxo_age (block_height, weeks_old);

CREATE TABLE utxo_balances (
    id SERIAL PRIMARY KEY,
    block_height BIGINT,
    median_time TIMESTAMP WITH TIME ZONE,
    balance_min BIGINT,
    balance_max BIGINT,
    utxo_count BIGINT,
    utxo_value BIGINT,
    utxo_size BIGINT,
    utxo_count_percent DOUBLE PRECISION,
    utxo_value_percent DOUBLE PRECISION,
    utxo_size_percent DOUBLE PRECISION
);

CREATE UNIQUE INDEX utxo_balances_block_height_balance_min_balance_max_idx ON utxo_balances (block_height, balance_min, balance_max);

CREATE TABLE utxo_addresses (
    id SERIAL PRIMARY KEY,
    block_height BIGINT,
    median_time TIMESTAMP WITH TIME ZONE,
    address TEXT,
    utxo_count BIGINT,
    utxo_value BIGINT,
    utxo_size BIGINT,
    utxo_count_percent DOUBLE PRECISION,
    utxo_value_percent DOUBLE PRECISION,
    utxo_size_percent DOUBLE PRECISION
);

CREATE UNIQUE INDEX utxo_addresses_block_height_address_idx ON utxo_addresses (block_height, address);

CREATE TABLE utxo_script_types (
    id SERIAL PRIMARY KEY,
    block_height BIGINT,
    median_time TIMESTAMP WITH TIME ZONE,
    script_type TEXT,
    utxo_count BIGINT,
    utxo_value BIGINT,
    utxo_size BIGINT,
    utxo_count_percent DOUBLE PRECISION,
    utxo_value_percent DOUBLE PRECISION,
    utxo_size_percent DOUBLE PRECISION
);

CREATE UNIQUE INDEX utxo_types_block_height_type_idx ON utxo_script_types (block_height, script_type);

