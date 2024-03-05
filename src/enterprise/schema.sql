CREATE DATABASE "bitcoin";

CREATE SCHEMA bitcoin;

DROP TABLE IF EXISTS blocks CASCADE;

CREATE TABLE blocks
(
    hash                                             TEXT PRIMARY KEY,
    network                                          TEXT,
    hash_prev_block                                  TEXT,
    merkle_root                                      TEXT,
    time                                             timestamp with time zone,

    median_time                                      timestamp with time zone,
    height                                           BIGINT,
    subsidy                                          BIGINT,

    transactions_count                               BIGINT,
    version                                          BIGINT,
    status                                           BIGINT,

    bits                                             BIGINT,
    nonce                                            BIGINT,
    difficulty                                       DOUBLE PRECISION,
    chain_work                                       TEXT,

    nonstandard_create_count                         BIGINT,
    pubkey_create_count                              BIGINT,
    pubkeyhash_create_count                          BIGINT,
    scripthash_create_count                          BIGINT,
    multisig_create_count                            BIGINT,
    null_data_create_count                           BIGINT,
    witness_v0_keyhash_create_count                  BIGINT,
    witness_v0_scripthash_create_count               BIGINT,
    witness_v1_taproot_create_count                  BIGINT,
    witness_unknown_create_count                     BIGINT,

    nonstandard_spend_count                          BIGINT,
    pubkey_spend_count                               BIGINT,
    pubkeyhash_spend_count                           BIGINT,
    scripthash_spend_count                           BIGINT,
    multisig_spend_count                             BIGINT,
    null_data_spend_count                            BIGINT,
    witness_v0_keyhash_spend_count                   BIGINT,
    witness_v0_scripthash_spend_count                BIGINT,
    witness_v1_taproot_spend_count                   BIGINT,
    witness_unknown_spend_count                      BIGINT,

    outputs_count                                    BIGINT,
    inputs_count                                     BIGINT,

    total_output_value                               BIGINT,
    total_input_value                                BIGINT,
    total_fees                                       BIGINT,

    total_size                                       BIGINT,
    total_vsize                                      BIGINT,
    total_weight                                     BIGINT,

    fee_rates                                        JSONB,
    output_data                                      JSONB,
    input_data                                       JSONB,

    transaction_data                                 JSONB,
    output_script_types                              JSONB,
    input_script_types                               JSONB,

    output_legacy_signature_operations               BIGINT,
    input_legacy_signature_operations                BIGINT,
    input_p2sh_signature_operations                  BIGINT,
    input_witness_signature_operations               BIGINT,

    outputs_total_size                               BIGINT,
    inputs_total_size                                BIGINT,
    net_utxo_size_impact                             BIGINT,
    coinbase                                         BIGINT,

    ordinals_weight                                  BIGINT,
    ordinals_count                                   BIGINT,
    ordinals_size                                    BIGINT,
    ordinals_vsize                                   BIGINT,
    ordinals_fees                                    BIGINT,

    non_ordinals_weight                              BIGINT,
    non_ordinals_count                               BIGINT,
    non_ordinals_size                                BIGINT,
    non_ordinals_vsize                               BIGINT,
    non_ordinals_fees                                BIGINT
);
