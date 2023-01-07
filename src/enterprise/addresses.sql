DROP TABLE IF EXISTS bitcoin.addresses CASCADE;

CREATE TABLE bitcoin.addresses
(
    id                 serial primary key,
    network            TEXT,

    input_height       BIGINT,
    input_median_time  TIMESTAMP WITH TIME ZONE,
    input_txid         TEXT,
    input_wtxid        TEXT,
    input_vector       BIGINT,
    input_size         BIGINT,

    output_height      BIGINT,
    output_median_time TIMESTAMP WITH TIME ZONE,
    output_txid        TEXT,
    output_wtxid       TEXT,
    output_vector      BIGINT,
    output_size        BIGINT,
    output_script_type TEXT,

    address            TEXT,
    amount             BIGINT
);

CREATE UNIQUE INDEX bitcoin_addresses_idx ON bitcoin.addresses (network, height, txid, vector, amount);
