#include <chain.h>
#include <chainparams.h>
#include <consensus/validation.h>
#include <core_io.h>
#include <key_io.h>
#include <logging.h>
#include <pubkey.h>
#include <primitives/block.h>
#include <streams.h>
#include <script/interpreter.h>
#include <serialize.h>
#include <interfaces/chain.h>
#include <validation.h>
#include <node/transaction.h>
#include <index/txindex.h>
#include <txmempool.h>
#include <common/system.h>
#include <common/args.h>
#include <rpc/blockchain.h>
#include <array>
#include <cmath>
#include <ctime>
#include <map>
#include <string>
#include <algorithm>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <limits>
#include <mutex>

#include <enterprise/common.h>
#include <enterprise/utxo_set_to_sql.h>
#include <enterprise/utilities.h>

#include <enterprise/db.h>
#include <enterprise/dotenv.h>
#include <enterprise/schema_setup.h>
#include <pqxx/pqxx>
#include <util/serfloat.h>
#include <util/strencodings.h>
#include <util/time.h>

std::map<int, double> calculatePercentiles(std::vector<double>& data) {
    std::map<int, double> percentiles;

    // Sort the data
    std::sort(data.begin(), data.end());

    // Calculate percentiles from 1 to 99
    for (int p = 1; p <= 99; ++p) {
        // Determine rank
        double rank = (p / 100.0) * (data.size() - 1);
        int lowIndex = std::floor(rank);
        int highIndex = std::ceil(rank);

        // Interpolate if necessary
        if (lowIndex == highIndex) {
            percentiles[p] = data[lowIndex];
        } else {
            double fraction = rank - lowIndex;
            percentiles[p] = data[lowIndex] + fraction * (data[highIndex] - data[lowIndex]);
            percentiles[p] = std::round(percentiles[p] * 100) / 100;
        }
    }

    return percentiles;
}

namespace {

std::once_flag g_schema_ready;
constexpr std::string_view UTXO_SNAPSHOT_QUEUE_TABLE{"utxo_snapshot_export_queue"};

std::string FormatUtcTimestamp(int64_t timestamp)
{
    std::tm timeinfo{};
    const std::time_t raw_time{static_cast<std::time_t>(timestamp)};
    if (gmtime_r(&raw_time, &timeinfo) == nullptr) {
        throw std::runtime_error("Failed to format UTC timestamp");
    }

    char buffer[80];
    if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo) == 0) {
        throw std::runtime_error("Failed to render UTC timestamp");
    }
    return buffer;
}

void EnsureSnapshotSchema()
{
    std::call_once(g_schema_ready, [] {
        auto& conn = enterprise::PgConnection();
        enterprise::EnsureUtxoSnapshotSchema(conn, enterprise::ChainName());
    });
}

bool SnapshotExists(const CBlockIndex& block_index)
{
    EnsureSnapshotSchema();
    auto& conn = enterprise::PgConnection();
    pqxx::work w(conn);
    const auto res = w.exec(
        "SELECT 1 FROM utxo_snapshots WHERE network = $1 AND block_hash = $2 "
        "UNION ALL "
        "SELECT 1 FROM " + std::string(UTXO_SNAPSHOT_QUEUE_TABLE) + " WHERE network = $1 AND block_hash = $2 "
        "LIMIT 1",
        pqxx::params{enterprise::ChainName(), block_index.GetBlockHash().GetHex()});
    w.commit();
    return !res.empty();
}

struct UtxoAgeRow {
    unsigned int weeks_old;
    CAmount utxo_value;
    unsigned int utxo_count;
    int64_t utxo_size;
    double count_percentage;
    double value_percentage;
    double size_percentage;

    template <typename Stream>
    void Serialize(Stream& s) const
    {
        const uint64_t count_percentage_bits = EncodeDouble(count_percentage);
        const uint64_t value_percentage_bits = EncodeDouble(value_percentage);
        const uint64_t size_percentage_bits = EncodeDouble(size_percentage);
        ::SerializeMany(s, weeks_old, utxo_value, utxo_count, utxo_size,
                        count_percentage_bits, value_percentage_bits, size_percentage_bits);
    }

    template <typename Stream>
    void Unserialize(Stream& s)
    {
        uint64_t count_percentage_bits;
        uint64_t value_percentage_bits;
        uint64_t size_percentage_bits;
        ::UnserializeMany(s, weeks_old, utxo_value, utxo_count, utxo_size,
                          count_percentage_bits, value_percentage_bits, size_percentage_bits);
        count_percentage = DecodeDouble(count_percentage_bits);
        value_percentage = DecodeDouble(value_percentage_bits);
        size_percentage = DecodeDouble(size_percentage_bits);
    }
};

