//    fs::create_directories("addresses/" + std::to_string(block_index->nHeight / 10000));

//    Write addresses_string_stream to a file
//    std::ofstream addresses_file;
//    addresses_file.open(
//            "addresses/" + std::to_string(block_index->nHeight / 10000) + "/" + std::to_string(block_index->nHeight) +
//            ".csv");
//    addresses_file << addresses_string_stream.str();
//    addresses_file.close();


// Todo:  Stream [height, median_time, txid:vector, address, script_type, debit, credit] into a table
//    pqxx::stream_to stream{
//            tx,
//            "score",
//            std::vector<std::string>{"name", "points"}};
//    for (auto const &entry: scores)
//        stream << entry;
//    stream.complete();


std::map < unsigned int, std::map < unsigned int, std::map < bool, std::array < uint64_t, 3 >>
>> utxo_set_stats;

uint64_t utxo_set_value = 0;
uint64_t utxo_set_value_satoshi = 0;
uint64_t utxo_set_value_less_than_one_day = 0;
uint64_t utxo_set_value_less_than_one_week = 0;
uint64_t utxo_set_value_less_than_one_month = 0;
uint64_t utxo_set_value_less_than_three_months = 0;
uint64_t utxo_set_value_less_than_six_months = 0;
uint64_t utxo_set_value_less_than_one_year = 0;
uint64_t utxo_set_value_less_than_two_years = 0;
uint64_t utxo_set_value_less_than_three_years = 0;
uint64_t utxo_set_value_less_than_four_years = 0;
uint64_t utxo_set_value_less_than_five_years = 0;
uint64_t utxo_set_value_less_than_six_years = 0;
uint64_t utxo_set_value_less_than_seven_years = 0;
uint64_t utxo_set_value_less_than_eight_years = 0;
uint64_t utxo_set_value_less_than_nine_years = 0;
uint64_t utxo_set_value_less_than_ten_years = 0;
uint64_t utxo_set_value_less_than_eleven_years = 0;
uint64_t utxo_set_value_less_than_twelve_years = 0;
uint64_t utxo_set_value_less_than_thirteen_years = 0;
uint64_t utxo_set_value_less_than_fourteen_years = 0;
uint64_t utxo_set_value_less_than_fifteen_years = 0;

