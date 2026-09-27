#include <enterprise/utxo_set_to_sql.h>

#include <chain.h>
#include <coins.h>
#include <consensus/amount.h>
#include <crypto/siphash.h>
#include <enterprise/pg_config.h>
#include <enterprise/pqxx_compat.h>
#include <logging.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <script/solver.h>
#include <serialize.h>
#include <uint256.h>
#include <util/log.h>
#include <util/time.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

constexpr size_t PER_UTXO_OVERHEAD = sizeof(COutPoint) + sizeof(uint32_t) + sizeof(bool);
constexpr double BLOCKS_PER_WEEK = 1008.0;
constexpr int ADDRESS_BINS_PER_DECADE = 100;
// Fixed keys keep script hashes, and therefore any collision, reproducible across scans.
constexpr uint64_t SCRIPT_KEY_K0{0x656e746572707269};
constexpr uint64_t SCRIPT_KEY_K1{0x7365626974636f69};

using AgeBucket = std::tuple<CAmount, uint64_t, int64_t>;
using BalanceBucket = std::tuple<CAmount, uint64_t, int64_t>;
using UsdBalanceBucket = std::tuple<double, uint64_t, int64_t>;

pqxx::connection ConnectPg()
{
    LogInfo("utxostats: opening PostgreSQL connection");
    pqxx::connection connection{enterprise::PgConnectionString()};
    LogInfo("utxostats: PostgreSQL connection opened");
    return connection;
}

void PinPublicSearchPath(pqxx::work& w)
{
    // The enterprise conninfo pins search_path to pg_catalog,public. Unqualified
    // CREATE TABLE then tries to write pg_catalog. Force public for this transaction.
    w.exec("SELECT pg_catalog.set_config('search_path', 'public', true)");
}

void EnsureUtxoStatsTables(pqxx::work& w)
{
    PinPublicSearchPath(w);
    w.exec(R"sql(
        CREATE TABLE IF NOT EXISTS utxo_age (
            id SERIAL PRIMARY KEY,
            block_height BIGINT,
            median_time TIMESTAMP,
            weeks_old BIGINT,
            utxo_count BIGINT,
            utxo_value BIGINT,
            utxo_size BIGINT,
            utxo_count_percent FLOAT,
            utxo_value_percent FLOAT,
            utxo_size_percent FLOAT
        )
    )sql");
    w.exec(R"sql(
        CREATE UNIQUE INDEX IF NOT EXISTS utxo_age_block_height_weeks_old_idx
            ON utxo_age (block_height, weeks_old)
    )sql");
    w.exec(R"sql(
        CREATE TABLE IF NOT EXISTS utxo_balances (
            id SERIAL PRIMARY KEY,
            block_height BIGINT,
            median_time TIMESTAMP,
            lower_bound BIGINT,
            upper_bound BIGINT,
            utxo_count BIGINT,
            utxo_value BIGINT,
            utxo_size BIGINT,
            utxo_count_percent FLOAT,
            utxo_value_percent FLOAT,
            utxo_size_percent FLOAT
        )
    )sql");
    w.exec(R"sql(
        CREATE UNIQUE INDEX IF NOT EXISTS utxo_balances_block_height_balance_min_balance_max_idx
            ON utxo_balances (block_height, lower_bound, upper_bound)
    )sql");
    w.exec(R"sql(
        CREATE TABLE IF NOT EXISTS utxo_balances_usd (
            id SERIAL PRIMARY KEY,
            block_height BIGINT,
            median_time TIMESTAMP,
            lower_bound BIGINT,
            upper_bound BIGINT,
            utxo_count BIGINT,
            utxo_value BIGINT,
            utxo_size BIGINT,
            utxo_count_percent FLOAT,
            utxo_value_percent FLOAT,
            utxo_size_percent FLOAT
        )
    )sql");
    w.exec(R"sql(
        CREATE UNIQUE INDEX IF NOT EXISTS utxo_balances_usd_block_height_balance_min_balance_max_idx
            ON utxo_balances_usd (block_height, lower_bound, upper_bound)
    )sql");
    w.exec(R"sql(
        CREATE TABLE IF NOT EXISTS utxo_script_types (
            id SERIAL PRIMARY KEY,
            block_height BIGINT,
            median_time TIMESTAMP,
            script_type TEXT,
            utxo_count BIGINT,
            utxo_value BIGINT,
            utxo_size BIGINT,
            utxo_count_percent FLOAT,
            utxo_value_percent FLOAT,
            utxo_size_percent FLOAT
        )
    )sql");
    w.exec(R"sql(
        CREATE UNIQUE INDEX IF NOT EXISTS utxo_types_block_height_type_idx
            ON utxo_script_types (block_height, script_type)
    )sql");
    w.exec(R"sql(
        CREATE TABLE IF NOT EXISTS address_balance_histogram (
            block_height BIGINT NOT NULL,
            block_hash TEXT NOT NULL,
            median_time TIMESTAMP NOT NULL,
            lower_bound BIGINT NOT NULL,
            upper_bound BIGINT NOT NULL,
            address_count BIGINT NOT NULL,
            balance BIGINT NOT NULL,
            PRIMARY KEY (block_height, lower_bound)
        )
    )sql");
}