struct UtxoBalanceRow {
    int64_t lower_bound;
    int64_t upper_bound;
    CAmount utxo_value;
    unsigned int utxo_count;
    int64_t utxo_size;
    double count_percentage;
    double value_percentage;
    double size_percentage;

    template <typename Stream>
    void Serialize(Stream& s) const
    {
        const uint64_t count_percentage_bits = EncodeDouble(count_percentage);
        const uint64_t value_percentage_bits = EncodeDouble(value_percentage);
        const uint64_t size_percentage_bits = EncodeDouble(size_percentage);
        ::SerializeMany(s, lower_bound, upper_bound, utxo_value, utxo_count,
                        utxo_size, count_percentage_bits, value_percentage_bits, size_percentage_bits);
    }

    template <typename Stream>
    void Unserialize(Stream& s)
    {
        uint64_t count_percentage_bits;
        uint64_t value_percentage_bits;
        uint64_t size_percentage_bits;
        ::UnserializeMany(s, lower_bound, upper_bound, utxo_value, utxo_count,
                          utxo_size, count_percentage_bits, value_percentage_bits, size_percentage_bits);
        count_percentage = DecodeDouble(count_percentage_bits);
        value_percentage = DecodeDouble(value_percentage_bits);
        size_percentage = DecodeDouble(size_percentage_bits);
    }
};

struct UtxoBalanceUsdRow {
    int64_t lower_bound;
    int64_t upper_bound;
    int64_t utxo_value;
    unsigned int utxo_count;
    int64_t utxo_size;
    double count_percentage;
    double value_percentage;
    double size_percentage;

    template <typename Stream>
    void Serialize(Stream& s) const
    {
        const uint64_t count_percentage_bits = EncodeDouble(count_percentage);
        const uint64_t value_percentage_bits = EncodeDouble(value_percentage);
        const uint64_t size_percentage_bits = EncodeDouble(size_percentage);
        ::SerializeMany(s, lower_bound, upper_bound, utxo_value, utxo_count,
                        utxo_size, count_percentage_bits, value_percentage_bits, size_percentage_bits);
    }

    template <typename Stream>
    void Unserialize(Stream& s)
    {
        uint64_t count_percentage_bits;
        uint64_t value_percentage_bits;
        uint64_t size_percentage_bits;
        ::UnserializeMany(s, lower_bound, upper_bound, utxo_value, utxo_count,
                          utxo_size, count_percentage_bits, value_percentage_bits, size_percentage_bits);
        count_percentage = DecodeDouble(count_percentage_bits);
        value_percentage = DecodeDouble(value_percentage_bits);
        size_percentage = DecodeDouble(size_percentage_bits);
    }
};

struct UtxoPercentileRow {
    int percentile;
    double utxo_value;

    template <typename Stream>
    void Serialize(Stream& s) const
    {
        const uint64_t utxo_value_bits = EncodeDouble(utxo_value);
        ::SerializeMany(s, percentile, utxo_value_bits);
    }

    template <typename Stream>
    void Unserialize(Stream& s)
    {
        uint64_t utxo_value_bits;
        ::UnserializeMany(s, percentile, utxo_value_bits);
        utxo_value = DecodeDouble(utxo_value_bits);
    }
};

struct AddressBalanceBucketRow {
    int64_t lower_bound;
    int64_t upper_bound;
    uint64_t address_count;

    SERIALIZE_METHODS(AddressBalanceBucketRow, obj)
    {
        READWRITE(obj.lower_bound, obj.upper_bound, obj.address_count);
    }
};

struct AddressBalanceBucketUsdRow {
    int64_t lower_bound_cents;
    int64_t upper_bound_cents;
    uint64_t address_count;

    SERIALIZE_METHODS(AddressBalanceBucketUsdRow, obj)
    {
        READWRITE(obj.lower_bound_cents, obj.upper_bound_cents, obj.address_count);
    }
};

struct UtxoScriptTypeRow {
    std::string script_type;
    CAmount utxo_value;
    unsigned int utxo_count;
    int64_t utxo_size;
    double count_percentage;
    double value_percentage;
    double size_percentage;

    template <typename Stream>
    void Serialize(Stream& s) const
    {
        const uint64_t count_percentage_bits = EncodeDouble(count_percentage);
        const uint64_t value_percentage_bits = EncodeDouble(value_percentage);
        const uint64_t size_percentage_bits = EncodeDouble(size_percentage);
        ::SerializeMany(s, script_type, utxo_value, utxo_count, utxo_size,
                        count_percentage_bits, value_percentage_bits, size_percentage_bits);
    }

