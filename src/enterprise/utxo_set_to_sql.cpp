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


#include <timedata.h>

#include <enterprise/utxo_set_to_sql.h>
#include <enterprise/utilities.h>

#include <enterprise/dotenv.h>
#include <pqxx/pqxx>


UtxoSetToSql::UtxoSetToSql(CBlockIndex *block_index, const CBlock &block, CCoinsViewCache &view, unsigned int flags,
                           CCoinsViewCursor *cursor) {

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
    std::map<std::string, std::tuple<CAmount, unsigned int, int64_t>> utxo_address_map;
    std::map<std::string, std::tuple<CAmount, unsigned int, int64_t>> utxo_script_type_map;


    auto &dotenv = env;
    dotenv.config();

    std::stringstream connStream;
    connStream << "dbname = "
               << dotenv["PGDB"]
               << " user = "
               << dotenv["PGUSER"]
               << " password = "
               << dotenv["PGPASSWORD"]
               << " hostaddr = "
               << dotenv["PGHOST"]
               << " port = "
               << dotenv["PGPORT"];
    pqxx::connection c(connStream.str());

    pqxx::work w1(c);
//    "SELECT price FROM prices WHERE day = '" + median_time + "';"
    pqxx::result r = w1.exec("SELECT price FROM prices WHERE day = '" + median_time + "';");
    LogPrintf("UtxoSetToSql: USD Price Query: %s\n", "SELECT price FROM prices WHERE day = '" + median_time + "';");
    w1.commit();

    double usd_price = r[0][0].as<double>();
    LogPrintf("UtxoSetToSql: USD Price: %f\n", usd_price);

    pqxx::work w(c);
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

        CTxDestination address;
        std::string address_string = "no-address";
        bool has_address = ExtractDestination(coin.out.scriptPubKey, address);
        if (has_address) {
            address_string = EncodeDestination(address);
        };

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

        int64_t lowerBoundUsd = std::pow(10, static_cast<int64_t>(std::log10(usd_value)));
        int64_t upperBoundUsd = lowerBoundUsd * 10;
        auto &balance_usd_tuple = utxo_balance_usd_map[{lowerBoundUsd, upperBoundUsd}];
        std::get<0>(balance_usd_tuple) += usd_value;
        std::get<1>(balance_usd_tuple) += 1;
        std::get<2>(balance_usd_tuple) += utxo_size;

        auto &address_tuple = utxo_address_map[address_string];
        std::get<0>(address_tuple) += coin_value;
        std::get<1>(address_tuple) += 1;
        std::get<2>(address_tuple) += utxo_size;

        auto &script_type_tuple = utxo_script_type_map[script_type];
        std::get<0>(script_type_tuple) += coin_value;
        std::get<1>(script_type_tuple) += 1;
        std::get<2>(script_type_tuple) += utxo_size;

        cursor->Next();
    }

    pqxx::stream_to stream{pqxx::stream_to::table(
            w,
            {"utxo_age"},
            {"block_height", "median_time", "weeks_old", "utxo_count", "utxo_value",
                                     "utxo_size", "utxo_count_percent", "utxo_value_percent", "utxo_size_percent"})};
    for (const auto &entry : utxo_age_map) {
        unsigned int weeks_old = entry.first;
        CAmount utxo_value = std::get<0>(entry.second);
        unsigned int utxo_count = std::get<1>(entry.second);
        int64_t utxo_size = std::get<2>(entry.second);

        double value_percentage = std::round(static_cast<double>(utxo_value) / total_value * 10000.0) / 100.0;
        double count_percentage = std::round(static_cast<double>(utxo_count) / total_count * 10000.0) / 100.0;
        double size_percentage = std::round(static_cast<double>(utxo_size) / total_size * 10000.0) / 100.0;

        stream.write_values(block_height, median_time, weeks_old, utxo_count, utxo_value, utxo_size, count_percentage, value_percentage, size_percentage);
    };
    stream.complete();

    pqxx::stream_to stream2{pqxx::stream_to::table(
            w,
            {"utxo_balances"},
            {"block_height", "median_time", "lower_bound", "upper_bound", "utxo_count", "utxo_value",
                                     "utxo_size", "utxo_count_percent", "utxo_value_percent", "utxo_size_percent"})};
    for (const auto &entry : utxo_balance_map) {
        CAmount lower_bound = std::get<0>(entry.first);
        CAmount upper_bound = std::get<1>(entry.first);
        CAmount utxo_value = std::get<0>(entry.second);
        unsigned int utxo_count = std::get<1>(entry.second);
        int64_t utxo_size = std::get<2>(entry.second);

        double value_percentage = std::round(static_cast<double>(utxo_value) / total_value * 10000.0) / 100.0;
        double count_percentage = std::round(static_cast<double>(utxo_count) / total_count * 10000.0) / 100.0;
        double size_percentage = std::round(static_cast<double>(utxo_size) / total_size * 10000.0) / 100.0;

        stream2.write_values(block_height, median_time, lower_bound, upper_bound, utxo_count, utxo_value, utxo_size, count_percentage, value_percentage, size_percentage);
    }
    stream2.complete();

    pqxx::stream_to stream2b{pqxx::stream_to::table(
            w,
            {"utxo_balances_usd"},
            {"block_height", "median_time", "lower_bound", "upper_bound", "utxo_count", "utxo_value",
                                     "utxo_size", "utxo_count_percent", "utxo_value_percent", "utxo_size_percent"})};

    for (const auto &entry : utxo_balance_usd_map) {
        int64_t lower_bound = std::get<0>(entry.first);
        int64_t upper_bound = std::get<1>(entry.first);
        double utxo_value = std::get<0>(entry.second);
        unsigned int utxo_count = std::get<1>(entry.second);
        int64_t utxo_size = std::get<2>(entry.second);

        double value_percentage = std::round(static_cast<double>(utxo_value) / total_usd_value * 10000.0) / 100.0;
        double count_percentage = std::round(static_cast<double>(utxo_count) / total_count * 10000.0) / 100.0;
        double size_percentage = std::round(static_cast<double>(utxo_size) / total_size * 10000.0) / 100.0;

        stream2b.write_values(block_height, median_time, lower_bound, upper_bound, utxo_count, utxo_value, utxo_size, count_percentage, value_percentage, size_percentage);
    }
    stream2b.complete();

    pqxx::stream_to stream3{pqxx::stream_to::table(
            w,
            {"utxo_addresses"},
            {"block_height", "median_time", "address", "utxo_count", "utxo_value",
                                     "utxo_size", "utxo_count_percent", "utxo_value_percent", "utxo_size_percent"})};
    for (const auto &entry : utxo_address_map) {
        std::string address = entry.first;
        CAmount utxo_value = std::get<0>(entry.second);
        unsigned int utxo_count = std::get<1>(entry.second);
        int64_t utxo_size = std::get<2>(entry.second);

        double value_percentage = std::round(static_cast<double>(utxo_value) / total_value * 10000.0) / 100.0;
        double count_percentage = std::round(static_cast<double>(utxo_count) / total_count * 10000.0) / 100.0;
        double size_percentage = std::round(static_cast<double>(utxo_size) / total_size * 10000.0) / 100.0;

        stream3.write_values(block_height, median_time, address, utxo_count, utxo_value, utxo_size, count_percentage, value_percentage, size_percentage);
    };
    stream3.complete();

    pqxx::stream_to stream4{pqxx::stream_to::table(
            w,
            {"utxo_script_types"},
            {"block_height", "median_time", "script_type", "utxo_count", "utxo_value",
                                     "utxo_size", "utxo_count_percent", "utxo_value_percent", "utxo_size_percent"})};
    for (const auto &entry : utxo_script_type_map) {
        std::string script_type = entry.first;
        CAmount utxo_value = std::get<0>(entry.second);
        unsigned int utxo_count = std::get<1>(entry.second);
        int64_t utxo_size = std::get<2>(entry.second);

        double value_percentage = std::round(static_cast<double>(utxo_value) / total_value * 10000.0) / 100.0;
        double count_percentage = std::round(static_cast<double>(utxo_count) / total_count * 10000.0) / 100.0;
        double size_percentage = std::round(static_cast<double>(utxo_size) / total_size * 10000.0) / 100.0;

        stream4.write_values(block_height, median_time, script_type, utxo_count, utxo_value, utxo_size, count_percentage, value_percentage, size_percentage);
    };
    stream4.complete();

    w.commit();

    LogPrintf("UtxoSetToSql: Block %d, %d UTXOs, %d bytes\n", block_height, total_count, total_size);

}