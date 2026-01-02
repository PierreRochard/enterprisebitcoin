
DROP TABLE IF EXISTS bitcoin.mempool_entries CASCADE;
DROP TABLE IF EXISTS bitcoin.mempool_families CASCADE;


CREATE TABLE bitcoin.mempool_entries
(
    txid                 TEXT PRIMARY KEY,
    network              TEXT,
    wtxid                TEXT,
    fee                  BIGINT,
    weight               BIGINT,

    memory_usage         BIGINT,
    entry_time           timestamp with time zone,

    entry_height         BIGINT,
    spends_coinbase      BOOLEAN,
    sigop_cost           BIGINT,

    fee_delta            BIGINT,
    height_lockpoint     BIGINT,
    time_lockpoint       timestamp with time zone,

    descendants_count    BIGINT,
    descendants_size     BIGINT,
    descendants_fees     BIGINT,

    ancestors_count      BIGINT,
    ancestors_size       BIGINT,
    ancestors_fees       BIGINT,
    ancestors_sigop_cost BIGINT,

    removal_reason       TEXT,
    removal_time         timestamp with time zone
);

CREATE TABLE bitcoin.utxo_set_statistics
(
    primary_key         BIGSERIAL PRIMARY KEY,
    stats_height        BIGINT,
    stats_day           DATE,

    output_height       BIGINT,
    output_day          DATE,
    output_days_count   BIGINT,
    output_script_type  BIGINT,
    output_is_coinbase  BOOLEAN,

    outputs_count       BIGINT,
    outputs_total_value BIGINT,
    outputs_total_size  BIGINT
);

CREATE UNIQUE INDEX utxo_set_statistics_index
    ON bitcoin.utxo_set_statistics (stats_day, output_height, output_script_type, output_is_coinbase);

CREATE INDEX utxo_set_date_index
    ON bitcoin.utxo_set_statistics (stats_day);