    template <typename Stream>
    void Unserialize(Stream& s)
    {
        uint64_t count_percentage_bits;
        uint64_t value_percentage_bits;
        uint64_t size_percentage_bits;
        ::UnserializeMany(s, script_type, utxo_value, utxo_count, utxo_size,
                          count_percentage_bits, value_percentage_bits, size_percentage_bits);
        count_percentage = DecodeDouble(count_percentage_bits);
        value_percentage = DecodeDouble(value_percentage_bits);
        size_percentage = DecodeDouble(size_percentage_bits);
    }
};

struct UtxoSetExportData {
    std::string network;
    std::string block_hash;
    int block_height;
    std::string median_time;
    std::vector<UtxoAgeRow> utxo_age_rows;
    std::vector<UtxoBalanceRow> utxo_balance_rows;
    std::vector<UtxoBalanceUsdRow> utxo_balance_usd_rows;
    std::vector<UtxoPercentileRow> utxo_percentiles;
    std::vector<UtxoPercentileRow> utxo_usd_percentiles;
    std::vector<AddressBalanceBucketRow> address_balance_bucket_rows;
    std::vector<AddressBalanceBucketUsdRow> address_balance_bucket_usd_rows;
    std::vector<UtxoScriptTypeRow> script_type_rows;

    SERIALIZE_METHODS(UtxoSetExportData, obj)
    {
        READWRITE(obj.network, obj.block_hash, obj.block_height, obj.median_time,
                  obj.utxo_age_rows, obj.utxo_balance_rows, obj.utxo_balance_usd_rows,
                  obj.utxo_percentiles, obj.utxo_usd_percentiles,
                  obj.address_balance_bucket_rows, obj.address_balance_bucket_usd_rows,
                  obj.script_type_rows);
    }
};

std::string SerializeExportData(const UtxoSetExportData& data)
{
    DataStream stream;
    stream << data;
    return HexStr(stream);
}

UtxoSetExportData DeserializeExportData(std::string_view payload_hex)
{
    const auto serialized = ParseHex(payload_hex);
    DataStream stream{serialized};
    UtxoSetExportData data;
    stream >> data;
    if (!stream.empty()) {
        throw std::runtime_error("Queued UTXO snapshot payload has trailing data");
    }
    return data;
}

void DeleteQueuedSnapshot(pqxx::work& w, std::string_view network, std::string_view block_hash)
{
    w.exec(
        pqxx::zview{"DELETE FROM utxo_snapshot_export_queue WHERE network = $1 AND block_hash = $2"},
        pqxx::params{std::string(network), std::string(block_hash)});
}

void RecordQueuedSnapshotFailure(pqxx::connection& conn, std::string_view network, std::string_view block_hash, std::string_view error)
{
    try {
        pqxx::work w(conn);
        w.exec(
            pqxx::zview{
                "UPDATE utxo_snapshot_export_queue "
                "SET last_attempted_at = now(), attempt_count = attempt_count + 1, last_error = $3 "
                "WHERE network = $1 AND block_hash = $2"},
            pqxx::params{std::string(network), std::string(block_hash), std::string(error)});
        w.commit();
    } catch (const std::exception& update_error) {
        LogWarning("UtxoSetToSql: failed to record queued snapshot failure for %s: %s",
                   std::string(block_hash).c_str(), update_error.what());
    }
}