// Heights with both a utxo_age snapshot and an address-balance histogram.
// Heights that only have utxo_age rows are rescanned to fill the histogram.
constexpr const char* EXPORTED_HEIGHTS_SQL{R"sql(
    SELECT block_height FROM utxo_age
    INTERSECT
    SELECT block_height FROM address_balance_histogram
)sql"};

void InsertAddressBalanceHistogram(pqxx::connection& c, pqxx::work& w, const enterprise::UtxoSetExportStats& stats,
    const std::vector<enterprise::AddressBalanceBin>& bins)
{
    c.prepare("AddressBalanceInsert", R"sql(
        INSERT INTO address_balance_histogram (
            block_height, block_hash, median_time, lower_bound, upper_bound, address_count, balance
        ) VALUES ($1, $2, $3, $4, $5, $6, $7)
        ON CONFLICT (block_height, lower_bound) DO NOTHING
    )sql");
    const std::string block_hash{stats.block_hash.GetHex()};
    for (const auto& bin : bins) {
        enterprise::ExecPrepared(w,
            "AddressBalanceInsert",
            stats.height,
            block_hash,
            stats.median_time,
            bin.lower_bound,
            bin.upper_bound,
            static_cast<int64_t>(bin.address_count),
            bin.balance);
    }
}

std::string FormatMedianTimeUtc(int64_t median_time)
{
    const std::time_t t{static_cast<std::time_t>(median_time)};
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[32]{};
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}

std::optional<double> LookupUsdPrice(pqxx::connection& c, const std::string& median_time)
{
    pqxx::work w{c};
    PinPublicSearchPath(w);
    c.prepare("UtxoStatsPrice", R"sql(
        SELECT price
        FROM prices
        WHERE day::date = $1::timestamp::date
        ORDER BY day
        LIMIT 1
    )sql");
    const auto res = enterprise::ExecPrepared(w, "UtxoStatsPrice", median_time);
    w.commit();
    if (res.empty() || res[0][0].is_null()) {
        return std::nullopt;
    }
    return res[0][0].as<double>();
}

} // namespace