uint64_t utxo_set_count = 0;
uint64_t utxo_set_count_satoshi = 0;
uint64_t utxo_set_count_less_than_one_day = 0;
uint64_t utxo_set_count_less_than_one_week = 0;
uint64_t utxo_set_count_less_than_one_month = 0;
uint64_t utxo_set_count_less_than_three_months = 0;
uint64_t utxo_set_count_less_than_six_months = 0;
uint64_t utxo_set_count_less_than_one_year = 0;
uint64_t utxo_set_count_less_than_two_years = 0;
uint64_t utxo_set_count_less_than_three_years = 0;
uint64_t utxo_set_count_less_than_four_years = 0;
uint64_t utxo_set_count_less_than_five_years = 0;
uint64_t utxo_set_count_less_than_six_years = 0;
uint64_t utxo_set_count_less_than_seven_years = 0;
uint64_t utxo_set_count_less_than_eight_years = 0;
uint64_t utxo_set_count_less_than_nine_years = 0;
uint64_t utxo_set_count_less_than_ten_years = 0;
uint64_t utxo_set_count_less_than_eleven_years = 0;
uint64_t utxo_set_count_less_than_twelve_years = 0;
uint64_t utxo_set_count_less_than_thirteen_years = 0;
uint64_t utxo_set_count_less_than_fourteen_years = 0;
uint64_t utxo_set_count_less_than_fifteen_years = 0;
//
//    while (cursor->Valid()) {
//        Coin coin;
//        cursor->GetValue(coin);
//
//        utxo_set_value += coin.out.nValue;
//        utxo_set_count += 1;
//
//        if (
////                coin.nHeight <= 50000
//                false
//                ) {
//            utxo_set_value_satoshi += coin.out.nValue;
//            utxo_set_count_satoshi += 1;
//        } else {
//            if (coin.nHeight >= block_index->nHeight - 144) {
//                utxo_set_value_less_than_one_day += coin.out.nValue;
//                utxo_set_count_less_than_one_day += 1;
//            }
//            if (coin.nHeight >= block_index->nHeight - 144 * 7) {
//                utxo_set_value_less_than_one_week += coin.out.nValue;
//                utxo_set_count_less_than_one_week += 1;
//            }
//            if (coin.nHeight > block_index->nHeight - 144 * 30) {
//                utxo_set_value_less_than_one_month += coin.out.nValue;
//                utxo_set_count_less_than_one_month += 1;
//            }
//            if (coin.nHeight > block_index->nHeight - 144 * 90) {
//                utxo_set_value_less_than_three_months += coin.out.nValue;
//                utxo_set_count_less_than_three_months += 1;
//            }
//            if (coin.nHeight > block_index->nHeight - 144 * 180) {
//                utxo_set_value_less_than_six_months += coin.out.nValue;
//                utxo_set_count_less_than_six_months += 1;
//            }
//            if (coin.nHeight > block_index->nHeight - 144 * 365) {
//                utxo_set_value_less_than_one_year += coin.out.nValue;
//                utxo_set_count_less_than_one_year += 1;
//            }
//            if (coin.nHeight > block_index->nHeight - 144 * 365 * 2) {
//                utxo_set_value_less_than_two_years += coin.out.nValue;
//                utxo_set_count_less_than_two_years += 1;
//            }
//            if (coin.nHeight > block_index->nHeight - 144 * 365 * 3) {
//                utxo_set_value_less_than_three_years += coin.out.nValue;
//                utxo_set_count_less_than_three_years += 1;
//            }
//            if (coin.nHeight > block_index->nHeight - 144 * 365 * 4) {
//                utxo_set_value_less_than_four_years += coin.out.nValue;
//                utxo_set_count_less_than_four_years += 1;
//            }
//            if (coin.nHeight > block_index->nHeight - 144 * 365 * 5) {
//                utxo_set_value_less_than_five_years += coin.out.nValue;
//                utxo_set_count_less_than_five_years += 1;
//            }
//            if (coin.nHeight > block_index->nHeight - 144 * 365 * 6) {
//                utxo_set_value_less_than_six_years += coin.out.nValue;
//                utxo_set_count_less_than_six_years += 1;
//            }
//            if (coin.nHeight > block_index->nHeight - 144 * 365 * 7) {
//                utxo_set_value_less_than_seven_years += coin.out.nValue;
//                utxo_set_count_less_than_seven_years += 1;
//            }
//            if (coin.nHeight > block_index->nHeight - 144 * 365 * 8) {
//                utxo_set_value_less_than_eight_years += coin.out.nValue;
//                utxo_set_count_less_than_eight_years += 1;
//            }
//            if (coin.nHeight > block_index->nHeight - 144 * 365 * 9) {
//                utxo_set_value_less_than_nine_years += coin.out.nValue;
//                utxo_set_count_less_than_nine_years += 1;
//            }
//            if (coin.nHeight > block_index->nHeight - 144 * 365 * 10) {
//                utxo_set_value_less_than_ten_years += coin.out.nValue;
//                utxo_set_count_less_than_ten_years += 1;
//            }
//            if (coin.nHeight > block_index->nHeight - 144 * 365 * 11) {
//                utxo_set_value_less_than_eleven_years += coin.out.nValue;
//                utxo_set_count_less_than_eleven_years += 1;
//            }
//            if (coin.nHeight > block_index->nHeight - 144 * 365 * 12) {
//                utxo_set_value_less_than_twelve_years += coin.out.nValue;
//                utxo_set_count_less_than_twelve_years += 1;
//            }
//            if (coin.nHeight > block_index->nHeight - 144 * 365 * 13) {
//                utxo_set_value_less_than_thirteen_years += coin.out.nValue;
//                utxo_set_count_less_than_thirteen_years += 1;
//            }
//            if (coin.nHeight > block_index->nHeight - 144 * 365 * 14) {
//                utxo_set_value_less_than_fourteen_years += coin.out.nValue;
//                utxo_set_count_less_than_fourteen_years += 1;
//            }
//            if (coin.nHeight > block_index->nHeight - 144 * 365 * 15) {
//                utxo_set_value_less_than_fifteen_years += coin.out.nValue;
//                utxo_set_count_less_than_fifteen_years += 1;
//            }
//        }
//
//
//        std::vector <std::vector<unsigned char>> solutions_data;
//        TxoutType which_type = Solver(coin.out.scriptPubKey, solutions_data);
//        const unsigned int script_type = GetTxnOutputTypeEnum(which_type);
//        unsigned int rounded_coin_height = coin.nHeight - (coin.nHeight % 144);
//        utxo_set_stats[rounded_coin_height][script_type][coin.IsCoinBase()][0] += 1;
//        utxo_set_stats[rounded_coin_height][script_type][coin.IsCoinBase()][1] += coin.out.nValue;
//        utxo_set_stats[rounded_coin_height][script_type][coin.IsCoinBase()][2] +=
//                PER_UTXO_OVERHEAD + coin.out.scriptPubKey.size();
//
//        cursor->Next();
//    };