void WriteQueuedSnapshot(pqxx::connection& conn, std::string_view network, std::string_view block_hash)
{
    enterprise::EnsureUtxoSnapshotSchema(conn, network);
    pqxx::work w(conn);
    const auto result = w.exec(
        pqxx::zview{
            "SELECT payload FROM utxo_snapshot_export_queue WHERE network = $1 AND block_hash = $2 LIMIT 1"},
        pqxx::params{std::string(network), std::string(block_hash)});
    if (result.empty()) {
        w.commit();
        return;
    }

    const UtxoSetExportData data = DeserializeExportData(result[0][0].as<std::string>());

    // Replace any prior snapshot at the same active-chain height.
    w.exec(pqxx::zview{"DELETE FROM utxo_snapshots WHERE network = $1 AND block_height = $2"},
           pqxx::params{data.network, data.block_height});
    w.exec(pqxx::zview{
               "INSERT INTO utxo_snapshots(network, block_hash, block_height, median_time) "
               "VALUES ($1, $2, $3, $4)"},
           pqxx::params{data.network, data.block_hash, data.block_height, data.median_time});

    auto stream_age = pqxx::stream_to::table(
            w,
            pqxx::table_path{"utxo_age"},
            {std::string_view{"network"}, std::string_view{"block_hash"}, std::string_view{"block_height"}, std::string_view{"median_time"}, std::string_view{"weeks_old"}, std::string_view{"utxo_count"}, std::string_view{"utxo_value"},
             std::string_view{"utxo_size"}, std::string_view{"utxo_count_percent"}, std::string_view{"utxo_value_percent"}, std::string_view{"utxo_size_percent"}});
    for (const auto& row : data.utxo_age_rows) {
        stream_age.write_values(data.network, data.block_hash, data.block_height, data.median_time, row.weeks_old, row.utxo_count,
                                row.utxo_value, row.utxo_size, row.count_percentage, row.value_percentage,
                                row.size_percentage);
    }
    stream_age.complete();

    auto stream_bal = pqxx::stream_to::table(
            w,
            pqxx::table_path{"utxo_balances"},
            {std::string_view{"network"}, std::string_view{"block_hash"}, std::string_view{"block_height"}, std::string_view{"median_time"}, std::string_view{"lower_bound"}, std::string_view{"upper_bound"}, std::string_view{"utxo_count"}, std::string_view{"utxo_value"},
             std::string_view{"utxo_size"}, std::string_view{"utxo_count_percent"}, std::string_view{"utxo_value_percent"}, std::string_view{"utxo_size_percent"}});
    for (const auto& row : data.utxo_balance_rows) {
        stream_bal.write_values(data.network, data.block_hash, data.block_height, data.median_time, row.lower_bound, row.upper_bound,
                                row.utxo_count, row.utxo_value, row.utxo_size, row.count_percentage,
                                row.value_percentage, row.size_percentage);
    }
    stream_bal.complete();

    auto stream_bal_usd = pqxx::stream_to::table(
            w,
            pqxx::table_path{"utxo_balances_usd"},
            {std::string_view{"network"}, std::string_view{"block_hash"}, std::string_view{"block_height"}, std::string_view{"median_time"}, std::string_view{"lower_bound"}, std::string_view{"upper_bound"}, std::string_view{"utxo_count"}, std::string_view{"utxo_value"},
             std::string_view{"utxo_size"}, std::string_view{"utxo_count_percent"}, std::string_view{"utxo_value_percent"}, std::string_view{"utxo_size_percent"}});
    for (const auto& row : data.utxo_balance_usd_rows) {
        stream_bal_usd.write_values(data.network, data.block_hash, data.block_height, data.median_time, row.lower_bound, row.upper_bound,
                                    row.utxo_count, row.utxo_value, row.utxo_size, row.count_percentage,
                                    row.value_percentage, row.size_percentage);
    }
    stream_bal_usd.complete();

    auto stream_percentiles = pqxx::stream_to::table(
            w,
            pqxx::table_path{"utxo_balances_percentiles"},
            {std::string_view{"network"}, std::string_view{"block_hash"}, std::string_view{"block_height"}, std::string_view{"median_time"}, std::string_view{"percentile"}, std::string_view{"utxo_value"}});
    for (const auto& row : data.utxo_percentiles) {
        stream_percentiles.write_values(data.network, data.block_hash, data.block_height, data.median_time, row.percentile, row.utxo_value);
    }
    stream_percentiles.complete();

    auto stream_usd_percentiles = pqxx::stream_to::table(
            w,
            pqxx::table_path{"utxo_balances_usd_percentiles"},
            {std::string_view{"network"}, std::string_view{"block_hash"}, std::string_view{"block_height"}, std::string_view{"median_time"}, std::string_view{"percentile"}, std::string_view{"utxo_value"}});
    for (const auto& row : data.utxo_usd_percentiles) {
        stream_usd_percentiles.write_values(data.network, data.block_hash, data.block_height, data.median_time, row.percentile, row.utxo_value);
    }
    stream_usd_percentiles.complete();

    auto stream_address_buckets = pqxx::stream_to::table(
            w,
            pqxx::table_path{"address_balance_buckets"},
            {std::string_view{"network"}, std::string_view{"block_hash"}, std::string_view{"block_height"}, std::string_view{"median_time"}, std::string_view{"lower_bound"}, std::string_view{"upper_bound"}, std::string_view{"address_count"}});
    for (const auto& row : data.address_balance_bucket_rows) {
        stream_address_buckets.write_values(data.network, data.block_hash, data.block_height, data.median_time, row.lower_bound, row.upper_bound, row.address_count);
    }
    stream_address_buckets.complete();

    auto stream_address_buckets_usd = pqxx::stream_to::table(
            w,
            pqxx::table_path{"address_balance_buckets_usd"},
            {std::string_view{"network"}, std::string_view{"block_hash"}, std::string_view{"block_height"}, std::string_view{"median_time"}, std::string_view{"lower_bound_cents"}, std::string_view{"upper_bound_cents"}, std::string_view{"address_count"}});
    for (const auto& row : data.address_balance_bucket_usd_rows) {
        stream_address_buckets_usd.write_values(data.network, data.block_hash, data.block_height, data.median_time, row.lower_bound_cents, row.upper_bound_cents, row.address_count);
    }
    stream_address_buckets_usd.complete();

    auto stream_script = pqxx::stream_to::table(
            w,
            pqxx::table_path{"utxo_script_types"},
            {std::string_view{"network"}, std::string_view{"block_hash"}, std::string_view{"block_height"}, std::string_view{"median_time"}, std::string_view{"script_type"}, std::string_view{"utxo_count"}, std::string_view{"utxo_value"},
             std::string_view{"utxo_size"}, std::string_view{"utxo_count_percent"}, std::string_view{"utxo_value_percent"}, std::string_view{"utxo_size_percent"}});
    for (const auto& row : data.script_type_rows) {
        stream_script.write_values(data.network, data.block_hash, data.block_height, data.median_time, row.script_type, row.utxo_count,
                                   row.utxo_value, row.utxo_size, row.count_percentage, row.value_percentage,
                                   row.size_percentage);
    }
    stream_script.complete();

    DeleteQueuedSnapshot(w, data.network, data.block_hash);
    w.commit();
}

