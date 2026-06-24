#include <enterprise/dotenv.h>
#include <enterprise/mempool_to_sql.h>

#include <common/args.h>
#include <kernel/mempool_removal_reason.h>
#include <util/chaintype.h>
#include <util/time.h>

#include <chrono>
#include <pqxx/pqxx>
#include <sstream>
#include <string>

namespace {
std::string ChainToString()
{
    return ChainTypeToString(gArgs.GetChainType());
}

pqxx::connection Connect()
{
    auto& dotenv = env;
    dotenv.config();

    std::stringstream conn_stream;
    conn_stream << "dbname = "
                << dotenv["PGDB"]
                << " user = "
                << dotenv["PGUSER"]
                << " password = "
                << dotenv["PGPASSWORD"]
                << " hostaddr = "
                << dotenv["PGHOST"]
                << " port = "
                << dotenv["PGPORT"];
    return pqxx::connection{conn_stream.str()};
}
} // namespace

RemoveMempoolEntry::RemoveMempoolEntry(const Txid& hash, MemPoolRemovalReason reason)
{
    pqxx::connection c{Connect()};
    pqxx::work w{c};

    c.prepare("UpdateMempoolEntry", "UPDATE mempool_entries SET "
                                    "removal_reason = $1, "
                                    "removal_time = to_timestamp($2), "
                                    "network = $3 "
                                    "WHERE txid = $4;");
    w.exec_prepared(
        "UpdateMempoolEntry",
        RemovalReasonToString(reason),
        GetTime<std::chrono::seconds>().count(),
        ChainToString(),
        hash.GetHex());
    w.commit();
}

MempoolEntryToSql::MempoolEntryToSql(const CTxMemPoolEntry& mempool_entry)
{
    pqxx::connection c{Connect()};
    pqxx::work w{c};

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

    const LockPoints& lock_points{mempool_entry.GetLockPoints()};
    w.exec_prepared(
        "InsertMempoolEntry",
        mempool_entry.GetTx().GetHash().GetHex(),
        ChainToString(),
        mempool_entry.GetTx().GetWitnessHash().GetHex(),
        mempool_entry.GetFee(),
        mempool_entry.GetTxWeight(),

        mempool_entry.DynamicMemoryUsage(),
        mempool_entry.GetTime().count(),

        mempool_entry.GetHeight(),
        mempool_entry.GetSpendsCoinbase(),
        mempool_entry.GetSigOpCost(),

        lock_points.height,
        lock_points.time,

        0,
        0,
        0,

        0,
        0,
        0,
        0);
    w.commit();
}
