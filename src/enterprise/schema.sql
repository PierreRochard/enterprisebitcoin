CREATE DATABASE "bitcoin";

CREATE SCHEMA bitcoin;

DROP TABLE IF EXISTS bitcoin.blocks CASCADE;

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

    utxo_set_value                                   BIGINT,
    utxo_set_value_satoshi                           BIGINT,
    utxo_set_value_less_than_one_day                 BIGINT,
    utxo_set_value_less_than_one_week                BIGINT,
    utxo_set_value_less_than_one_month               BIGINT,
    utxo_set_value_less_than_three_months            BIGINT,
    utxo_set_value_less_than_six_months              BIGINT,
    utxo_set_value_less_than_one_year                BIGINT,
    utxo_set_value_less_than_two_years               BIGINT,
    utxo_set_value_less_than_three_years             BIGINT,
    utxo_set_value_less_than_four_years              BIGINT,
    utxo_set_value_less_than_five_years              BIGINT,
    utxo_set_value_less_than_six_years               BIGINT,
    utxo_set_value_less_than_seven_years             BIGINT,
    utxo_set_value_less_than_eight_years             BIGINT,
    utxo_set_value_less_than_nine_years              BIGINT,
    utxo_set_value_less_than_ten_years               BIGINT,
    utxo_set_value_less_than_eleven_years            BIGINT,
    utxo_set_value_less_than_twelve_years            BIGINT,
    utxo_set_value_less_than_thirteen_years          BIGINT,
    utxo_set_value_less_than_fourteen_years          BIGINT,
    utxo_set_value_less_than_fifteen_years           BIGINT,

    utxo_set_count                                  BIGINT,
    utxo_set_count_satoshi                          BIGINT,
    utxo_set_count_less_than_one_day                BIGINT,
    utxo_set_count_less_than_one_week               BIGINT,
    utxo_set_count_less_than_one_month              BIGINT,
    utxo_set_count_less_than_three_months           BIGINT,
    utxo_set_count_less_than_six_months             BIGINT,
    utxo_set_count_less_than_one_year               BIGINT,
    utxo_set_count_less_than_two_years              BIGINT,
    utxo_set_count_less_than_three_years            BIGINT,
    utxo_set_count_less_than_four_years             BIGINT,
    utxo_set_count_less_than_five_years             BIGINT,
    utxo_set_count_less_than_six_years              BIGINT,
    utxo_set_count_less_than_seven_years            BIGINT,
    utxo_set_count_less_than_eight_years            BIGINT,
    utxo_set_count_less_than_nine_years             BIGINT,
    utxo_set_count_less_than_ten_years              BIGINT,
    utxo_set_count_less_than_eleven_years           BIGINT,
    utxo_set_count_less_than_twelve_years           BIGINT,
    utxo_set_count_less_than_thirteen_years         BIGINT,
    utxo_set_count_less_than_fourteen_years         BIGINT,
    utxo_set_count_less_than_fifteen_years          BIGINT
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