void EnqueuePersistedSnapshot(std::string network, std::string block_hash)
{
    enterprise::DbWorkQueue::Instance().Enqueue(
        [network = std::move(network), block_hash = std::move(block_hash)](pqxx::connection& conn) {
            try {
                WriteQueuedSnapshot(conn, network, block_hash);
            } catch (const std::exception& e) {
                RecordQueuedSnapshotFailure(conn, network, block_hash, e.what());
                LogWarning("UtxoSetToSql: queued snapshot export failed for %s: %s",
                           block_hash.c_str(), e.what());
            }
        });
}

void PersistQueuedSnapshot(const UtxoSetExportData& data)
{
    EnsureSnapshotSchema();
    auto& conn = enterprise::PgConnection();
    pqxx::work w(conn);
    const std::string payload = SerializeExportData(data);
    w.exec(
        pqxx::zview{
            "INSERT INTO utxo_snapshot_export_queue(network, block_hash, block_height, payload) "
            "VALUES ($1, $2, $3, $4) "
            "ON CONFLICT (network, block_hash) DO UPDATE "
            "SET block_height = EXCLUDED.block_height, payload = EXCLUDED.payload, "
            "    queued_at = now(), last_attempted_at = NULL, attempt_count = 0, last_error = NULL"},
        pqxx::params{data.network, data.block_hash, data.block_height, payload});
    w.commit();
}

void DeleteQueuedSnapshot(std::string_view network, std::string_view block_hash)
{
    EnsureSnapshotSchema();
    auto& conn = enterprise::PgConnection();
    pqxx::work w(conn);
    DeleteQueuedSnapshot(w, network, block_hash);
    w.commit();
}

void EnqueueUtxoInsert(UtxoSetExportData&& data)
{
    const std::string network = data.network;
    const std::string block_hash = data.block_hash;
    PersistQueuedSnapshot(data);
    EnqueuePersistedSnapshot(network, block_hash);
}

} // namespace

bool ShouldExportUtxoSetToSql(const CBlockIndex& block_index)
{
    const int64_t height = block_index.nHeight;
    if (height < UTXO_EXPORT_INTERVAL) {
        return false;
    }
    if (height % UTXO_EXPORT_INTERVAL != 0) {
        return false;
    }
    if (SnapshotExists(block_index)) {
        const std::string block_hash = block_index.GetBlockHash().ToString();
        LogInfo("UtxoSetToSql: Block %d (%s) already exported or queued, skipping",
                height, block_hash.c_str());
        return false;
    }
    return true;
}

void RemoveUtxoSnapshotFromSql(const CBlockIndex& block_index)
{
    if (block_index.nHeight < UTXO_EXPORT_INTERVAL || block_index.nHeight % UTXO_EXPORT_INTERVAL != 0) {
        return;
    }

    EnsureSnapshotSchema();
    const std::string network = enterprise::ChainName();
    const std::string block_hash = block_index.GetBlockHash().GetHex();
    enterprise::DbWorkQueue::Instance().Enqueue([network, block_hash](pqxx::connection& conn) {
        enterprise::EnsureUtxoSnapshotSchema(conn, network);
        pqxx::work w(conn);
        w.exec(pqxx::zview{"DELETE FROM utxo_snapshots WHERE network = $1 AND block_hash = $2"},
               pqxx::params{network, block_hash});
        DeleteQueuedSnapshot(w, network, block_hash);
        w.commit();
    });
}

