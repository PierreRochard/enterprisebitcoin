-- DESTRUCTIVE FRESH-DATABASE BOOTSTRAP ONLY.
-- To preserve an existing PostgreSQL 17 database, use the additive migration
-- in src/enterprise/migrations/20260711_pg17_reuse_v1.sql instead.

DROP TABLE IF EXISTS blocks CASCADE;
DROP TABLE IF EXISTS mempool_entries CASCADE;

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

    btc_usd_price                                   DOUBLE PRECISION,
    btc_usd_price_low                               DOUBLE PRECISION,
    btc_usd_price_high                              DOUBLE PRECISION,
    btc_usd_price_source                            TEXT,
    denomination_eligible_outputs_count             BIGINT,
    denomination_eligible_value_sats                BIGINT,

    usd_denom_outputs_count                         BIGINT,
    usd_denom_value_sats                            BIGINT,
    usd_denom_confidence_sum                        DOUBLE PRECISION,

    sats_denom_outputs_count                        BIGINT,
    sats_denom_value_sats                           BIGINT,
    sats_denom_confidence_sum                       DOUBLE PRECISION,

    unknown_denom_outputs_count                     BIGINT,
    unknown_denom_value_sats                        BIGINT,

    ambiguous_denom_outputs_count                   BIGINT,
    ambiguous_denom_value_sats                      BIGINT,

    likely_change_outputs_count                     BIGINT,
    likely_change_value_sats                        BIGINT,

    denomination_classifier_version                 TEXT
);

CREATE UNIQUE INDEX IF NOT EXISTS blocks_network_height_idx ON blocks(network, height);

CREATE TABLE IF NOT EXISTS enterprise_block_ingest
(
    network                                          TEXT NOT NULL,
    hash                                             TEXT NOT NULL,
    height                                           BIGINT NOT NULL,
    event_type                                       TEXT NOT NULL,
    status                                           TEXT NOT NULL,
    source                                           TEXT NOT NULL,
    spool_path                                       TEXT,
    attempts                                         BIGINT NOT NULL DEFAULT 0,
    last_error                                       TEXT,
    queued_at                                        timestamp with time zone NOT NULL DEFAULT now(),
    updated_at                                       timestamp with time zone NOT NULL DEFAULT now(),
    completed_at                                     timestamp with time zone,
    PRIMARY KEY (network, hash, event_type)
);

CREATE INDEX IF NOT EXISTS enterprise_block_ingest_monitor_idx
    ON enterprise_block_ingest (network, source, status, height);

CREATE TABLE IF NOT EXISTS enterprise_block_gaps
(
    network                                          TEXT NOT NULL,
    height                                           BIGINT NOT NULL,
    expected_hash                                    TEXT,
    status                                           TEXT NOT NULL,
    source                                           TEXT,
    last_error                                       TEXT,
    first_seen_at                                    timestamp with time zone NOT NULL DEFAULT now(),
    updated_at                                       timestamp with time zone NOT NULL DEFAULT now(),
    PRIMARY KEY (network, height)
);

CREATE TABLE IF NOT EXISTS enterprise_schema_migrations
(
    version     TEXT PRIMARY KEY,
    description TEXT NOT NULL,
    applied_at  timestamp with time zone NOT NULL DEFAULT now(),
    applied_by  TEXT NOT NULL DEFAULT SESSION_USER
);

INSERT INTO enterprise_schema_migrations (version, description)
VALUES (
    '20260711_pg17_reuse_v1',
    'Additive PostgreSQL 17 reuse schema for enterprise denomination backfill'
)
ON CONFLICT (version) DO NOTHING;

CREATE TABLE mempool_entries
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

CREATE TABLE IF NOT EXISTS prices
(
    day          timestamp with time zone PRIMARY KEY,
    price        DOUBLE PRECISION NOT NULL,
    price_low    DOUBLE PRECISION,
    price_high   DOUBLE PRECISION,
    price_source TEXT
);
