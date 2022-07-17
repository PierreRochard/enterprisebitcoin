#include <chain.h>
#include <chainparams.h>
#include <consensus/validation.h>
#include <core_io.h>
#include <logging.h>
#include <pubkey.h>
#include <primitives/block.h>
#include <rpc/blockchain.h>
#include <script/interpreter.h>
#include <script/standard.h>
#include <serialize.h>
#include <validation.h>
#include <node/transaction.h>
#include <index/txindex.h>
#include <txmempool.h>

#include <timedata.h>

#include <enterprise/block_to_sql.h>
#include <enterprise/utilities.h>

#include <enterprise/dotenv.h>
#include <pqxx/pqxx>

using namespace dotenv;


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


    const unsigned int removal_reason = GetMemPoolRemovalReasonEnum(reason);
    c.prepare("UpdateMempoolEntry", "UPDATE bitcoin.mempool_entries SET "
                                    "removal_reason = $1, removal_time = $2 WHERE txid = $3;");

    auto r2{w.exec_prepared(
            "UpdateMempoolEntry",
            removal_reason,                   // 1 removal_reason
            GetAdjustedTime(),            // 2 removal_time
            hash.GetHex()                                     // 3 txid

    )};
    w.commit();

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


    c.prepare("InsertMempoolEntry", "INSERT INTO bitcoin.mempool_entries "
                                    "("
                                    "txid, "
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
                                    "to_timestamp($6), "

                                    "$7, "
                                    "$8, "
                                    "$9, "

                                    "$10, "
                                    "to_timestamp($11), "

                                    "$12, "
                                    "$13, "
                                    "$14, "

                                    "$15, "
                                    "$16, "
                                    "$17, "
                                    "$18 "
                                    ") ON CONFLICT (txid) DO NOTHING;");

    auto r2{w.exec_prepared(
            "InsertMempoolEntry",
            mempool_entry.GetTx().GetHash().GetHex(),                   // 1 txid
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

BlockToSql::BlockToSql(CBlockIndex *block_index, const CBlock &block, CCoinsViewCache &view, unsigned int flags) {
    static constexpr size_t
    PER_UTXO_OVERHEAD = sizeof(COutPoint) + sizeof(uint32_t) + sizeof(bool);

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

    std::map<CAmount, unsigned int> fee_rates;

    std::map<unsigned int, std::array<uint64_t, 4>> output_script_types;
    std::map<unsigned int, std::array<uint64_t, 6>> input_script_types;

    unsigned int segwit_spend_count = 0;
    unsigned int outputs_count = 0;
    unsigned int inputs_count = 0;
    CAmount total_output_value = 0;
    CAmount total_input_value = 0;
    CAmount total_fees = 0;

    uint64_t block_output_legacy_signature_operations = 0;
    uint64_t block_input_legacy_signature_operations = 0;
    uint64_t block_input_p2sh_signature_operations = 0;
    uint64_t block_input_witness_signature_operations = 0;

    uint64_t block_outputs_total_size = 0;
    uint64_t block_inputs_total_size = 0;

    int64_t block_net_utxo_size_impact = 0;

    std::ostringstream output_data_string_stream;
    output_data_string_stream << "[";

    std::ostringstream input_data_string_stream;
    input_data_string_stream << "[";

    std::ostringstream transaction_data_string_stream;
    transaction_data_string_stream << "[";

    for (std::size_t transaction_index = 0; transaction_index < block.vtx.size(); ++transaction_index) {
        const CTransactionRef &transaction = block.vtx[transaction_index];

        TransactionData transaction_data = TransactionData{transaction_index, block.vtx[transaction_index]};

        // Outputs
        for (std::size_t output_vector = 0; output_vector < transaction->vout.size(); ++output_vector) {
            const CTxOut &txout_data = transaction->vout[output_vector];
            int64_t output_size = GetSerializeSize(txout_data, PROTOCOL_VERSION);
            if (output_size > 300) {
                LogPrintf("\n");
            }
            block_outputs_total_size += output_size;
            int64_t utxo_size = output_size + PER_UTXO_OVERHEAD;
            int64_t this_output_legacy_signature_operations =
                    txout_data.scriptPubKey.GetSigOpCount(false) * WITNESS_SCALE_FACTOR;
            block_output_legacy_signature_operations += this_output_legacy_signature_operations;

            transaction_data.total_output_value += txout_data.nValue;
            transaction_data.utxo_size_inc += utxo_size;
            block_net_utxo_size_impact += utxo_size;

            std::vector <std::vector<unsigned char>> solutions_data;
            TxoutType which_type = Solver(txout_data.scriptPubKey, solutions_data);
            const unsigned int script_type = GetTxnOutputTypeEnum(which_type);
            output_script_types[script_type][0] += 1;
            output_script_types[script_type][1] += output_size;
            output_script_types[script_type][2] += txout_data.nValue;
            output_script_types[script_type][3] += this_output_legacy_signature_operations;

            output_data_string_stream << "[";
            output_data_string_stream << output_size << ",";
            output_data_string_stream << txout_data.nValue << ",";
            output_data_string_stream << transaction_data.GetFeeRate() << ",";
            output_data_string_stream << script_type;
            output_data_string_stream << "]";
            if (transaction_index != block.vtx.size() - 1 || output_vector != transaction->vout.size() - 1) {
                output_data_string_stream << ",";
            }
        }

        //        Inputs
        for (std::size_t input_vector = 0; input_vector < transaction->vin.size(); ++input_vector) {
            if (transaction_data.is_coinbase) {
                continue;
            }

            const CTxIn &txin_data = transaction->vin[input_vector];
            const Coin &coin = view.AccessCoin(txin_data.prevout);
            CTxOut spent_output_data = coin.out;
            if (coin.IsSpent()) {
                CTransactionRef spent_output_transaction;
                for (std::size_t transaction_index = 0; transaction_index < block.vtx.size(); ++transaction_index) {
                    const CTransactionRef &transaction = block.vtx[transaction_index];
                    if (txin_data.prevout.hash == transaction->GetHash() ||
                        txin_data.prevout.hash == transaction->GetWitnessHash()) {
                        spent_output_transaction = transaction;
                    }
                }
                spent_output_data = spent_output_transaction->vout[txin_data.prevout.n];
            }

            int64_t this_input_legacy_signature_operations =
                    txin_data.scriptSig.GetSigOpCount(false) * WITNESS_SCALE_FACTOR;
            block_input_legacy_signature_operations += this_input_legacy_signature_operations;

            int64_t this_input_p2sh_signature_operations = 0;
            if (flags & SCRIPT_VERIFY_P2SH && spent_output_data.scriptPubKey.IsPayToScriptHash()) {
                this_input_p2sh_signature_operations = spent_output_data.scriptPubKey.GetSigOpCount(
                        txin_data.scriptSig) * WITNESS_SCALE_FACTOR;
            }
            block_input_p2sh_signature_operations += this_input_p2sh_signature_operations;


            int64_t this_input_witness_signature_operations = 0;
            this_input_witness_signature_operations += CountWitnessSigOps(txin_data.scriptSig,
                                                                          spent_output_data.scriptPubKey,
                                                                          &txin_data.scriptWitness,
                                                                          flags);
            block_input_witness_signature_operations += this_input_witness_signature_operations;

            unsigned int spent_output_size = GetSerializeSize(spent_output_data, PROTOCOL_VERSION);

            if (spent_output_size > 300) {
                LogPrintf("\n");
            }
            unsigned int spent_utxo_size = spent_output_size + PER_UTXO_OVERHEAD;
            transaction_data.total_input_value += spent_output_data.nValue;
            transaction_data.utxo_size_inc -= spent_utxo_size;
            block_net_utxo_size_impact -= spent_utxo_size;

            std::vector <std::vector<unsigned char>> solutions_data;
            TxoutType which_type = Solver(spent_output_data.scriptPubKey, solutions_data);
            const unsigned int spent_script_type = GetTxnOutputTypeEnum(which_type);

            uint64_t input_size = GetSerializeSize(txin_data, PROTOCOL_VERSION);
            block_inputs_total_size += input_size;

            input_script_types[spent_script_type][0] += 1;
            input_script_types[spent_script_type][1] += GetTransactionInputWeight(txin_data);
            input_script_types[spent_script_type][2] += input_size;
            input_script_types[spent_script_type][3] += spent_output_size;
            input_script_types[spent_script_type][4] += spent_output_data.nValue;
            input_script_types[spent_script_type][5] += this_input_legacy_signature_operations;
            input_script_types[spent_script_type][5] += this_input_p2sh_signature_operations;
            input_script_types[spent_script_type][5] += this_input_witness_signature_operations;

            input_data_string_stream << "[";
            input_data_string_stream << GetTransactionInputWeight(txin_data) << ",";
            input_data_string_stream << input_size << ",";
            input_data_string_stream << spent_output_size << ",";
            input_data_string_stream << spent_output_data.nValue << ",";
            input_data_string_stream << spent_script_type;
            input_data_string_stream << "]";
            if (transaction_index != block.vtx.size() - 1 || input_vector != transaction->vin.size() - 1) {
                input_data_string_stream << ",";
            }
        }

        segwit_spend_count += transaction_data.is_segwit_out_spend;
        outputs_count += transaction_data.m_transaction->vout.size();
        inputs_count += transaction_data.m_transaction->vin.size();
        total_output_value += transaction_data.total_output_value;
        total_input_value += transaction_data.total_input_value;
        total_fees += transaction_data.GetFee();

        transaction_data_string_stream << "[";
        transaction_data_string_stream << transaction_data.m_transaction->GetTotalSize() << ",";
        transaction_data_string_stream << transaction_data.vsize << ",";
        transaction_data_string_stream << transaction_data.weight << ",";
        transaction_data_string_stream << transaction_data.GetFee();
        transaction_data_string_stream << "]";
        if (transaction_index != block.vtx.size() - 1) {
            transaction_data_string_stream << ",";
        }

        CAmount fee_rate = transaction_data.GetFee() / transaction_data.vsize;
        fee_rates[fee_rate] += transaction_data.weight;
    }

    std::ostringstream fee_rates_string_stream;
    fee_rates_string_stream << "[";
    for (auto it = fee_rates.begin(); it != fee_rates.end(); ++it) {
        fee_rates_string_stream << "[";
        fee_rates_string_stream << it->first;
        fee_rates_string_stream << ",";
        fee_rates_string_stream << it->second;
        fee_rates_string_stream << "]";
        if ((it != fee_rates.end()) && (next(it) == fee_rates.end())) continue;
        fee_rates_string_stream << ",";
    }

    std::ostringstream output_script_types_string_stream;
    output_script_types_string_stream << "[";
    for (auto it = output_script_types.begin(); it != output_script_types.end(); ++it) {
        output_script_types_string_stream << "[";
        output_script_types_string_stream << it->first;
        output_script_types_string_stream << ",";
        auto arr = it->second;
        for (auto it2 = std::begin(arr); it2 != std::end(arr); ++it2) {
            output_script_types_string_stream << *it2;
            if ((it2 != std::end(arr)) && (std::next(it2) == std::end(arr))) continue;
            output_script_types_string_stream << ",";
        }
        output_script_types_string_stream << "]";
        if ((it != output_script_types.end()) && (next(it) == output_script_types.end())) continue;
        output_script_types_string_stream << ",";
    }

    std::ostringstream input_script_types_string_stream;
    input_script_types_string_stream << "[";
    for (auto it = input_script_types.begin(); it != input_script_types.end(); ++it) {
        input_script_types_string_stream << "[";
        input_script_types_string_stream << it->first;
        input_script_types_string_stream << ",";
        auto arr = it->second;
        for (auto it2 = std::begin(arr); it2 != std::end(arr); ++it2) {
            input_script_types_string_stream << *it2;
            if ((it2 != std::end(arr)) && (std::next(it2) == std::end(arr))) continue;
            input_script_types_string_stream << ",";
        }
        input_script_types_string_stream << "]";
        if ((it != input_script_types.end()) && (next(it) == input_script_types.end())) continue;
        input_script_types_string_stream << ",";
    }

    fee_rates_string_stream << "]";
    output_data_string_stream << "]";
    input_data_string_stream << "]";
    transaction_data_string_stream << "]";
    input_script_types_string_stream << "]";
    output_script_types_string_stream << "]";

    c.prepare("InsertBlock", "INSERT INTO bitcoin.blocks "
                             "("
                             "hash, "
                             "merkle_root, "
                             "time, "

                             "median_time, "
                             "height, "
                             "subsidy, "

                             "transactions_count, "
                             "version, "
                             "status, "

                             "bits, "
                             "nonce, "
                             "difficulty, "

                             "chain_work, "
                             "segwit_spend_count, "
                             "outputs_count, "

                             "inputs_count, "
                             "total_output_value, "
                             "total_input_value, "
                             "total_fees, "

                             "total_size, "
                             "total_vsize, "
                             "total_weight, "

                             "fee_rates, "
                             "output_data, "
                             "input_data, "

                             "transaction_data, "
                             "output_script_types, "
                             "input_script_types, "


                             "output_legacy_signature_operations, "
                             "input_legacy_signature_operations, "
                             "input_p2sh_signature_operations, "
                             "input_witness_signature_operations, "

                             "outputs_total_size, "
                             "inputs_total_size, "
                             "net_utxo_size_impact, "
                             "hash_prev_block"
                             ") "

                             "VALUES "
                             "("
                             "$1, "
                             "$2, "
                             "to_timestamp($3), "
                             "to_timestamp($4), "
                             "$5, "
                             "$6, "
                             "$7, "
                             "$8, "
                             "$9, "
                             "$10, "
                             "$11, "
                             "$12, "
                             "$13, "
                             "$14, "
                             "$15, "
                             "$16, "
                             "$17, "
                             "$18, "
                             "$19, "
                             "$20, "
                             "$21, "
                             "$22, "
                             "$23, "
                             "$24, "
                             "$25, "
                             "$26, "
                             "$27, "
                             "$28, "
                             "$29, "
                             "$30, "
                             "$31, "
                             "$32, "
                             "$33, "
                             "$34, "
                             "$35, "
                             "$36"
                             ") ON CONFLICT (hash) DO NOTHING;");

    auto r2{w.exec_prepared(
            "InsertBlock",
            block.GetBlockHeader().GetHash().GetHex(),                   // hash
            block_index->hashMerkleRoot.GetHex(), // merkle_root
            block_index->GetBlockTime(),          // time

            block_index->GetMedianTimePast(),     // median_time
            block_index->nHeight,                 // height
            GetBlockSubsidy(block_index->nHeight, Params().GetConsensus()), // subsidy

            block_index->nTx,                     // transactions_count
            block_index->nVersion,                // version
            block_index->nStatus,                 // status

            block_index->nBits,                   // bits
            block_index->nNonce,                  // nonce
            GetDifficulty(block_index),         // difficulty

            block_index->nChainWork.GetHex(),      // chain_work
            segwit_spend_count,
            outputs_count,

            inputs_count,
            total_output_value,
            total_input_value,
            total_fees,

            GetSerializeSize(block, CLIENT_VERSION),
            GetBlockWeight(block) / WITNESS_SCALE_FACTOR,
            GetBlockWeight(block),

            fee_rates_string_stream.str(),
            output_data_string_stream.str(),
            input_data_string_stream.str(),

            transaction_data_string_stream.str(),
            output_script_types_string_stream.str(),
            input_script_types_string_stream.str(),

            block_output_legacy_signature_operations,
            block_input_legacy_signature_operations,
            block_input_p2sh_signature_operations,
            block_input_witness_signature_operations,

            block_outputs_total_size,
            block_inputs_total_size,
            block_net_utxo_size_impact,
            block.GetBlockHeader().hashPrevBlock.ToString()

    )};
    w.commit();

}

TransactionData::TransactionData(const int &transaction_index, const CTransactionRef &transaction) :
        m_transaction_index(transaction_index),
        m_transaction(transaction) {
    transaction_hash = transaction->GetHash().GetHex();
    is_coinbase = transaction->IsCoinBase();
    weight = GetTransactionWeight(*transaction);
    vsize = GetVirtualTransactionSize(*transaction);
    is_segwit_out_spend = is_coinbase ? false : !(transaction->GetHash() == transaction->GetWitnessHash());
};
