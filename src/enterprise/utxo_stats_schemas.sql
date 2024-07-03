CREATE TABLE utxo_age (
    id SERIAL PRIMARY KEY,
    block_height BIGINT,
    median_time TIMESTAMP,
    weeks_old BIGINT,
    utxo_count BIGINT,
    utxo_value BIGINT,
    utxo_size BIGINT,
    utxo_count_percent FLOAT,
    utxo_value_percent FLOAT,
    utxo_size_percent FLOAT
);

CREATE UNIQUE INDEX utxo_age_block_height_weeks_old_idx ON utxo_age (block_height, weeks_old);

CREATE TABLE utxo_balances (
    id SERIAL PRIMARY KEY,
    block_height BIGINT,
    median_time TIMESTAMP,
    lower_bound BIGINT,
    upper_bound BIGINT,
    utxo_count BIGINT,
    utxo_value BIGINT,
    utxo_size BIGINT,
    utxo_count_percent FLOAT,
    utxo_value_percent FLOAT,
    utxo_size_percent FLOAT
);

CREATE UNIQUE INDEX utxo_balances_block_height_balance_min_balance_max_idx ON utxo_balances (block_height, lower_bound, upper_bound);

CREATE TABLE utxo_balances_usd (
                               id SERIAL PRIMARY KEY,
                               block_height BIGINT,
                               median_time TIMESTAMP,
                               lower_bound BIGINT,
                               upper_bound BIGINT,
                               utxo_count BIGINT,
                               utxo_value BIGINT,
                               utxo_size BIGINT,
                               utxo_count_percent FLOAT,
                               utxo_value_percent FLOAT,
                               utxo_size_percent FLOAT
);

CREATE UNIQUE INDEX utxo_balances_usd_block_height_balance_min_balance_max_idx ON utxo_balances_usd (block_height, lower_bound, upper_bound);

CREATE TABLE utxo_script_types (
    id SERIAL PRIMARY KEY,
    block_height BIGINT,
    median_time TIMESTAMP,
    script_type TEXT,
    utxo_count BIGINT,
    utxo_value BIGINT,
    utxo_size BIGINT,
    utxo_count_percent FLOAT,
    utxo_value_percent FLOAT,
    utxo_size_percent FLOAT
);

CREATE UNIQUE INDEX utxo_types_block_height_type_idx ON utxo_script_types (block_height, script_type);

