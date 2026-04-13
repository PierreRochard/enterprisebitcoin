#include <enterprise/schema_setup.h>

#include <array>
#include <pqxx/pqxx>
#include <string>
#include <string_view>

namespace {

struct SnapshotTableSpec {
    std::string_view table;
    std::string_view old_unique_index;
    std::string_view new_unique_index;
    std::string_view new_unique_columns;
    std::string_view block_height_index;
    std::string_view fk_name;
};

struct BlockColumnSpec {
    std::string_view name;
    std::string_view type;
};

constexpr std::array<SnapshotTableSpec, 8> SNAPSHOT_TABLES{{
    {"utxo_age", "utxo_age_block_height_weeks_old_idx", "utxo_age_network_hash_weeks_old_idx", "network, block_hash, weeks_old", "utxo_age_block_height_idx", "utxo_age_snapshot_fk"},
    {"utxo_balances", "utxo_balances_block_height_balance_min_balance_max_idx", "utxo_balances_network_hash_bounds_idx", "network, block_hash, lower_bound, upper_bound", "utxo_balances_block_height_idx", "utxo_balances_snapshot_fk"},
    {"utxo_balances_usd", "utxo_balances_usd_block_height_balance_min_balance_max_idx", "utxo_balances_usd_network_hash_bounds_idx", "network, block_hash, lower_bound, upper_bound", "utxo_balances_usd_block_height_idx", "utxo_balances_usd_snapshot_fk"},
    {"utxo_balances_percentiles", "utxo_balances_percentiles_block_height_percentile_idx", "utxo_balances_percentiles_network_hash_percentile_idx", "network, block_hash, percentile", "utxo_balances_percentiles_block_height_idx", "utxo_balances_percentiles_snapshot_fk"},
    {"utxo_balances_usd_percentiles", "utxo_balances_usd_percentiles_block_height_percentile_idx", "utxo_balances_usd_percentiles_network_hash_percentile_idx", "network, block_hash, percentile", "utxo_balances_usd_percentiles_block_height_idx", "utxo_balances_usd_percentiles_snapshot_fk"},
    {"address_balance_buckets", "address_balance_buckets_block_height_bucket_idx", "address_balance_buckets_network_hash_bounds_idx", "network, block_hash, lower_bound, upper_bound", "address_balance_buckets_block_height_idx", "address_balance_buckets_snapshot_fk"},
    {"address_balance_buckets_usd", "address_balance_buckets_usd_block_height_bucket_idx", "address_balance_buckets_usd_network_hash_bounds_idx", "network, block_hash, lower_bound_cents, upper_bound_cents", "address_balance_buckets_usd_block_height_idx", "address_balance_buckets_usd_snapshot_fk"},
    {"utxo_script_types", "utxo_types_block_height_type_idx", "utxo_script_types_network_hash_type_idx", "network, block_hash, script_type", "utxo_script_types_block_height_idx", "utxo_script_types_snapshot_fk"},
}};

constexpr std::array<BlockColumnSpec, 25> BLOCK_COLUMNS{{
    {"spent_age_blocks_sum", "BIGINT"},
    {"avg_spent_age_blocks", "DOUBLE PRECISION"},
    {"coinblocks_destroyed", "DOUBLE PRECISION"},
    {"coindays_destroyed", "DOUBLE PRECISION"},
    {"min_fee_rate", "DOUBLE PRECISION"},
    {"median_fee_rate", "DOUBLE PRECISION"},
    {"p90_fee_rate", "DOUBLE PRECISION"},
    {"max_fee_rate", "DOUBLE PRECISION"},
    {"inputs_total_witness_size", "BIGINT"},
    {"taproot_inputs_total_witness_size", "BIGINT"},
    {"taproot_key_path_spend_count", "BIGINT"},
    {"taproot_script_path_spend_count", "BIGINT"},
    {"taproot_annex_spend_count", "BIGINT"},
    {"tapscript_spend_count", "BIGINT"},
    {"coinbase_script_sig_size", "BIGINT"},
    {"coinbase_witness_stack_items", "BIGINT"},
    {"coinbase_witness_size", "BIGINT"},
    {"coinbase_outputs_count", "BIGINT"},
    {"has_witness_commitment", "BOOLEAN"},
    {"witness_commitment_index", "BIGINT"},
    {"coinbase_tag", "TEXT"},
    {"version_bits_top_bits_valid", "BOOLEAN"},
    {"version_bits_signalling", "JSONB"},
    {"unknown_version_bits", "JSONB"},
    {"network", "TEXT"},
}};

void Exec(pqxx::work& w, const std::string& sql)
{
    w.exec(sql);
}

void EnsureSnapshotColumns(pqxx::work& w, std::string_view table)
{
    Exec(w, "ALTER TABLE " + std::string(table) + " ADD COLUMN IF NOT EXISTS network TEXT");
    Exec(w, "ALTER TABLE " + std::string(table) + " ADD COLUMN IF NOT EXISTS block_hash TEXT");
}

void EnsureSnapshotForeignKey(pqxx::work& w, const SnapshotTableSpec& spec)
{
    Exec(w,
         "DO $$ "
         "BEGIN "
         "    IF NOT EXISTS (SELECT 1 FROM pg_constraint WHERE conname = '" +
             std::string(spec.fk_name) + "') THEN "
                                         "        ALTER TABLE " +
             std::string(spec.table) +
             "            ADD CONSTRAINT " + std::string(spec.fk_name) +
             "            FOREIGN KEY (network, block_hash) "
             "            REFERENCES utxo_snapshots(network, block_hash) "
             "            ON DELETE CASCADE; "
             "    END IF; "
             "END $$");
}

void EnsureSnapshotIndexes(pqxx::work& w, const SnapshotTableSpec& spec)
{
    Exec(w, "DROP INDEX IF EXISTS " + std::string(spec.old_unique_index));
    Exec(w, "CREATE UNIQUE INDEX IF NOT EXISTS " + std::string(spec.new_unique_index) +
                " ON " + std::string(spec.table) + " (" + std::string(spec.new_unique_columns) + ")");
    Exec(w, "CREATE INDEX IF NOT EXISTS " + std::string(spec.block_height_index) +
                " ON " + std::string(spec.table) + " (block_height)");
}

void EnsureBlockColumns(pqxx::work& w)
{
    for (const auto& spec : BLOCK_COLUMNS) {
        Exec(w, "ALTER TABLE blocks ADD COLUMN IF NOT EXISTS " + std::string(spec.name) + " " + std::string(spec.type));
    }
}

} // namespace

