#include <chain.h>
#include <chainparams.h>
#include <consensus/validation.h>
#include <core_io.h>
#include <key_io.h>
#include <logging.h>
#include <pubkey.h>
#include <primitives/block.h>
#include <script/interpreter.h>
#include <serialize.h>
#include <validation.h>
#include <node/transaction.h>
#include <index/txindex.h>
#include <txmempool.h>
#include <common/system.h>
#include <common/args.h>
#include <rpc/blockchain.h>
#include <cmath>
#include <ctime>
#include <string>
#include <algorithm>
#include <string_view>
#include <tuple>
#include <utility>

#include <enterprise/utxo_set_to_sql.h>
#include <enterprise/utilities.h>

#include <enterprise/db.h>
#include <enterprise/dotenv.h>
#include <pqxx/pqxx>

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

struct UtxoAgeRow {
    unsigned int weeks_old;
    CAmount utxo_value;
    unsigned int utxo_count;
    int64_t utxo_size;
    double count_percentage;
    double value_percentage;
    double size_percentage;
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
};

struct UtxoBalanceUsdRow {
    int64_t lower_bound;
    int64_t upper_bound;
    double utxo_value;
    unsigned int utxo_count;
    int64_t utxo_size;
    double count_percentage;
    double value_percentage;
    double size_percentage;
};

struct UtxoPercentileRow {
    int percentile;
    double utxo_value;
};

struct UtxoScriptTypeRow {
    std::string script_type;
    CAmount utxo_value;
    unsigned int utxo_count;
    int64_t utxo_size;
    double count_percentage;
    double value_percentage;
    double size_percentage;
};

struct UtxoSetExportData {
    int block_height;
    std::string median_time;
    std::vector<UtxoAgeRow> utxo_age_rows;
    std::vector<UtxoBalanceRow> utxo_balance_rows;
    std::vector<UtxoBalanceUsdRow> utxo_balance_usd_rows;
    std::vector<UtxoPercentileRow> utxo_percentiles;
    std::vector<UtxoPercentileRow> utxo_usd_percentiles;
    std::vector<UtxoScriptTypeRow> script_type_rows;
};