//
//    if (false
////    block_index->nHeight > 777000
//            ) {
//
////        write the utxo_set_stats to the utxo_set_statistics sql table here
//        pqxx::work w2(c);
//        c.prepare("InsertStats",
//                  "INSERT INTO utxo_set_statistics "
//                  "("
//                  "stats_height, "
//                  "stats_day, "
//
//                  "output_height,"
//                  "output_script_type, "
//                  "output_is_coinbase, "
//
//                  "outputs_count, "
//                  "outputs_total_value, "
//                  "outputs_total_size "
//                  ") "
//                  "VALUES "
//                  "("
//                  "$1, " // 1 stats_height
//                  "to_timestamp($2)::date, " // 2 stats_day
//
//                  "$3, " // 3 output_height
//                  "$4, " // 4 output_script_type
//                  "$5, " // 5 output_is_coinbase
//
//                  "$6, " // 6 outputs_count
//                  "$7, " // 7 outputs_total_value
//                  "$8 " // 8 outputs_total_size
//                  ") ON CONFLICT DO NOTHING;");
//        for (const auto &output_height: utxo_set_stats) {
//            for (const auto &script_type: output_height.second) {
//                for (const auto &is_coinbase: script_type.second) {
//                    auto r2{w2.exec_prepared(
//                            "InsertStats",
//                            block_index->nHeight, // 1 stats_height
//                            block_index->GetMedianTimePast(), // 2 stats_day
//
//                            output_height.first, // 3 output_height
//                            script_type.first, // 4 output_script_type
//                            is_coinbase.first, // 5 output_is_coinbase
//
//                            is_coinbase.second[0], // 6 outputs_count
//                            is_coinbase.second[1], // 7 outputs_total_value
//                            is_coinbase.second[2] // 8 outputs_total_size
//                    )};
//                }
//            }
//        }
//        w2.commit();
//    }


"utxo_set_value, "
"utxo_set_value_less_than_one_day, "
"utxo_set_value_less_than_one_week, "
"utxo_set_value_less_than_one_month, "
"utxo_set_value_less_than_three_months, "
"utxo_set_value_less_than_six_months, "
"utxo_set_value_less_than_one_year, "
"utxo_set_value_less_than_two_years, "
"utxo_set_value_less_than_three_years, "
"utxo_set_value_less_than_four_years, "
"utxo_set_value_less_than_five_years, "
"utxo_set_value_less_than_six_years, "
"utxo_set_value_less_than_seven_years, "
"utxo_set_value_less_than_eight_years, "
"utxo_set_value_less_than_nine_years, "
"utxo_set_value_less_than_ten_years, "
"utxo_set_value_less_than_eleven_years, "
"utxo_set_value_less_than_twelve_years, "
"utxo_set_value_less_than_thirteen_years, "
"utxo_set_value_less_than_fourteen_years, "
"utxo_set_value_less_than_fifteen_years, "
"utxo_set_value_satoshi,"

"utxo_set_count, "
"utxo_set_count_satoshi, "
"utxo_set_count_less_than_one_day, "
"utxo_set_count_less_than_one_week, "
"utxo_set_count_less_than_one_month, "
"utxo_set_count_less_than_three_months, "
"utxo_set_count_less_than_six_months, "
"utxo_set_count_less_than_one_year, "
"utxo_set_count_less_than_two_years, "
"utxo_set_count_less_than_three_years, "
"utxo_set_count_less_than_four_years, "
"utxo_set_count_less_than_five_years, "
"utxo_set_count_less_than_six_years, "
"utxo_set_count_less_than_seven_years, "
"utxo_set_count_less_than_eight_years, "
"utxo_set_count_less_than_nine_years, "
"utxo_set_count_less_than_ten_years, "
"utxo_set_count_less_than_eleven_years, "
"utxo_set_count_less_than_twelve_years, "
"utxo_set_count_less_than_thirteen_years, "
"utxo_set_count_less_than_fourteen_years, "
"utxo_set_count_less_than_fifteen_years"


