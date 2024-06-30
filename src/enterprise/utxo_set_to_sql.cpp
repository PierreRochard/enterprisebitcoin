#include <chain.h>
#include <chainparams.h>
#include <consensus/validation.h>
#include <core_io.h>
#include <key_io.h>
#include <logging.h>
#include <pubkey.h>
#include <primitives/block.h>
#include <rpc/blockchain.h>
#include <script/interpreter.h>
#include <serialize.h>
#include <validation.h>
#include <node/transaction.h>
#include <index/txindex.h>
#include <txmempool.h>
#include <common/system.h>
#include <common/args.h>
#include <rpc/blockchain.h>
#include <cmath> // Include for std::round


#include <timedata.h>

#include <enterprise/utxo_set_to_sql.h>
#include <enterprise/utilities.h>

#include <enterprise/dotenv.h>
#include <pqxx/pqxx>

using namespace dotenv;

UtxoSetToSql::UtxoSetToSql(CBlockIndex *block_index, const CBlock &block, CCoinsViewCache &view, unsigned int flags,
                           CCoinsViewCursor *cursor) {

    int block_height = block_index->nHeight;
    int64_t median_time = block_index->GetMedianTimePast();

    CAmount total_value = 0;
    unsigned int total_count = 0;
    int64_t total_size = 0;

    std::map<unsigned int, std::tuple<CAmount, unsigned int, int64_t>> utxo_age_map;
    std::map<std::array<CAmount, 2>, std::tuple<CAmount, unsigned int, int64_t>> uxto_balance_map;
    std::map<std::string, std::tuple<CAmount, unsigned int, int64_t>> utxo_address_map;
    std::map<std::string, std::tuple<CAmount, unsigned int, int64_t>> utxo_script_type_map;


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
        total_size += utxo_size;

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
        auto &balance_tuple = uxto_balance_map[{lowerBound, upperBound}];
        std::get<0>(balance_tuple) += coin_value;
        std::get<1>(balance_tuple) += 1;
        std::get<2>(balance_tuple) += utxo_size;

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

    pqxx::work w(c);

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

        double value_percentage = static_cast<unsigned int>((utxo_value / static_cast<double>(total_value)) * 100.0);
        double count_percentage = static_cast<unsigned int>((utxo_count / static_cast<double>(total_count)) * 100.0);
        double size_percentage = static_cast<unsigned int>((utxo_size / static_cast<double>(total_size)) * 100.0);

        stream << block_height << median_time << weeks_old << utxo_count << utxo_value << utxo_size << count_percentage << value_percentage << size_percentage;
    };
    stream.complete();

    pqxx::stream_to stream2{pqxx::stream_to::table(
            w,
            {"utxo_balances"},
            {"block_height", "median_time", "lower_bound", "upper_bound", "utxo_count", "utxo_value",
                                     "utxo_size", "utxo_count_percent", "utxo_value_percent", "utxo_size_percent"})};
    for (const auto &entry : uxto_balance_map) {
        CAmount lower_bound = std::get<0>(entry.first);
        CAmount upper_bound = std::get<1>(entry.first);
        CAmount utxo_value = std::get<0>(entry.second);
        unsigned int utxo_count = std::get<1>(entry.second);
        int64_t utxo_size = std::get<2>(entry.second);

        double value_percentage = static_cast<unsigned int>((utxo_value / static_cast<double>(total_value)) * 100.0);
        double count_percentage = static_cast<unsigned int>((utxo_count / static_cast<double>(total_count)) * 100.0);
        double size_percentage = static_cast<unsigned int>((utxo_size / static_cast<double>(total_size)) * 100.0);

        stream2 << block_height << median_time << lower_bound << upper_bound << utxo_count << utxo_value << utxo_size << count_percentage << value_percentage << size_percentage;
    }
    stream2.complete();

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

        double value_percentage = static_cast<unsigned int>((utxo_value / static_cast<double>(total_value)) * 100.0);
        double count_percentage = static_cast<unsigned int>((utxo_count / static_cast<double>(total_count)) * 100.0);
        double size_percentage = static_cast<unsigned int>((utxo_size / static_cast<double>(total_size)) * 100.0);

        stream3 << block_height << median_time << address << utxo_count << utxo_value << utxo_size << count_percentage << value_percentage << size_percentage;
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

        double value_percentage = static_cast<unsigned int>((utxo_value / static_cast<double>(total_value)) * 100.0);
        double count_percentage = static_cast<unsigned int>((utxo_count / static_cast<double>(total_count)) * 100.0);
        double size_percentage = static_cast<unsigned int>((utxo_size / static_cast<double>(total_size)) * 100.0);

        stream4 << block_height << median_time << script_type << utxo_count << utxo_value << utxo_size << count_percentage << value_percentage << size_percentage;
    };
    stream4.complete();

    w.commit();

    LogPrintf("UtxoSetToSql: Block %d, %d UTXOs, %d bytes\n", block_height, total_count, total_size);

}