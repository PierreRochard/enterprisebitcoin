CREATE DATABASE "218261699b8c4a16ae1681698f8a665a";

CREATE SCHEMA bitcoin;

DROP TABLE IF EXISTS bitcoin.blocks CASCADE;

CREATE TABLE bitcoin.blocks
(
    id                 SERIAL PRIMARY KEY,
    hash               TEXT,
    merkle_root        TEXT,
    time               BIGINT,
    median_time        BIGINT,
    height             BIGINT,
    subsidy            BIGINT,
    transactions_count INTEGER,
    version            INTEGER,
    status             INTEGER,
    bits               INTEGER,
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

    fee_rates          INT[],
    output_data        INT[],
    input_data         INT[],
    transaction_data   INT[]
);