"$68, "  // utxo_set_value
"$69, "  // utxo_set_value_less_than_one_day
"$70, "  // utxo_set_value_less_than_one_week
"$71, "  // utxo_set_value_less_than_one_month
"$72, "  // utxo_set_value_less_than_three_months
"$73, "  // utxo_set_value_less_than_six_months
"$74, "  // utxo_set_value_less_than_one_year
"$75, "  // utxo_set_value_less_than_two_years
"$76, "  // utxo_set_value_less_than_three_years
"$77, "  // utxo_set_value_less_than_four_years
"$78, "  // utxo_set_value_less_than_five_years
"$79, "  // utxo_set_value_less_than_six_years
"$80, "  // utxo_set_value_less_than_seven_years
"$81, "  // utxo_set_value_less_than_eight_years
"$82, "  // utxo_set_value_less_than_nine_years
"$83, "  // utxo_set_value_less_than_ten_years
"$84, "  // utxo_set_value_less_than_eleven_years
"$85, "  // utxo_set_value_less_than_twelve_years
"$86, "  // utxo_set_value_less_than_thirteen_years
"$87, "  // utxo_set_value_less_than_fourteen_years
"$88, "  // utxo_set_value_less_than_fifteen_years
"$89, "  // utxo_set_value_satoshi

"$90, "  // utxo_set_count
"$91, "  // utxo_set_count_satoshi
"$92, "  // utxo_set_count_less_than_one_day
"$93, "  // utxo_set_count_less_than_one_week
"$94, "  // utxo_set_count_less_than_one_month
"$95, "  // utxo_set_count_less_than_three_months
"$96, "  // utxo_set_count_less_than_six_months
"$97, "  // utxo_set_count_less_than_one_year
"$98, "  // utxo_set_count_less_than_two_years
"$99, "  // utxo_set_count_less_than_three_years
"$100, "  // utxo_set_count_less_than_four_years
"$101, "  // utxo_set_count_less_than_five_years
"$102, "  // utxo_set_count_less_than_six_years
"$103, "  // utxo_set_count_less_than_seven_years
"$104, "  // utxo_set_count_less_than_eight_years
"$105, "  // utxo_set_count_less_than_nine_years
"$106, "  // utxo_set_count_less_than_ten_years
"$107, "  // utxo_set_count_less_than_eleven_years
"$108, "  // utxo_set_count_less_than_twelve_years
"$109, "  // utxo_set_count_less_than_thirteen_years
"$110, "  // utxo_set_count_less_than_fourteen_years
"$111 "  // utxo_set_count_less_than_fifteen_years


utxo_set_value,
utxo_set_value_less_than_one_day,
utxo_set_value_less_than_one_week,
utxo_set_value_less_than_one_month,
utxo_set_value_less_than_three_months,
utxo_set_value_less_than_six_months,
utxo_set_value_less_than_one_year,
utxo_set_value_less_than_two_years,
utxo_set_value_less_than_three_years,
utxo_set_value_less_than_four_years,
utxo_set_value_less_than_five_years,
utxo_set_value_less_than_six_years,
utxo_set_value_less_than_seven_years,
utxo_set_value_less_than_eight_years,
utxo_set_value_less_than_nine_years,
utxo_set_value_less_than_ten_years,
utxo_set_value_less_than_eleven_years,
utxo_set_value_less_than_twelve_years,
utxo_set_value_less_than_thirteen_years,
utxo_set_value_less_than_fourteen_years,
utxo_set_value_less_than_fifteen_years,
utxo_set_value_satoshi,

utxo_set_count,
utxo_set_count_satoshi,
utxo_set_count_less_than_one_day,
utxo_set_count_less_than_one_week,
utxo_set_count_less_than_one_month,
utxo_set_count_less_than_three_months,
utxo_set_count_less_than_six_months,
utxo_set_count_less_than_one_year,
utxo_set_count_less_than_two_years,
utxo_set_count_less_than_three_years,
utxo_set_count_less_than_four_years,
utxo_set_count_less_than_five_years,
utxo_set_count_less_than_six_years,
utxo_set_count_less_than_seven_years,
utxo_set_count_less_than_eight_years,
utxo_set_count_less_than_nine_years,
utxo_set_count_less_than_ten_years,
utxo_set_count_less_than_eleven_years,
utxo_set_count_less_than_twelve_years,
utxo_set_count_less_than_thirteen_years,
utxo_set_count_less_than_fourteen_years,
utxo_set_count_less_than_fifteen_years