namespace enterprise {

void EnsureBlockExportSchema(pqxx::connection& conn, std::string_view network)
{
    const std::string network_name{network};
    pqxx::work w(conn);
    EnsureBlockColumns(w);
    w.exec(
        pqxx::zview{
            "UPDATE blocks "
            "SET network = $1 "
            "WHERE network IS NULL "
            "  AND EXISTS (SELECT 1 FROM blocks WHERE network IS NULL) "
            "  AND NOT EXISTS (SELECT 1 FROM blocks WHERE network IS NOT NULL)"},
        pqxx::params{network_name});
    Exec(w, "CREATE INDEX IF NOT EXISTS blocks_network_height_idx ON blocks (network, height)");
    Exec(w, "CREATE INDEX IF NOT EXISTS blocks_network_prev_hash_idx ON blocks (network, hash_prev_block)");
    Exec(w,
         "CREATE TABLE IF NOT EXISTS block_address_flows ("
         "    network TEXT NOT NULL,"
         "    block_hash TEXT NOT NULL REFERENCES blocks(hash) ON DELETE CASCADE,"
         "    input_height BIGINT,"
         "    input_median_time TIMESTAMPTZ,"
         "    input_txid TEXT,"
         "    input_wtxid TEXT,"
         "    input_vector BIGINT,"
         "    input_size BIGINT,"
         "    output_height BIGINT NOT NULL,"
         "    output_median_time TIMESTAMPTZ,"
         "    output_txid TEXT NOT NULL,"
         "    output_wtxid TEXT,"
         "    output_vector BIGINT NOT NULL,"
         "    output_size BIGINT NOT NULL,"
         "    output_script_type BIGINT NOT NULL,"
         "    address TEXT,"
         "    amount BIGINT NOT NULL"
         ")");
    Exec(w,
         "DO $$ "
         "BEGIN "
         "    IF EXISTS (SELECT 1 FROM pg_constraint WHERE conname = 'block_address_flows_pkey') THEN "
         "        ALTER TABLE block_address_flows DROP CONSTRAINT block_address_flows_pkey; "
         "    END IF; "
         "    IF EXISTS ("
         "        SELECT 1 "
         "        FROM information_schema.columns "
         "        WHERE table_name = 'block_address_flows' "
         "          AND column_name = 'id'"
         "    ) THEN "
         "        ALTER TABLE block_address_flows ALTER COLUMN id DROP DEFAULT; "
         "        ALTER TABLE block_address_flows ALTER COLUMN id DROP NOT NULL; "
         "    END IF; "
         "END $$");
    Exec(w, "DROP SEQUENCE IF EXISTS block_address_flows_id_seq");
    Exec(w, "CREATE INDEX IF NOT EXISTS block_address_flows_block_hash_idx ON block_address_flows (block_hash)");
    // Keep only the cleanup index during catchup; the large query-serving indexes
    // can be rebuilt later once backfill is complete.
    Exec(w,
         "CREATE TABLE IF NOT EXISTS block_address_flow_export_queue ("
         "    network TEXT NOT NULL,"
         "    block_hash TEXT NOT NULL,"
         "    block_height BIGINT NOT NULL,"
         "    queued_at TIMESTAMPTZ NOT NULL DEFAULT now(),"
         "    last_attempted_at TIMESTAMPTZ,"
         "    attempt_count BIGINT NOT NULL DEFAULT 0,"
         "    last_error TEXT,"
         "    PRIMARY KEY (network, block_hash)"
         ")");
    Exec(w,
         "CREATE INDEX IF NOT EXISTS block_address_flow_export_queue_network_height_idx "
         "ON block_address_flow_export_queue (network, block_height)");
    Exec(w,
         "CREATE TABLE IF NOT EXISTS block_address_flow_exports ("
         "    network TEXT NOT NULL,"
         "    block_hash TEXT NOT NULL,"
         "    block_height BIGINT NOT NULL,"
         "    exported_at TIMESTAMPTZ NOT NULL DEFAULT now(),"
         "    PRIMARY KEY (network, block_hash),"
         "    UNIQUE (network, block_height)"
         ")");
    w.commit();
}

void EnsureUtxoSnapshotSchema(pqxx::connection& conn, std::string_view network)
{
    const std::string network_name{network};

    EnsureBlockExportSchema(conn, network);

    pqxx::work w(conn);
    Exec(w,
         "CREATE TABLE IF NOT EXISTS utxo_snapshots ("
         "    network TEXT NOT NULL,"
         "    block_hash TEXT NOT NULL,"
         "    block_height BIGINT NOT NULL,"
         "    median_time TIMESTAMP,"
         "    exported_at TIMESTAMPTZ NOT NULL DEFAULT now(),"
         "    PRIMARY KEY (network, block_hash),"
         "    UNIQUE (network, block_height)"
         ")");
    Exec(w,
         "CREATE TABLE IF NOT EXISTS utxo_snapshot_export_queue ("
         "    network TEXT NOT NULL,"
         "    block_hash TEXT NOT NULL,"
         "    block_height BIGINT NOT NULL,"
         "    payload TEXT NOT NULL,"
         "    queued_at TIMESTAMPTZ NOT NULL DEFAULT now(),"
         "    last_attempted_at TIMESTAMPTZ,"
         "    attempt_count BIGINT NOT NULL DEFAULT 0,"
         "    last_error TEXT,"
         "    PRIMARY KEY (network, block_hash)"
         ")");
    Exec(w,
         "CREATE INDEX IF NOT EXISTS utxo_snapshot_export_queue_network_height_idx "
         "ON utxo_snapshot_export_queue (network, block_height)");

    for (const auto& spec : SNAPSHOT_TABLES) {
        EnsureSnapshotColumns(w, spec.table);
    }

    // Migrate legacy height-only rows to the current network/hash identity using the
    // active-chain block rows that already exist in the blocks table.
    w.exec(
        pqxx::zview{
            "INSERT INTO utxo_snapshots(network, block_hash, block_height, median_time) "
            "SELECT $1, b.hash, u.block_height, COALESCE(MAX(u.median_time), (b.median_time AT TIME ZONE 'UTC')) "
            "FROM utxo_age u "
            "JOIN blocks b ON b.network = $1 AND b.height = u.block_height "
            "WHERE u.network IS NULL OR u.block_hash IS NULL "
            "GROUP BY b.hash, u.block_height, b.median_time "
            "ON CONFLICT (network, block_hash) DO NOTHING"},
        pqxx::params{network_name});

    for (const auto& spec : SNAPSHOT_TABLES) {
        const std::string update_query =
            "UPDATE " + std::string(spec.table) + " t "
                                                  "SET network = s.network, block_hash = s.block_hash "
                                                  "FROM utxo_snapshots s "
                                                  "WHERE (t.network IS NULL OR t.block_hash IS NULL) "
                                                  "  AND t.block_height = s.block_height "
                                                  "  AND s.network = $1";
        w.exec(update_query, pqxx::params{network_name});
    }

    for (const auto& spec : SNAPSHOT_TABLES) {
        EnsureSnapshotForeignKey(w, spec);
        EnsureSnapshotIndexes(w, spec);
    }

    w.commit();
}

} // namespace enterprise
