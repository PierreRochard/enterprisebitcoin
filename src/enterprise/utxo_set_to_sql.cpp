#include <enterprise/utxo_set_to_sql.h>

#include <chain.h>
#include <coins.h>
#include <consensus/amount.h>
#include <enterprise/pg_config.h>
#include <enterprise/pqxx_compat.h>
#include <logging.h>
#include <primitives/transaction.h>
#include <script/solver.h>
#include <serialize.h>
#include <uint256.h>
#include <util/log.h>
#include <util/time.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <functional>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace {

constexpr size_t PER_UTXO_OVERHEAD = sizeof(COutPoint) + sizeof(uint32_t) + sizeof(bool);
constexpr double BLOCKS_PER_WEEK = 1008.0;

using AgeBucket = std::tuple<CAmount, uint64_t, int64_t>;
using BalanceBucket = std::tuple<CAmount, uint64_t, int64_t>;
using UsdBalanceBucket = std::tuple<double, uint64_t, int64_t>;

pqxx::connection ConnectPg()
{
    return pqxx::connection{enterprise::PgConnectionString()};
}

void EnsureUtxoStatsTables(pqxx::work& w)
{
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

bool ShouldExportUtxoSet(int height, int64_t db_max_height, bool height_already_exported, bool force)
{
    if (height < 0) return false;
    if (height_already_exported && !force) return false;
    if (force) return true;
    if (db_max_height < 0) return true;
    return (height / UTXO_EXPORT_INTERVAL) > (db_max_height / UTXO_EXPORT_INTERVAL);
}

int64_t MaxExportedUtxoHeight()
{
    try {
        pqxx::connection c{ConnectPg()};
        pqxx::work w{c};
        EnsureUtxoStatsTables(w);
        const auto res = w.exec("SELECT COALESCE(MAX(block_height), -1) FROM utxo_age");
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
        c.prepare("UtxoHeightExists", "SELECT 1 FROM utxo_age WHERE block_height = $1 LIMIT 1");
        const auto res = enterprise::ExecPrepared(w, "UtxoHeightExists", height);
        w.commit();
        return !res.empty();
    } catch (const std::exception& e) {
        LogWarning("utxostats: failed to check utxo_age height %d: %s", height, e.what());
        return false;
    }
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

    std::map<unsigned int, AgeBucket> age_map;
    std::map<std::array<CAmount, 2>, BalanceBucket> balance_map;
    std::map<std::array<int64_t, 2>, UsdBalanceBucket> usd_balance_map;
    std::map<std::string, AgeBucket> script_type_map;

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

    const auto elapsed{Ticks<std::chrono::milliseconds>(NodeClock::now() - started) / 1000.0};
    LogInfo("utxostats: scanned %u coins at height %d in %.1f s", total_count, stats.height, elapsed);

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
    LogInfo("utxostats: wrote %u age buckets for height %d", stats.age_buckets, stats.height);
    return stats;
}

} // namespace enterprise
