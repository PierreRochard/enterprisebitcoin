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

#include <enterprise/block_to_sql.h>
#include <enterprise/database.h>
#include <enterprise/utilities.h>

#include "enterprise/models/addresses.h"
#include "enterprise/models/blocks.h"
#include "enterprise/models/inputs.h"
#include "enterprise/models/outputs.h"
#include "enterprise/models/scripts.h"
#include "enterprise/models/transactions.h"

#include "enterprise/models/addresses-odb.hxx"
#include "enterprise/models/blocks-odb.hxx"
#include "enterprise/models/inputs-odb.hxx"
#include "enterprise/models/outputs-odb.hxx"
#include "enterprise/models/scripts-odb.hxx"
#include "enterprise/models/transactions-odb.hxx"

#include <odb/database.hxx>
#include <odb/result.hxx>
#include <odb/transaction.hxx>

#include <boost/accumulators/accumulators.hpp>
#include <boost/accumulators/statistics/count.hpp>
#include <boost/accumulators/statistics/sum.hpp>
#include <boost/accumulators/statistics/sum_kahan.hpp>
#include <boost/accumulators/statistics/stats.hpp>
#include <boost/accumulators/statistics/moment.hpp>
#include <boost/accumulators/statistics/weighted_p_square_quantile.hpp>

using namespace boost::accumulators;

typedef accumulator_set<
        int64_t,
        stats<
            tag::weighted_p_square_quantile
        >
        , int64_t
        > accumulator_t;

