CREATE DATABASE "218261699b8c4a16ae1681698f8a665a";

CREATE SCHEMA bitcoin;

DROP TABLE IF EXISTS bitcoin.blocks CASCADE;

CREATE TABLE bitcoin.blocks
(
    hash               TEXT PRIMARY KEY,
    merkle_root        TEXT,
    time               BIGINT,
    median_time        BIGINT,
    height             BIGINT,
    subsidy            BIGINT,
    transactions_count BIGINT,
    version            BIGINT,
    status             BIGINT,
    bits               BIGINT,
    nonce              BIGINT,
    difficulty         DOUBLE PRECISION,
    chain_work         TEXT,

    segwit_spend_count BIGINT,
    outputs_count      BIGINT,
    inputs_count       BIGINT,
    total_output_value BIGINT,
    total_fees         BIGINT,
    total_size         BIGINT,
    total_vsize        BIGINT,
    total_weight       BIGINT,

    fee_rates          BIGINT[],
    output_data        BIGINT[],
    input_data         BIGINT[],
    transaction_data   BIGINT[]
);
