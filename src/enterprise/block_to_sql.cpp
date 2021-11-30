#include <chain.h>
#include <chainparams.h>
#include <consensus/validation.h>
#include <core_io.h>
#include <logging.h>
#include <pubkey.h>
#include <primitives/block.h>
#include <rpc/blockchain.h>
#include <script/standard.h>
#include <serialize.h>
#include <validation.h>
#include <node/transaction.h>
#include <index/txindex.h>

#include <enterprise/block_to_sql.h>
#include <enterprise/utilities.h>

#include <enterprise/dotenv.h>
#include <pqxx/pqxx>

using namespace dotenv;

BlockToSql::BlockToSql(const CBlockIndex block_index, const CBlock block) : m_block_index(block_index),
                                                                            m_block(block),
                                                                            m_block_header_hash(
                                                                                    block.GetBlockHeader().GetHash().GetHex()) {
    auto &dotenv = env;
    dotenv.config();

    std::stringstream connStream;
    connStream << "dbname = " << dotenv["PGDB"] << " user = " << dotenv["PGUSER"] << " password = "
               << dotenv["PGPASSWORD"] << " hostaddr = " << dotenv["PGHOST"] << " port = " << dotenv["PGPORT"];
    pqxx::connection c(connStream.str());

    c.prepare("CheckBlock", "SELECT hash from bitcoin.blocks WHERE hash = $1 LIMIT 1;");
    pqxx::work w(c);
    auto r1{w.exec_prepared("CheckBlock", m_block_header_hash)};
    w.commit();
    if (std::size(r1) == 1) return;

    std::map<CAmount, unsigned int> fee_rates;

    unsigned int segwit_spend_count = 0;
    unsigned int outputs_count = 0;
    unsigned int inputs_count = 0;
    unsigned int total_output_value = 0;
    unsigned int total_fees = 0;
    unsigned int total_size = 0;
    unsigned int total_vsize = 0;
    unsigned int total_weight = 0;

    std::ostringstream output_data_string_stream;
    output_data_string_stream << "{";

    std::ostringstream input_data_string_stream;
    input_data_string_stream << "{";

    std::ostringstream transaction_data_string_stream;
    transaction_data_string_stream << "{";

    for (std::size_t transaction_index = 0; transaction_index < m_block.vtx.size(); ++transaction_index) {
        const CTransactionRef &transaction = m_block.vtx[transaction_index];

        TransactionData transaction_data = TransactionData{transaction_index, m_block.vtx[transaction_index]};

        // Outputs
        for (std::size_t output_vector = 0; output_vector < transaction->vout.size(); ++output_vector) {
            const CTxOut &txout_data = transaction->vout[output_vector];
            unsigned int output_size = GetSerializeSize(txout_data, PROTOCOL_VERSION);
            unsigned int utxo_size = output_size + PER_UTXO_OVERHEAD;
            transaction_data.total_output_value += txout_data.nValue;
            transaction_data.utxo_size_inc += utxo_size;

            std::vector <std::vector<unsigned char>> solutions_data;
            TxoutType which_type = Solver(txout_data.scriptPubKey, solutions_data);
            const unsigned int script_type = GetTxnOutputTypeEnum(which_type);

            output_data_string_stream << "{";
            output_data_string_stream << output_size << ",";
            output_data_string_stream << txout_data.nValue << ",";
            output_data_string_stream << transaction_data.GetFeeRate() << ",";
            output_data_string_stream << script_type;
            output_data_string_stream << "}";
            if (transaction_index != m_block.vtx.size()-1 || output_vector != transaction->vout.size()-1) {
                output_data_string_stream << ",";
            }
        }

        //        Inputs
        for (std::size_t input_vector = 0; input_vector < transaction->vin.size(); ++input_vector) {
            if (transaction_data.is_coinbase) {
                continue;
            }

            const CTxIn &txin_data = transaction->vin[input_vector];

            uint256 hash_block;
            CTransactionRef spent_output_transaction = GetTransaction(
                    nullptr,
                    nullptr,
                    txin_data.prevout.hash,
                    Params().GetConsensus(),
                    hash_block
            );
            if (hash_block.IsNull()) {
                for (std::size_t transaction_index = 0; transaction_index < m_block.vtx.size(); ++transaction_index) {
                    const CTransactionRef &transaction = m_block.vtx[transaction_index];
                    if (txin_data.prevout.hash == transaction->GetHash() ||
                        txin_data.prevout.hash == transaction->GetWitnessHash()) {
                        spent_output_transaction = transaction;
                        hash_block = block.GetBlockHeader().GetHash();
                    }
                }
            }
            if (hash_block.IsNull()) {
                if (!g_txindex) {
                    LogPrintf("Use -txindex\n");
                } else {
                    LogPrintf("No such blockchain transaction\n");
                }
                LogPrintf("%s-%i not found while processing block \n", txin_data.prevout.hash.GetHex(),
                          txin_data.prevout.n);
            }

            const CTxOut &spent_output_data = spent_output_transaction->vout[txin_data.prevout.n];
            unsigned int spent_output_size = GetSerializeSize(spent_output_data, PROTOCOL_VERSION);
            unsigned int spent_utxo_size = spent_output_size + PER_UTXO_OVERHEAD;
            transaction_data.total_input_value += spent_output_data.nValue;
            transaction_data.utxo_size_inc -= spent_utxo_size;

            std::vector <std::vector<unsigned char>> solutions_data;
            TxoutType which_type = Solver(spent_output_data.scriptPubKey, solutions_data);
            const unsigned int spent_script_type = GetTxnOutputTypeEnum(which_type);

            input_data_string_stream << "{";
            input_data_string_stream << GetTransactionInputWeight(txin_data) << ",";
            input_data_string_stream << spent_output_size << ",";
            input_data_string_stream << spent_output_data.nValue << ",";
            input_data_string_stream << spent_script_type;
            input_data_string_stream << "}";
            if (transaction_index != m_block.vtx.size()-1 || input_vector != transaction->vin.size()-1) {
                input_data_string_stream << ",";
            }
        }

        segwit_spend_count += transaction_data.is_segwit_out_spend;
        outputs_count += transaction_data.m_transaction->vout.size();
        inputs_count += transaction_data.m_transaction->vin.size();
        total_output_value += transaction_data.total_output_value;
        total_fees += transaction_data.GetFee();
        total_size += transaction_data.m_transaction->GetTotalSize();
        total_vsize += transaction_data.vsize;
        total_weight += transaction_data.weight;

        transaction_data_string_stream << "{";
        transaction_data_string_stream << transaction_data.m_transaction->GetTotalSize() << ",";
        transaction_data_string_stream << transaction_data.vsize << ",";
        transaction_data_string_stream << transaction_data.weight << ",";
        transaction_data_string_stream << transaction_data.GetFee();
        transaction_data_string_stream << "}";
        if (transaction_index != m_block.vtx.size()-1) {
            transaction_data_string_stream << ",";
        }

        CAmount fee_rate = transaction_data.GetFee() / transaction_data.vsize;
        fee_rates[fee_rate] += transaction_data.weight;
    }

    std::ostringstream fee_rates_string_stream;
    fee_rates_string_stream << "{";
    for (auto it = fee_rates.begin(); it != fee_rates.end(); ++it) {
        fee_rates_string_stream << "{";
        fee_rates_string_stream << it->first;
        fee_rates_string_stream << ",";
        fee_rates_string_stream << it->second;
        fee_rates_string_stream << "}";
        if ((it != fee_rates.end()) && (next(it) == fee_rates.end())) continue;
        fee_rates_string_stream << ",";
    }

    fee_rates_string_stream << "}";
    output_data_string_stream << "}";
    input_data_string_stream << "}";
    transaction_data_string_stream << "}";

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
                             "total_fees, "

                             "total_size, "
                             "total_vsize, "
                             "total_weight, "

                             "fee_rates, "
                             "output_data, "
                             "input_data, "

                             "transaction_data"
                             ") "
                             "VALUES "
                             "("
                             "$1, "
                             "$2, "
                             "$3, "
                             "$4, "
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
                             "$25"
                             ");");

    auto r2{w.exec_prepared(
            "InsertBlock",
            m_block_header_hash,                   // hash
            m_block_index.hashMerkleRoot.GetHex(), // merkle_root
            m_block_index.GetBlockTime(),          // time

            m_block_index.GetMedianTimePast(),     // median_time
            m_block_index.nHeight,                 // height
            GetBlockSubsidy(m_block_index.nHeight, Params().GetConsensus()), // subsidy

            m_block_index.nTx,                     // transactions_count
            m_block_index.nVersion,                // version
            m_block_index.nStatus,                 // status

            m_block_index.nBits,                   // bits
            m_block_index.nNonce,                  // nonce
            GetDifficulty(&m_block_index),         // difficulty

            m_block_index.nChainWork.GetHex(),      // chain_work
            segwit_spend_count,
            outputs_count,

            inputs_count,
            total_output_value,
            total_fees,

            total_size,
            total_vsize,
            total_weight,

            fee_rates_string_stream.str(),
            output_data_string_stream.str(),
            input_data_string_stream.str(),

            transaction_data_string_stream.str()
    )};
    w.commit();

}

TransactionData::TransactionData(const int &transaction_index, const CTransactionRef &transaction) :
        m_transaction_index(transaction_index),
        m_transaction(transaction) {
    transaction_hash = transaction->GetHash().GetHex();
    is_coinbase = transaction->IsCoinBase();
    weight = GetTransactionWeight(*transaction);
    vsize = (weight + WITNESS_SCALE_FACTOR - 1) / WITNESS_SCALE_FACTOR;
    is_segwit_out_spend = is_coinbase ? false : !(transaction->GetHash() == transaction->GetWitnessHash());
};
