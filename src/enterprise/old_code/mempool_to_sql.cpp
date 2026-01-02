
std::string GetMemPoolRemovalReasonString(MemPoolRemovalReason r) {
    switch (r) {
        case MemPoolRemovalReason::EXPIRY:
            return "expiry";
        case MemPoolRemovalReason::SIZELIMIT:
            return "size_limit";
        case MemPoolRemovalReason::REORG:
            return "reorg";
        case MemPoolRemovalReason::BLOCK:
            return "block";
        case MemPoolRemovalReason::CONFLICT:
            return "conflict";
        case MemPoolRemovalReason::REPLACED:
            return "replaced";
    } // no default case, so the compiler can warn about missing cases
    assert(false);
}



RemoveMempoolEntry::RemoveMempoolEntry(const uint256 hash, MemPoolRemovalReason reason) {
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


    std::string removal_reason = GetMemPoolRemovalReasonString(reason);
    c.prepare("UpdateMempoolEntry", "UPDATE mempool_entries SET "
                                    "removal_reason = $1, "
                                    "removal_time = to_timestamp($2), "
                                    "network = $3 "
                                    "WHERE txid = $4;");
//    int removal_time = std::chrono::seconds{GetAdjustedTime()}.count();
//    auto r2{w.exec_prepared(
//            "UpdateMempoolEntry",
//            removal_reason,            // 1 removal_reason
//            removal_time,              // 2 removal_time
//            ChainToString(),              // 3 network
//            hash.GetHex()              // 4 txid
//
//    )};
//    w.commit();

};

MempoolEntryToSql::MempoolEntryToSql(CTxMemPoolEntry mempool_entry) {
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


    c.prepare("InsertMempoolEntry", "INSERT INTO mempool_entries "
                                    "("
                                    "txid, "
                                    "network, "
                                    "wtxid, "
                                    "fee, "
                                    "weight, "

                                    "memory_usage, "
                                    "entry_time, "

                                    "entry_height, "
                                    "spends_coinbase, "
                                    "sigop_cost, "

                                    "height_lockpoint, "
                                    "time_lockpoint, "

                                    "descendants_count, "
                                    "descendants_size, "
                                    "descendants_fees, "

                                    "ancestors_count, "
                                    "ancestors_size, "
                                    "ancestors_fees, "
                                    "ancestors_sigop_cost "

                                    ") "

                                    "VALUES "
                                    "("
                                    "$1, "
                                    "$2, "
                                    "$3, "
                                    "$4, "
                                    "$5, "

                                    "$6, "
                                    "to_timestamp($7), "

                                    "$8, "
                                    "$9, "
                                    "$10, "

                                    "$11, "
                                    "to_timestamp($12), "

                                    "$13, "
                                    "$14, "
                                    "$15, "

                                    "$16, "
                                    "$17, "
                                    "$18, "
                                    "$19 "
                                    ") ON CONFLICT (txid) DO NOTHING;");

    auto r2{w.exec_prepared(
            "InsertMempoolEntry",
            mempool_entry.GetTx().GetHash().GetHex(),                   // 1 txid
            ChainToString(),                                               // 2 network
            mempool_entry.GetTx().GetWitnessHash().GetHex(),            // 2 wtxid
            mempool_entry.GetFee(),                                     // 3 fee
            mempool_entry.GetTxWeight(),                                // 4 weight

            mempool_entry.DynamicMemoryUsage(),                         // 5 memory_usage
            mempool_entry.GetTime().count(),                            // 6 entry_time

            mempool_entry.GetHeight(),                                  // 7 entry_height
            mempool_entry.GetSpendsCoinbase(),                          // 8 spends_coinbase
            mempool_entry.GetSigOpCost(),                               // 9 sigop_cost

            mempool_entry.GetLockPoints().height,                       // 10 height_lockpoint
            mempool_entry.GetLockPoints().time,                         // 11 time_lockpoint

            mempool_entry.GetCountWithDescendants(),                // 12 descendants_count
            mempool_entry.GetSizeWithDescendants(),                 // 13 descendants_size
            mempool_entry.GetModFeesWithDescendants(),              // 14 descendants_fees

            mempool_entry.GetCountWithAncestors(),                  // 15 ancestors_count
            mempool_entry.GetSizeWithAncestors(),                   // 16 ancestors_size
            mempool_entry.GetModFeesWithAncestors(),                // 17 ancestors_fees
            mempool_entry.GetSigOpCostWithAncestors()               // 18 ancestors_sigop_cost

    )};
    w.commit();

};