namespace enterprise {

unsigned int CoinAgeWeeks(int snapshot_height, int coin_height)
{
    const int age_blocks{snapshot_height - coin_height};
    if (age_blocks <= 0) return 0;
    return static_cast<unsigned int>(std::round(static_cast<double>(age_blocks) / BLOCKS_PER_WEEK));
}

std::pair<CAmount, CAmount> ValueMagnitudeBounds(CAmount value)
{
    if (value <= 0) {
        return {0, 1};
    }
    const double logv{std::log10(static_cast<double>(value))};
    if (!std::isfinite(logv) || logv < 0) {
        return {1, 10};
    }
    CAmount lower{static_cast<CAmount>(std::pow(10.0, static_cast<int>(logv)))};
    if (lower <= 0) lower = 1;
    return {lower, lower * 10};
}

double PercentRounded(double part, double total)
{
    if (total == 0.0) return 0.0;
    return std::round(part / total * 10000.0) / 100.0;
}

uint64_t ScriptBalanceMap::Key(const CScript& script)
{
    const uint64_t key{CSipHasher(SCRIPT_KEY_K0, SCRIPT_KEY_K1).Write(std::span{script.data(), script.size()}).Finalize()};
    return key == EMPTY ? 1 : key;
}

void ScriptBalanceMap::Add(uint64_t key, CAmount value)
{
    if ((m_size + 1) * 2 > m_slots.size()) Grow();
    const size_t mask{m_slots.size() - 1};
    for (size_t i{key & mask};; i = (i + 1) & mask) {
        Slot& slot{m_slots[i]};
        if (slot.key == key) {
            slot.balance += value;
            return;
        }
        if (slot.key == EMPTY) {
            slot.key = key;
            slot.balance = value;
            ++m_size;
            return;
        }
    }
}

void ScriptBalanceMap::Grow()
{
    std::vector<Slot> old;
    old.swap(m_slots);
    m_slots.assign(old.empty() ? 1024 : old.size() * 2, Slot{});
    m_size = 0;
    for (const Slot& slot : old) {
        if (slot.key != EMPTY) Add(slot.key, slot.balance);
    }
}

const std::vector<CAmount>& AddressBalanceBinEdges()
{
    static const std::vector<CAmount> edges{[] {
        std::vector<CAmount> out;
        CAmount power{1};
        // 10^16 satoshis is past MAX_MONEY, so the last bin is open-ended in practice.
        for (int decade{0}; decade < 16; ++decade, power *= 10) {
            out.push_back(power);
            for (int step{1}; step < ADDRESS_BINS_PER_DECADE; ++step) {
                const long double edge{std::pow(10.0L, decade + static_cast<long double>(step) / ADDRESS_BINS_PER_DECADE)};
                const CAmount lower{static_cast<CAmount>(std::ceil(edge))};
                if (lower > out.back() && lower < power * 10) out.push_back(lower);
            }
        }
        out.push_back(power);
        return out;
    }()};
    return edges;
}

size_t AddressBalanceBinIndex(CAmount balance)
{
    const auto& edges{AddressBalanceBinEdges()};
    const auto it{std::upper_bound(edges.begin(), edges.end() - 1, std::max<CAmount>(balance, 1))};
    return static_cast<size_t>(it - edges.begin()) - 1;
}

std::vector<AddressBalanceBin> BuildAddressBalanceHistogram(const ScriptBalanceMap& balances)
{
    const auto& edges{AddressBalanceBinEdges()};
    std::vector<AddressBalanceBin> bins(edges.size() - 1);
    for (size_t i{0}; i < bins.size(); ++i) {
        bins[i].lower_bound = edges[i];
        bins[i].upper_bound = edges[i + 1];
    }
    balances.ForEach([&](uint64_t, CAmount balance) {
        if (balance <= 0) return;
        auto& bin{bins[AddressBalanceBinIndex(balance)]};
        bin.address_count += 1;
        bin.balance += balance;
    });
    std::erase_if(bins, [](const AddressBalanceBin& bin) { return bin.address_count == 0; });
    return bins;
}

bool ShouldExportUtxoSet(int height, int64_t db_max_height, bool height_already_exported, bool force)
{
    if (height <= 0) return false;
    if (height_already_exported && !force) return false;
    if (force) return true;
    // Aligned heights are historical snapshots. Export them even when a newer
    // unaligned catch-up row already exists, so an IBD node can fill gaps.
    if (height > 0 && height % UTXO_EXPORT_INTERVAL == 0) return true;
    if (db_max_height < 0) return true;
    return (height / UTXO_EXPORT_INTERVAL) > (db_max_height / UTXO_EXPORT_INTERVAL);
}

int64_t MaxExportedUtxoHeight()
{
    try {
        pqxx::connection c{ConnectPg()};
        pqxx::work w{c};
        EnsureUtxoStatsTables(w);
        const auto res = w.exec(std::string{"SELECT COALESCE(MAX(block_height), -1) FROM ("} + EXPORTED_HEIGHTS_SQL + ") exported");
        w.commit();
        if (res.empty() || res[0][0].is_null()) return -1;
        return res[0][0].as<int64_t>();
    } catch (const std::exception& e) {
        LogWarning("utxostats: failed to read max utxo_age height: %s", e.what());
        return -1;
    }
}

bool UtxoHeightExported(int height)
{
    try {
        pqxx::connection c{ConnectPg()};
        pqxx::work w{c};
        EnsureUtxoStatsTables(w);
        c.prepare("UtxoHeightExists", R"sql(
            SELECT 1 FROM utxo_age WHERE block_height = $1
            INTERSECT
            SELECT 1 FROM address_balance_histogram WHERE block_height = $1
        )sql");
        const auto res = enterprise::ExecPrepared(w, "UtxoHeightExists", height);
        w.commit();
        return !res.empty();
    } catch (const std::exception& e) {
        LogWarning("utxostats: failed to check utxo_age height %d: %s", height, e.what());
        return false;
    }
}

void LoadExportedUtxoHeights(std::unordered_set<int>& heights, int64_t& max_height)
{
    pqxx::connection c{ConnectPg()};
    pqxx::work w{c};
    EnsureUtxoStatsTables(w);
    const auto res = w.exec(EXPORTED_HEIGHTS_SQL);
    w.commit();
    heights.clear();
    max_height = -1;
    for (const auto& row : res) {
        if (row[0].is_null()) continue;
        const int64_t height{row[0].as<int64_t>()};
        heights.insert(static_cast<int>(height));
        if (height > max_height) max_height = height;
    }
}

UtxoAgeHistogram ScanUtxoAgeFromCursor(
    const CBlockIndex& block_index,
    CCoinsViewCursor& cursor,
    const std::function<void()>& interruption_point)
{
    UtxoAgeHistogram histogram;
    histogram.stats.height = block_index.nHeight;
    histogram.stats.block_hash = block_index.GetBlockHash();
    histogram.stats.median_time = FormatMedianTimeUtc(block_index.GetMedianTimePast());

    std::map<unsigned int, AgeBucket> age_map;
    ScriptBalanceMap script_balances;
    CAmount total_value{0};
    uint64_t total_count{0};
    int64_t total_size{0};

    LogInfo("utxostats: scanning UTXO set at height %d", histogram.stats.height);
    const auto started{NodeClock::now()};
    uint64_t scanned{0};
    while (cursor.Valid()) {
        if (interruption_point && (scanned % 10000 == 0)) {
            interruption_point();
        }
        Coin coin;
        if (!cursor.GetValue(coin)) {
            throw std::runtime_error("unable to read UTXO set value");
        }
        if (coin.IsSpent()) {
            cursor.Next();
            continue;
        }

        const unsigned int weeks_old{CoinAgeWeeks(histogram.stats.height, static_cast<int>(coin.nHeight))};
        const CAmount coin_value{coin.out.nValue};
        const int64_t utxo_size{static_cast<int64_t>(GetSerializeSize(coin.out)) + static_cast<int64_t>(PER_UTXO_OVERHEAD)};

        total_value += coin_value;
        total_count += 1;
        total_size += utxo_size;

        auto& age_tuple = age_map[weeks_old];
        std::get<0>(age_tuple) += coin_value;
        std::get<1>(age_tuple) += 1;
        std::get<2>(age_tuple) += utxo_size;

        script_balances.Add(ScriptBalanceMap::Key(coin.out.scriptPubKey), coin_value);

        ++scanned;
        if (scanned % 5000000 == 0) {
            LogInfo("utxostats: scanned %u coins at height %d", scanned, histogram.stats.height);
        }
        cursor.Next();
    }

    histogram.stats.utxo_count = total_count;
    histogram.stats.total_value = total_value;
    histogram.stats.total_size = total_size;
    histogram.rows.reserve(age_map.size());
    for (const auto& [weeks_old, bucket] : age_map) {
        histogram.rows.push_back({weeks_old, std::get<0>(bucket), std::get<1>(bucket), std::get<2>(bucket)});
    }
    histogram.stats.age_buckets = histogram.rows.size();
    histogram.address_bins = BuildAddressBalanceHistogram(script_balances);
    histogram.stats.funded_scripts = script_balances.size();
    histogram.stats.address_bins = histogram.address_bins.size();

    const auto elapsed{Ticks<std::chrono::milliseconds>(NodeClock::now() - started) / 1000.0};
    LogInfo("utxostats: scanned %u coins (%u funded scripts) at height %d in %.1f s",
        total_count, histogram.stats.funded_scripts, histogram.stats.height, elapsed);
    return histogram;
}

void WriteUtxoAgeHistogram(const UtxoAgeHistogram& histogram)
{
    const auto& stats{histogram.stats};
    pqxx::connection c{ConnectPg()};
    pqxx::work w{c};
    EnsureUtxoStatsTables(w);
    c.prepare("UtxoAgeInsert", R"sql(
        INSERT INTO utxo_age (
            block_height, median_time, weeks_old, utxo_count, utxo_value, utxo_size,
            utxo_count_percent, utxo_value_percent, utxo_size_percent
        ) VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9)
        ON CONFLICT (block_height, weeks_old) DO NOTHING
    )sql");
    for (const auto& row : histogram.rows) {
        enterprise::ExecPrepared(w,
            "UtxoAgeInsert",
            stats.height,
            stats.median_time,
            static_cast<int64_t>(row.weeks_old),
            static_cast<int64_t>(row.utxo_count),
            row.utxo_value,
            row.utxo_size,
            PercentRounded(static_cast<double>(row.utxo_count), static_cast<double>(stats.utxo_count)),
            PercentRounded(static_cast<double>(row.utxo_value), static_cast<double>(stats.total_value)),
            PercentRounded(static_cast<double>(row.utxo_size), static_cast<double>(stats.total_size)));
    }
    InsertAddressBalanceHistogram(c, w, stats, histogram.address_bins);
    w.commit();
    LogInfo("utxostats: wrote %u age buckets and %u address bins for height %d",
        stats.age_buckets, stats.address_bins, stats.height);
}

