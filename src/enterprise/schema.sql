CREATE DATABASE "218261699b8c4a16ae1681698f8a665a";

CREATE SCHEMA bitcoin;

DROP TABLE IF EXISTS bitcoin.blocks CASCADE;

CREATE TABLE bitcoin.blocks
(
    hash                               TEXT PRIMARY KEY,
    merkle_root                        TEXT,
    time                               timestamp with time zone,

    median_time                        timestamp with time zone,
    height                             BIGINT,
    subsidy                            BIGINT,

    transactions_count                 BIGINT,
    version                            BIGINT,
    status                             BIGINT,

    bits                               BIGINT,
    nonce                              BIGINT,
    difficulty                         DOUBLE PRECISION,

    chain_work                         TEXT,
    segwit_spend_count                 BIGINT,
    outputs_count                      BIGINT,

    inputs_count                       BIGINT,
    total_output_value                 BIGINT,
    total_input_value                  BIGINT,
    total_fees                         BIGINT,

    total_size                         BIGINT,
    total_vsize                        BIGINT,
    total_weight                       BIGINT,

    fee_rates                          JSONB,
    output_data                        JSONB,
    input_data                         JSONB,

    transaction_data                   JSONB,
    output_script_types                JSONB,
    input_script_types                 JSONB,

    output_legacy_signature_operations BIGINT,
    input_legacy_signature_operations  BIGINT,
    input_p2sh_signature_operations    BIGINT,
    input_witness_signature_operations BIGINT
);