void EnqueueUtxoInsert(UtxoSetExportData&& data)
{
    enterprise::DbWorkQueue::Instance().Enqueue([data = std::move(data)](pqxx::connection& conn) mutable {
        pqxx::work w(conn);

        auto stream_age = pqxx::stream_to::table(
                w,
                pqxx::table_path{"utxo_age"},
                {std::string_view{"block_height"}, std::string_view{"median_time"}, std::string_view{"weeks_old"}, std::string_view{"utxo_count"}, std::string_view{"utxo_value"},
                 std::string_view{"utxo_size"}, std::string_view{"utxo_count_percent"}, std::string_view{"utxo_value_percent"}, std::string_view{"utxo_size_percent"}});
        for (const auto& row : data.utxo_age_rows) {
            stream_age.write_values(data.block_height, data.median_time, row.weeks_old, row.utxo_count,
                                    row.utxo_value, row.utxo_size, row.count_percentage, row.value_percentage,
                                    row.size_percentage);
        }
        stream_age.complete();

        auto stream_bal = pqxx::stream_to::table(
                w,
                pqxx::table_path{"utxo_balances"},
                {std::string_view{"block_height"}, std::string_view{"median_time"}, std::string_view{"lower_bound"}, std::string_view{"upper_bound"}, std::string_view{"utxo_count"}, std::string_view{"utxo_value"},
                 std::string_view{"utxo_size"}, std::string_view{"utxo_count_percent"}, std::string_view{"utxo_value_percent"}, std::string_view{"utxo_size_percent"}});
        for (const auto& row : data.utxo_balance_rows) {
            stream_bal.write_values(data.block_height, data.median_time, row.lower_bound, row.upper_bound,
                                    row.utxo_count, row.utxo_value, row.utxo_size, row.count_percentage,
                                    row.value_percentage, row.size_percentage);
        }
        stream_bal.complete();

        auto stream_bal_usd = pqxx::stream_to::table(
                w,
                pqxx::table_path{"utxo_balances_usd"},
                {std::string_view{"block_height"}, std::string_view{"median_time"}, std::string_view{"lower_bound"}, std::string_view{"upper_bound"}, std::string_view{"utxo_count"}, std::string_view{"utxo_value"},
                 std::string_view{"utxo_size"}, std::string_view{"utxo_count_percent"}, std::string_view{"utxo_value_percent"}, std::string_view{"utxo_size_percent"}});
        for (const auto& row : data.utxo_balance_usd_rows) {
            stream_bal_usd.write_values(data.block_height, data.median_time, row.lower_bound, row.upper_bound,
                                        row.utxo_count, row.utxo_value, row.utxo_size, row.count_percentage,
                                        row.value_percentage, row.size_percentage);
        }
        stream_bal_usd.complete();

        auto stream_percentiles = pqxx::stream_to::table(
                w,
                pqxx::table_path{"utxo_balances_percentiles"},
                {std::string_view{"block_height"}, std::string_view{"median_time"}, std::string_view{"percentile"}, std::string_view{"utxo_value"}});
        for (const auto& row : data.utxo_percentiles) {
            stream_percentiles.write_values(data.block_height, data.median_time, row.percentile, row.utxo_value);
        }
        stream_percentiles.complete();

        auto stream_usd_percentiles = pqxx::stream_to::table(
                w,
                pqxx::table_path{"utxo_balances_usd_percentiles"},
                {std::string_view{"block_height"}, std::string_view{"median_time"}, std::string_view{"percentile"}, std::string_view{"utxo_value"}});
        for (const auto& row : data.utxo_usd_percentiles) {
            stream_usd_percentiles.write_values(data.block_height, data.median_time, row.percentile, row.utxo_value);
        }
        stream_usd_percentiles.complete();

        auto stream_script = pqxx::stream_to::table(
                w,
                pqxx::table_path{"utxo_script_types"},
                {std::string_view{"block_height"}, std::string_view{"median_time"}, std::string_view{"script_type"}, std::string_view{"utxo_count"}, std::string_view{"utxo_value"},
                 std::string_view{"utxo_size"}, std::string_view{"utxo_count_percent"}, std::string_view{"utxo_value_percent"}, std::string_view{"utxo_size_percent"}});
        for (const auto& row : data.script_type_rows) {
            stream_script.write_values(data.block_height, data.median_time, row.script_type, row.utxo_count,
                                       row.utxo_value, row.utxo_size, row.count_percentage, row.value_percentage,
                                       row.size_percentage);
        }
        stream_script.complete();

        w.commit();
    });
}

} // namespace

UtxoSetToSql::UtxoSetToSql(CBlockIndex *block_index, const CBlock &block, CCoinsViewCache &view, script_verify_flags flags,
                           CCoinsViewCursor *cursor) {

    (void)flags; // currently unused; kept for interface symmetry

    int block_height = block_index->nHeight;

    int64_t median_time_int = block_index->GetMedianTimePast();
    time_t median_time_time = static_cast<time_t>(median_time_int);
    struct tm *timeinfo;
    timeinfo = localtime(&median_time_time);
    char buffer[80];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);

    std::string median_time = std::string(buffer);

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

    auto& conn = enterprise::PgConnection();
    pqxx::work w1(conn);

    const std::string price_query = "SELECT price FROM prices WHERE day = '" + median_time + "';";
    pqxx::result r = w1.exec(price_query);
    LogInfo("UtxoSetToSql: USD Price Query: %s", price_query.c_str());
    w1.commit();
    if (r.empty()) {
        throw std::runtime_error("No price data for the given day");
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

        cursor->Next();
    }
    LogInfo("Completed UTXO Set to SQL for block %d", block_height);

    UtxoSetExportData export_data;
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
        double utxo_value = std::get<0>(entry.second);
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