UtxoSetExportStats ExportUtxoSetFromCursor(
    const CBlockIndex& block_index,
    CCoinsViewCursor& cursor,
    const std::function<void()>& interruption_point,
    bool include_optional_tables)
{
    UtxoSetExportStats stats;
    stats.height = block_index.nHeight;
    stats.block_hash = block_index.GetBlockHash();
    stats.median_time = FormatMedianTimeUtc(block_index.GetMedianTimePast());

    if (UtxoHeightExported(stats.height)) {
        stats.skipped = true;
        stats.skip_reason = "already exported";
        return stats;
    }

    if (!include_optional_tables) {
        UtxoAgeHistogram histogram{ScanUtxoAgeFromCursor(block_index, cursor, interruption_point)};
        WriteUtxoAgeHistogram(histogram);
        return histogram.stats;
    }

    std::map<unsigned int, AgeBucket> age_map;
    std::map<std::array<CAmount, 2>, BalanceBucket> balance_map;
    std::map<std::array<int64_t, 2>, UsdBalanceBucket> usd_balance_map;
    std::map<std::string, AgeBucket> script_type_map;
    ScriptBalanceMap script_balances;

    CAmount total_value{0};
    uint64_t total_count{0};
    int64_t total_size{0};
    double total_usd_value{0.0};

    pqxx::connection c{ConnectPg()};
    std::optional<double> usd_price;
    try {
        usd_price = LookupUsdPrice(c, stats.median_time);
    } catch (const std::exception& e) {
        LogWarning("utxostats: price lookup failed for %s: %s", stats.median_time, e.what());
    }
    if (usd_price) {
        LogInfo("utxostats: USD price for %s is %f", stats.median_time, *usd_price);
    } else {
        LogInfo("utxostats: no USD price for %s; skipping USD tables", stats.median_time);
    }

    LogInfo("utxostats: scanning UTXO set at height %d", stats.height);
    const auto started{NodeClock::now()};
    uint64_t scanned{0};
    while (cursor.Valid()) {
        if (interruption_point && (scanned % 10000 == 0)) {
            interruption_point();
        }
        Coin coin;
        if (!cursor.GetValue(coin)) {
            throw std::runtime_error("unable to read UTXO set value");
        }
        if (coin.IsSpent()) {
            cursor.Next();
            continue;
        }

        const unsigned int weeks_old{CoinAgeWeeks(stats.height, static_cast<int>(coin.nHeight))};
        const CAmount coin_value{coin.out.nValue};
        const int64_t utxo_size{static_cast<int64_t>(GetSerializeSize(coin.out)) + static_cast<int64_t>(PER_UTXO_OVERHEAD)};

        total_value += coin_value;
        total_count += 1;
        total_size += utxo_size;

        auto& age_tuple = age_map[weeks_old];
        std::get<0>(age_tuple) += coin_value;
        std::get<1>(age_tuple) += 1;
        std::get<2>(age_tuple) += utxo_size;

        script_balances.Add(ScriptBalanceMap::Key(coin.out.scriptPubKey), coin_value);

        if (include_optional_tables) {
            const auto bounds{ValueMagnitudeBounds(coin_value)};
            auto& balance_tuple = balance_map[{bounds.first, bounds.second}];
            std::get<0>(balance_tuple) += coin_value;
            std::get<1>(balance_tuple) += 1;
            std::get<2>(balance_tuple) += utxo_size;

            std::vector<std::vector<unsigned char>> solutions;
            const TxoutType which_type{Solver(coin.out.scriptPubKey, solutions)};
            auto& script_tuple = script_type_map[GetTxnOutputType(which_type)];
            std::get<0>(script_tuple) += coin_value;
            std::get<1>(script_tuple) += 1;
            std::get<2>(script_tuple) += utxo_size;

            if (usd_price) {
                const double usd_value{static_cast<double>(coin_value) / static_cast<double>(COIN) * *usd_price};
                total_usd_value += usd_value;
                const auto usd_bounds{ValueMagnitudeBounds(usd_value > 0 ? static_cast<CAmount>(usd_value) : 0)};
                auto& usd_tuple = usd_balance_map[{usd_bounds.first, usd_bounds.second}];
                std::get<0>(usd_tuple) += usd_value;
                std::get<1>(usd_tuple) += 1;
                std::get<2>(usd_tuple) += utxo_size;
            }
        }

        ++scanned;
        if (scanned % 5000000 == 0) {
            LogInfo("utxostats: scanned %u coins at height %d", scanned, stats.height);
        }
        cursor.Next();
    }

    stats.utxo_count = total_count;
    stats.total_value = total_value;
    stats.total_size = total_size;
    stats.age_buckets = age_map.size();
    const std::vector<AddressBalanceBin> address_bins{BuildAddressBalanceHistogram(script_balances)};
    stats.funded_scripts = script_balances.size();
    stats.address_bins = address_bins.size();

    const auto elapsed{Ticks<std::chrono::milliseconds>(NodeClock::now() - started) / 1000.0};
    LogInfo("utxostats: scanned %u coins (%u funded scripts) at height %d in %.1f s",
        total_count, stats.funded_scripts, stats.height, elapsed);

    pqxx::work w{c};
    EnsureUtxoStatsTables(w);

    c.prepare("UtxoAgeInsert", R"sql(
        INSERT INTO utxo_age (
            block_height, median_time, weeks_old, utxo_count, utxo_value, utxo_size,
            utxo_count_percent, utxo_value_percent, utxo_size_percent
        ) VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9)
        ON CONFLICT (block_height, weeks_old) DO NOTHING
    )sql");
    for (const auto& [weeks_old, bucket] : age_map) {
        enterprise::ExecPrepared(w, 
            "UtxoAgeInsert",
            stats.height,
            stats.median_time,
            static_cast<int64_t>(weeks_old),
            static_cast<int64_t>(std::get<1>(bucket)),
            std::get<0>(bucket),
            std::get<2>(bucket),
            PercentRounded(static_cast<double>(std::get<1>(bucket)), static_cast<double>(total_count)),
            PercentRounded(static_cast<double>(std::get<0>(bucket)), static_cast<double>(total_value)),
            PercentRounded(static_cast<double>(std::get<2>(bucket)), static_cast<double>(total_size)));
    }

    InsertAddressBalanceHistogram(c, w, stats, address_bins);

    if (include_optional_tables) {
        stats.wrote_optional = true;
        c.prepare("UtxoBalancesInsert", R"sql(
            INSERT INTO utxo_balances (
                block_height, median_time, lower_bound, upper_bound, utxo_count, utxo_value, utxo_size,
                utxo_count_percent, utxo_value_percent, utxo_size_percent
            ) VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10)
            ON CONFLICT (block_height, lower_bound, upper_bound) DO NOTHING
        )sql");
        for (const auto& [bounds, bucket] : balance_map) {
            enterprise::ExecPrepared(w, 
                "UtxoBalancesInsert",
                stats.height,
                stats.median_time,
                bounds[0],
                bounds[1],
                static_cast<int64_t>(std::get<1>(bucket)),
                std::get<0>(bucket),
                std::get<2>(bucket),
                PercentRounded(static_cast<double>(std::get<1>(bucket)), static_cast<double>(total_count)),
                PercentRounded(static_cast<double>(std::get<0>(bucket)), static_cast<double>(total_value)),
                PercentRounded(static_cast<double>(std::get<2>(bucket)), static_cast<double>(total_size)));
        }

        c.prepare("UtxoScriptInsert", R"sql(
            INSERT INTO utxo_script_types (
                block_height, median_time, script_type, utxo_count, utxo_value, utxo_size,
                utxo_count_percent, utxo_value_percent, utxo_size_percent
            ) VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9)
            ON CONFLICT (block_height, script_type) DO NOTHING
        )sql");
        for (const auto& [script_type, bucket] : script_type_map) {
            enterprise::ExecPrepared(w, 
                "UtxoScriptInsert",
                stats.height,
                stats.median_time,
                script_type,
                static_cast<int64_t>(std::get<1>(bucket)),
                std::get<0>(bucket),
                std::get<2>(bucket),
                PercentRounded(static_cast<double>(std::get<1>(bucket)), static_cast<double>(total_count)),
                PercentRounded(static_cast<double>(std::get<0>(bucket)), static_cast<double>(total_value)),
                PercentRounded(static_cast<double>(std::get<2>(bucket)), static_cast<double>(total_size)));
        }

        if (usd_price && total_usd_value > 0) {
            stats.wrote_usd = true;
            c.prepare("UtxoUsdInsert", R"sql(
                INSERT INTO utxo_balances_usd (
                    block_height, median_time, lower_bound, upper_bound, utxo_count, utxo_value, utxo_size,
                    utxo_count_percent, utxo_value_percent, utxo_size_percent
                ) VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10)
                ON CONFLICT (block_height, lower_bound, upper_bound) DO NOTHING
            )sql");
            for (const auto& [bounds, bucket] : usd_balance_map) {
                enterprise::ExecPrepared(w, 
                    "UtxoUsdInsert",
                    stats.height,
                    stats.median_time,
                    bounds[0],
                    bounds[1],
                    static_cast<int64_t>(std::get<1>(bucket)),
                    std::get<0>(bucket),
                    std::get<2>(bucket),
                    PercentRounded(static_cast<double>(std::get<1>(bucket)), static_cast<double>(total_count)),
                    PercentRounded(std::get<0>(bucket), total_usd_value),
                    PercentRounded(static_cast<double>(std::get<2>(bucket)), static_cast<double>(total_size)));
            }
        }
    }

    w.commit();
    LogInfo("utxostats: wrote %u age buckets and %u address bins for height %d",
        stats.age_buckets, stats.address_bins, stats.height);
    return stats;
}

} // namespace enterprise
