CREATE DATABASE "218261699b8c4a16ae1681698f8a665a";

CREATE SCHEMA bitcoin;

DROP TABLE IF EXISTS bitcoin.blocks CASCADE;

CREATE TABLE bitcoin.blocks
(
    hash                               TEXT PRIMARY KEY,
    network                            TEXT,
    hash_prev_block                    TEXT,
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

    nonstandard_create_count           BIGINT,
    pubkey_create_count                BIGINT,
    pubkeyhash_create_count            BIGINT,
    scripthash_create_count            BIGINT,
    multisig_create_count              BIGINT,
    null_data_create_count             BIGINT,
    witness_v0_keyhash_create_count    BIGINT,
    witness_v0_scripthash_create_count BIGINT,
    witness_v1_taproot_create_count    BIGINT,
    witness_unknown_create_count       BIGINT,

    nonstandard_spend_count            BIGINT,
    pubkey_spend_count                 BIGINT,
    pubkeyhash_spend_count             BIGINT,
    scripthash_spend_count             BIGINT,
    multisig_spend_count               BIGINT,
    null_data_spend_count              BIGINT,
    witness_v0_keyhash_spend_count     BIGINT,
    witness_v0_scripthash_spend_count  BIGINT,
    witness_v1_taproot_spend_count     BIGINT,
    witness_unknown_spend_count        BIGINT,

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
    input_witness_signature_operations BIGINT,

    outputs_total_size                 BIGINT,
    inputs_total_size                  BIGINT,
    net_utxo_size_impact               BIGINT
);

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