void SchedulePendingUtxoSnapshotRetries(interfaces::Chain& chain)
{
    EnsureSnapshotSchema();

    const std::string network = enterprise::ChainName();
    auto& conn = enterprise::PgConnection();
    pqxx::work w(conn);
    const auto pending_rows = w.exec(
        pqxx::zview{
            "SELECT block_hash, block_height FROM utxo_snapshot_export_queue "
            "WHERE network = $1 ORDER BY block_height"},
        pqxx::params{network});
    w.commit();

    if (pending_rows.empty()) {
        return;
    }

    for (const auto& row : pending_rows) {
        const std::string block_hash = row[0].as<std::string>();
        const int block_height = row[1].as<int>();
        bool in_active_chain{false};
        int active_height{-1};
        if (const auto queued_hash = uint256::FromHex(block_hash)) {
            in_active_chain = false;
            const bool found = chain.findBlock(*queued_hash, interfaces::FoundBlock().height(active_height).inActiveChain(in_active_chain));
            if (found && in_active_chain && active_height == block_height) {
                EnqueuePersistedSnapshot(network, block_hash);
                continue;
            }
        }
        DeleteQueuedSnapshot(network, block_hash);
        LogInfo("UtxoSetToSql: dropped stale queued snapshot for height %d (%s)",
                block_height, block_hash.c_str());
    }
}

