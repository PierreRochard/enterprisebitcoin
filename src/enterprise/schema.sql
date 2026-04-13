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
    non_ordinals_fees                                BIGINT,

    spent_age_blocks_sum                             BIGINT,
    avg_spent_age_blocks                             DOUBLE PRECISION,
    coinblocks_destroyed                             DOUBLE PRECISION,
    coindays_destroyed                               DOUBLE PRECISION,

    min_fee_rate                                     DOUBLE PRECISION,
    median_fee_rate                                  DOUBLE PRECISION,
    p90_fee_rate                                     DOUBLE PRECISION,
    max_fee_rate                                     DOUBLE PRECISION,

    inputs_total_witness_size                        BIGINT,
    taproot_inputs_total_witness_size                BIGINT,
    taproot_key_path_spend_count                     BIGINT,
    taproot_script_path_spend_count                  BIGINT,
    taproot_annex_spend_count                        BIGINT,
    tapscript_spend_count                            BIGINT,

    coinbase_script_sig_size                         BIGINT,
    coinbase_witness_stack_items                     BIGINT,
    coinbase_witness_size                            BIGINT,
    coinbase_outputs_count                           BIGINT,
    has_witness_commitment                           BOOLEAN,
    witness_commitment_index                         BIGINT,
    coinbase_tag                                     TEXT,

    version_bits_top_bits_valid                      BOOLEAN,
    version_bits_signalling                          JSONB,
    unknown_version_bits                             JSONB
);

CREATE TABLE block_address_flow_export_queue
(
    network                                          TEXT NOT NULL,
    block_hash                                       TEXT NOT NULL,
    block_height                                     BIGINT NOT NULL,
    queued_at                                        TIMESTAMPTZ NOT NULL DEFAULT now(),
    last_attempted_at                                TIMESTAMPTZ,
    attempt_count                                    BIGINT NOT NULL DEFAULT 0,
    last_error                                       TEXT,
    PRIMARY KEY (network, block_hash)
);

CREATE INDEX block_address_flow_export_queue_network_height_idx
    ON block_address_flow_export_queue (network, block_height);

CREATE TABLE block_address_flow_exports
(
    network                                          TEXT NOT NULL,
    block_hash                                       TEXT NOT NULL,
    block_height                                     BIGINT NOT NULL,
    exported_at                                      TIMESTAMPTZ NOT NULL DEFAULT now(),
    PRIMARY KEY (network, block_hash),
    UNIQUE (network, block_height)
);

CREATE TABLE address_flow_block_summaries
(
    network                                          TEXT NOT NULL,
    block_hash                                       TEXT NOT NULL,
    block_height                                     BIGINT NOT NULL,
    day                                              DATE NOT NULL,
    median_time                                      TIMESTAMPTZ NOT NULL,
    received_sats                                    BIGINT NOT NULL,
    spent_sats                                       BIGINT NOT NULL,
    net_sats                                         BIGINT NOT NULL,
    tx_count                                         BIGINT NOT NULL,
    exported_at                                      TIMESTAMPTZ NOT NULL DEFAULT now(),
    PRIMARY KEY (network, block_hash),
    UNIQUE (network, block_height)
);

CREATE INDEX address_flow_block_summaries_network_day_height_idx
    ON address_flow_block_summaries (network, day, block_height);

CREATE TABLE address_flow_daily
(
    network                                          TEXT NOT NULL,
    day                                              DATE NOT NULL,
    received_sats                                    BIGINT NOT NULL,
    spent_sats                                       BIGINT NOT NULL,
    net_sats                                         BIGINT NOT NULL,
    block_count                                      BIGINT NOT NULL,
    tx_count                                         BIGINT NOT NULL,
    last_block_height                                BIGINT NOT NULL,
    last_block_hash                                  TEXT NOT NULL,
    median_time                                      TIMESTAMPTZ NOT NULL,
    updated_at                                       TIMESTAMPTZ NOT NULL DEFAULT now(),
    PRIMARY KEY (network, day)
);