BlockToSql::BlockToSql(const CBlockIndex block_index, const CBlock block) : m_block_index(block_index),
                                                                            m_block(block),
                                                                            m_block_header_hash(block.GetBlockHeader().GetHash().GetHex())
{
    // Check if block is already in the SQL database
    std::auto_ptr<odb::database> enterprise_database(create_enterprise_database());
    {
        typedef odb::query<eBlocks> query;
        odb::transaction t(enterprise_database->begin());
        std::auto_ptr<eBlocks> r(enterprise_database->query_one<eBlocks>(query::hash == m_block_header_hash));
        if (r.get() != 0) {
            return;
        }
        t.commit();
    }

    // Initialize the block's SQL data record
    eBlocks block_record(
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
            m_block_index.nChainWork.GetHex()      // chain_work
    );

    std::map<double, accumulator_t> accumulators;
    for ( double n = 0.01; n < 1 ; n += 0.01 ) {
        accumulators.insert(std::make_pair(n, accumulator_t(quantile_probability = n)));
    }
    accumulator_t acc0(quantile_probability = 0.5);


    for (std::size_t transaction_index = 0; transaction_index < m_block.vtx.size(); ++transaction_index) {
        const CTransactionRef& transaction = m_block.vtx[transaction_index];

        TransactionData transaction_data = TransactionData {transaction_index, m_block.vtx[transaction_index]};

        // Outputs
        for (std::size_t output_vector = 0; output_vector < transaction->vout.size(); ++output_vector) {
            const CTxOut& txout_data = transaction->vout[output_vector];
            unsigned int output_size = GetSerializeSize(txout_data, PROTOCOL_VERSION);
            unsigned int utxo_size = output_size + PER_UTXO_OVERHEAD;
            transaction_data.total_output_value += txout_data.nValue;
            transaction_data.utxo_size_inc += utxo_size;

            std::vector<std::vector<unsigned char>> solutions_data;
            txnouttype which_type = Solver(txout_data.scriptPubKey, solutions_data);
            const char* script_type = GetTxnOutputType(which_type);

            std::ostringstream oss;
            oss << "";
            oss << output_size << "," ;
            oss << txout_data.nValue << ",";
            oss << transaction_data.GetFeeRate() << ",";
            oss << CScriptID(txout_data.scriptPubKey).GetHex() << ",";
            oss << ScriptToAsmStr(txout_data.scriptPubKey) << ",";
            oss << script_type;
            oss << ";";
            block_record.output_data += oss.str();

        }

        // Inputs
        for (std::size_t input_vector = 0; input_vector < transaction->vin.size(); ++input_vector) {
            if (transaction_data.is_coinbase) {
                continue;
            }

            const CTxIn& txin_data = transaction->vin[input_vector];

            CTransactionRef spent_output_transaction;
            uint256 hash_block;
            bool in_current_block = false;
            bool was_found = GetTransaction(txin_data.prevout.hash, spent_output_transaction, Params().GetConsensus(), hash_block);
            if (!was_found) {
                for (std::size_t transaction_index = 0; transaction_index < m_block.vtx.size(); ++transaction_index) {
                    const CTransactionRef& transaction = m_block.vtx[transaction_index];
                    if (txin_data.prevout.hash == transaction->GetHash() ||
                        txin_data.prevout.hash == transaction->GetWitnessHash()) {
                        spent_output_transaction = transaction;
                        in_current_block = true;
                    }
                }
            }

            if (!was_found && !in_current_block) {
                LogPrintf("%s-%i not found while processing block \n", txin_data.prevout.hash.GetHex(),
                          txin_data.prevout.n);
            }

            const CTxOut& spent_output_data = spent_output_transaction->vout[txin_data.prevout.n];
            unsigned int spent_output_size = GetSerializeSize(spent_output_data, PROTOCOL_VERSION);
            unsigned int spent_utxo_size = spent_output_size + PER_UTXO_OVERHEAD;
            transaction_data.total_input_value += spent_output_data.nValue;
            transaction_data.utxo_size_inc -= spent_utxo_size;

            std::vector<std::vector<unsigned char>> solutions_data;
            txnouttype which_type = Solver(spent_output_data.scriptPubKey, solutions_data);
            const char* spent_script_type = GetTxnOutputType(which_type);

            std::ostringstream oss;
            oss << "";
            oss << spent_output_size << "," ;
            oss << spent_output_data.nValue << ",";
            oss << CScriptID(spent_output_data.scriptPubKey).GetHex() << ",";
            oss << spent_script_type << ",";
            oss << ScriptToAsmStr(spent_output_data.scriptPubKey) << ",";
            oss << GetTransactionInputWeight(txin_data) << "," ;
            oss << CScriptID(txin_data.scriptSig).GetHex() << ",";
            oss << ScriptToAsmStr(txin_data.scriptSig, true) << ",";
            if (!txin_data.scriptWitness.IsNull()) {
                for (const auto& item : txin_data.scriptWitness.stack) {
                    oss << HexStr(item.begin(), item.end()) << "*";
                }
            }
            oss << ";";
            block_record.input_data += oss.str();
        }

        block_record.segwit_spend_count += transaction_data.is_segwit_out_spend;
        block_record.outputs_count += transaction_data.m_transaction->vout.size();
        block_record.inputs_count += transaction_data.m_transaction->vin.size();
        block_record.total_output_value += transaction_data.total_output_value;
        block_record.total_fees += transaction_data.GetFee();
        block_record.total_size += transaction_data.m_transaction->GetTotalSize();
        block_record.total_vsize += transaction_data.vsize;
        block_record.total_weight += transaction_data.weight;
        std::ostringstream oss2;
        oss2 << "";
        oss2 << transaction_data.m_transaction->GetTotalSize() << "," ;
        oss2 << transaction_data.vsize << ",";
        oss2 << transaction_data.weight << ",";
        oss2 << transaction_data.GetFee();
        oss2 << ";";
        block_record.fee_data += oss2.str();

        for(auto it = accumulators.begin(); it != accumulators.end(); ++it)
        {
            it->second(transaction_data.GetFee()/transaction_data.vsize, weight=transaction_data.vsize);
        }

        acc0(transaction_data.GetFee()/transaction_data.vsize, weight=transaction_data.vsize);
    }
    block_record.median_fee = weighted_p_square_quantile(acc0);

    std::ostringstream oss3;
    oss3 << "{";
    for(auto it = accumulators.begin(); it != accumulators.end(); ++it)
    {
        oss3 << "{";
        oss3 << it->first;
        oss3 << ",";
        oss3 << weighted_p_square_quantile(it->second);
        oss3 << "}";
        double median = 0.5;
        if (std::abs(it->first - median) < 0.001) {
            block_record.median_fee = weighted_p_square_quantile(it->second);
        }
        if (it != accumulators.end())
        {
            oss3 << ",";
        }
    }
    oss3 << "}";

    block_record.fee_distribution = oss3.str();

    // Insert block into the database
    odb::transaction t(enterprise_database->begin(), false);
    odb::transaction::current(t);
    enterprise_database->persist(block_record);
    t.commit();
}

TransactionData::TransactionData(const int &transaction_index, const CTransactionRef &transaction) :
        m_transaction_index(transaction_index),
        m_transaction(transaction)
{
    transaction_hash = transaction->GetHash().GetHex();
    is_coinbase = transaction->IsCoinBase();
    weight = GetTransactionWeight(*transaction);
    vsize = (weight + WITNESS_SCALE_FACTOR - 1) / WITNESS_SCALE_FACTOR;
    is_segwit_out_spend = is_coinbase ? false : !(transaction->GetHash() == transaction->GetWitnessHash());
};