UtxoSetToSql::UtxoSetToSql(CBlockIndex *block_index, const CBlock &block, CCoinsViewCache &view, script_verify_flags flags,
                           CCoinsViewCursor *cursor) {

    (void)block; // snapshot export derives data from the chainstate cursor
    (void)view; // kept for interface symmetry with the validation hook
    (void)flags; // currently unused; kept for interface symmetry

    int block_height = block_index->nHeight;
    if (!ShouldExportUtxoSetToSql(*block_index)) {
        return;
    }

    int64_t median_time_int = block_index->GetMedianTimePast();
    const std::string median_time = FormatUtcTimestamp(median_time_int);
    const std::string price_day = FormatISO8601Date(median_time_int);

    CAmount total_value = 0;
    double total_usd_value = 0;
    unsigned int total_count = 0;
    int64_t total_size = 0;

    std::map<unsigned int, std::tuple<CAmount, unsigned int, int64_t>> utxo_age_map;
    std::map<std::array<CAmount, 2>, std::tuple<CAmount, unsigned int, int64_t>> utxo_balance_map;
    std::map<std::array<int64_t, 2>, std::tuple<CAmount, unsigned int, int64_t>> utxo_balance_usd_map;
    std::map<std::string, std::tuple<CAmount, unsigned int, int64_t>> utxo_script_type_map;

    std::vector<double> utxo_balance;
    std::vector<double> utxo_balance_usd;

    std::unordered_map<std::string, CAmount> address_balances;

    auto& conn = enterprise::PgConnection();
    pqxx::work w1(conn);

    pqxx::result r = w1.exec(
        pqxx::zview{"SELECT price FROM prices WHERE day = $1::date"},
        pqxx::params{price_day});
    LogInfo("UtxoSetToSql: USD Price day: %s", price_day.c_str());
    w1.commit();
    if (r.empty()) {
        throw std::runtime_error(strprintf("No price data for UTC day %s", price_day));
    }
    double usd_price = r[0][0].as<double>();
    LogInfo("UtxoSetToSql: USD Price: %f", usd_price);

    LogInfo("Starting UTXO Set to SQL for block %d", block_height);
    while (cursor->Valid()) {
        Coin coin;
        cursor->GetValue(coin);
        if (coin.IsSpent()) {
            cursor->Next();
            continue;
        }

        // Calculate the coin age in weeks
        float BLOCKS_PER_WEEK = 1008.0;
        unsigned int coin_age_weeks = static_cast<unsigned int>(std::round((block_index->nHeight - coin.nHeight) / BLOCKS_PER_WEEK));
        CAmount coin_value = coin.out.nValue;

        int64_t output_size = GetSerializeSize(coin.out);
        static constexpr size_t PER_UTXO_OVERHEAD = sizeof(COutPoint) + sizeof(uint32_t) + sizeof(bool);
        int64_t utxo_size = output_size + PER_UTXO_OVERHEAD;

        std::vector <std::vector<unsigned char>> solutions_data;
        TxoutType which_type = Solver(coin.out.scriptPubKey, solutions_data);
        std::string script_type = GetTxnOutputType(which_type);

        total_value += coin_value;
        total_count += 1;
        total_size += utxo_size;

        auto &age_tuple = utxo_age_map[coin_age_weeks];
        std::get<0>(age_tuple) += coin_value;
        std::get<1>(age_tuple) += 1;
        std::get<2>(age_tuple) += utxo_size;

        CAmount lowerBound = std::pow(10, static_cast<CAmount>(std::log10(coin_value)));
        CAmount upperBound = lowerBound * 10;
        auto &balance_tuple = utxo_balance_map[{lowerBound, upperBound}];
        std::get<0>(balance_tuple) += coin_value;
        std::get<1>(balance_tuple) += 1;
        std::get<2>(balance_tuple) += utxo_size;

        double usd_value = static_cast<double>(coin_value) / 100000000.0 * usd_price;
        total_usd_value += usd_value;

        utxo_balance.push_back(static_cast<double>(coin_value));
        utxo_balance_usd.push_back(usd_value);

        int64_t lowerBoundUsd = std::pow(10, static_cast<int64_t>(std::log10(usd_value)));
        int64_t upperBoundUsd = lowerBoundUsd * 10;
        auto &balance_usd_tuple = utxo_balance_usd_map[{lowerBoundUsd, upperBoundUsd}];
        std::get<0>(balance_usd_tuple) += usd_value;
        std::get<1>(balance_usd_tuple) += 1;
        std::get<2>(balance_usd_tuple) += utxo_size;

        auto &script_type_tuple = utxo_script_type_map[script_type];
        std::get<0>(script_type_tuple) += coin_value;
        std::get<1>(script_type_tuple) += 1;
        std::get<2>(script_type_tuple) += utxo_size;

        CTxDestination address;
        if (ExtractDestination(coin.out.scriptPubKey, address)) {
            address_balances[EncodeDestination(address)] += coin_value;
        }

        cursor->Next();
    }
    LogInfo("Completed UTXO Set to SQL for block %d", block_height);

    UtxoSetExportData export_data;
    export_data.network = enterprise::ChainName();
    export_data.block_hash = block_index->GetBlockHash().GetHex();
    export_data.block_height = block_height;
    export_data.median_time = median_time;

    for (const auto &entry : utxo_age_map) {
        unsigned int weeks_old = entry.first;
        CAmount utxo_value = std::get<0>(entry.second);
        unsigned int utxo_count = std::get<1>(entry.second);
        int64_t utxo_size = std::get<2>(entry.second);

        double value_percentage = std::round(static_cast<double>(utxo_value) / total_value * 10000.0) / 100.0;
        double count_percentage = std::round(static_cast<double>(utxo_count) / total_count * 10000.0) / 100.0;
        double size_percentage = std::round(static_cast<double>(utxo_size) / total_size * 10000.0) / 100.0;

        export_data.utxo_age_rows.push_back({weeks_old, utxo_value, utxo_count, utxo_size, count_percentage, value_percentage, size_percentage});
    };

    for (const auto &entry : utxo_balance_map) {
        CAmount lower_bound = std::get<0>(entry.first);
        CAmount upper_bound = std::get<1>(entry.first);
        CAmount utxo_value = std::get<0>(entry.second);
        unsigned int utxo_count = std::get<1>(entry.second);
        int64_t utxo_size = std::get<2>(entry.second);

        double value_percentage = std::round(static_cast<double>(utxo_value) / total_value * 10000.0) / 100.0;
        double count_percentage = std::round(static_cast<double>(utxo_count) / total_count * 10000.0) / 100.0;
        double size_percentage = std::round(static_cast<double>(utxo_size) / total_size * 10000.0) / 100.0;

        export_data.utxo_balance_rows.push_back({static_cast<int64_t>(lower_bound), static_cast<int64_t>(upper_bound), utxo_value, utxo_count, utxo_size, count_percentage, value_percentage, size_percentage});
    }

    for (const auto &entry : utxo_balance_usd_map) {
        int64_t lower_bound = std::get<0>(entry.first);
        int64_t upper_bound = std::get<1>(entry.first);
        int64_t utxo_value = std::get<0>(entry.second);
        unsigned int utxo_count = std::get<1>(entry.second);
        int64_t utxo_size = std::get<2>(entry.second);

        double value_percentage = std::round(static_cast<double>(utxo_value) / total_usd_value * 10000.0) / 100.0;
        double count_percentage = std::round(static_cast<double>(utxo_count) / total_count * 10000.0) / 100.0;
        double size_percentage = std::round(static_cast<double>(utxo_size) / total_size * 10000.0) / 100.0;

        export_data.utxo_balance_usd_rows.push_back({lower_bound, upper_bound, utxo_value, utxo_count, utxo_size, count_percentage, value_percentage, size_percentage});
    }

    std::map<int, double> percentiles = calculatePercentiles(utxo_balance);
    std::map<int, double> usd_percentiles = calculatePercentiles(utxo_balance_usd);
    LogInfo("UtxoSetToSql: Percentiles calculated");

    for (const auto &entry : percentiles) {
        int percentile = entry.first;
        double utxo_value = entry.second;
        export_data.utxo_percentiles.push_back({percentile, utxo_value});
    }

    for (const auto &entry : usd_percentiles) {
        int percentile = entry.first;
        double utxo_value = entry.second;
        export_data.utxo_usd_percentiles.push_back({percentile, utxo_value});
    }

    static constexpr std::array<std::array<CAmount, 2>, 8> ADDRESS_BALANCE_BUCKETS{{
        {0, COIN / 100},
        {COIN / 100, COIN / 10},
        {COIN / 10, COIN},
        {COIN, 10 * COIN},
        {10 * COIN, 100 * COIN},
        {100 * COIN, 1000 * COIN},
        {1000 * COIN, 10000 * COIN},
        {10000 * COIN, std::numeric_limits<CAmount>::max()},
    }};
    std::array<uint64_t, ADDRESS_BALANCE_BUCKETS.size()> address_bucket_counts{};
    std::array<uint64_t, ADDRESS_BALANCE_BUCKETS.size()> address_bucket_counts_usd{};
    std::array<int64_t, ADDRESS_BALANCE_BUCKETS.size()> address_bucket_lower_usd_cents{};
    std::array<int64_t, ADDRESS_BALANCE_BUCKETS.size()> address_bucket_upper_usd_cents{};
    for (size_t i = 0; i < ADDRESS_BALANCE_BUCKETS.size(); ++i) {
        const CAmount lower = ADDRESS_BALANCE_BUCKETS[i][0];
        const CAmount upper = ADDRESS_BALANCE_BUCKETS[i][1];
        address_bucket_lower_usd_cents[i] = static_cast<int64_t>(
            std::llround(static_cast<double>(lower) / COIN * usd_price * 100.0));
        if (upper == std::numeric_limits<CAmount>::max()) {
            address_bucket_upper_usd_cents[i] = std::numeric_limits<int64_t>::max();
        } else {
            address_bucket_upper_usd_cents[i] = static_cast<int64_t>(
                std::llround(static_cast<double>(upper) / COIN * usd_price * 100.0));
        }
    }
    for (const auto& entry : address_balances) {
        const CAmount balance = entry.second;
        for (size_t i = 0; i < ADDRESS_BALANCE_BUCKETS.size(); ++i) {
            const CAmount lower = ADDRESS_BALANCE_BUCKETS[i][0];
            const CAmount upper = ADDRESS_BALANCE_BUCKETS[i][1];
            if (balance >= lower && balance < upper) {
                address_bucket_counts[i] += 1;
                break;
            }
        }
        const int64_t usd_cents = static_cast<int64_t>(
            std::llround(static_cast<double>(balance) / COIN * usd_price * 100.0));
        for (size_t i = 0; i < ADDRESS_BALANCE_BUCKETS.size(); ++i) {
            if (usd_cents >= address_bucket_lower_usd_cents[i] && usd_cents < address_bucket_upper_usd_cents[i]) {
                address_bucket_counts_usd[i] += 1;
                break;
            }
        }
    }
    std::map<std::pair<int64_t, int64_t>, uint64_t> merged_usd_buckets;
    for (size_t i = 0; i < ADDRESS_BALANCE_BUCKETS.size(); ++i) {
        export_data.address_balance_bucket_rows.push_back({
            ADDRESS_BALANCE_BUCKETS[i][0],
            ADDRESS_BALANCE_BUCKETS[i][1],
            address_bucket_counts[i],
        });
        merged_usd_buckets[{address_bucket_lower_usd_cents[i], address_bucket_upper_usd_cents[i]}] +=
            address_bucket_counts_usd[i];
    }
    for (const auto& [bounds, count] : merged_usd_buckets) {
        export_data.address_balance_bucket_usd_rows.push_back({
            bounds.first,
            bounds.second,
            count,
        });
    }

    for (const auto &entry : utxo_script_type_map) {
        std::string script_type = entry.first;
        CAmount utxo_value = std::get<0>(entry.second);
        unsigned int utxo_count = std::get<1>(entry.second);
        int64_t utxo_size = std::get<2>(entry.second);

        double value_percentage = std::round(static_cast<double>(utxo_value) / total_value * 10000.0) / 100.0;
        double count_percentage = std::round(static_cast<double>(utxo_count) / total_count * 10000.0) / 100.0;
        double size_percentage = std::round(static_cast<double>(utxo_size) / total_size * 10000.0) / 100.0;

        export_data.script_type_rows.push_back({script_type, utxo_value, utxo_count, utxo_size, count_percentage, value_percentage, size_percentage});
    };

    EnqueueUtxoInsert(std::move(export_data));

    LogInfo("UtxoSetToSql: Block %d queued, %d UTXOs, %d bytes", block_height, total_count, total_size);

}
