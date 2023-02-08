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
#include <script/standard.h>
#include <serialize.h>
#include <validation.h>
#include <node/transaction.h>
#include <index/txindex.h>
#include <txmempool.h>
#include <util/system.h>

#include <timedata.h>

#include <enterprise/block_to_sql.h>
#include <enterprise/utilities.h>

#include <enterprise/dotenv.h>
#include <pqxx/pqxx>

using namespace dotenv;


std::string ChainToString() {
    if (gArgs.GetChainName() == CBaseChainParams::MAIN) return "mainnet";
    if (gArgs.GetChainName() == CBaseChainParams::TESTNET) return "testnet";
    if (gArgs.GetChainName() == CBaseChainParams::SIGNET) return "signet";
    if (gArgs.GetChainName() == CBaseChainParams::REGTEST) return "regtest";
    return "unknown";
}

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
    c.prepare("UpdateMempoolEntry", "UPDATE bitcoin.mempool_entries SET "
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


    c.prepare("InsertMempoolEntry", "INSERT INTO bitcoin.mempool_entries "
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

    unsigned int nonstandard_create_count = 0;
    unsigned int pubkey_create_count = 0;
    unsigned int pubkeyhash_create_count = 0;
    unsigned int scripthash_create_count = 0;
    unsigned int multisig_create_count = 0;
    unsigned int null_data_create_count = 0;
    unsigned int witness_v0_keyhash_create_count = 0;
    unsigned int witness_v0_scripthash_create_count = 0;
    unsigned int witness_v1_taproot_create_count = 0;
    unsigned int witness_unknown_create_count = 0;

    unsigned int nonstandard_spend_count = 0;
    unsigned int pubkey_spend_count = 0;
    unsigned int pubkeyhash_spend_count = 0;
    unsigned int scripthash_spend_count = 0;
    unsigned int multisig_spend_count = 0;
    unsigned int null_data_spend_count = 0;
    unsigned int witness_v0_keyhash_spend_count = 0;
    unsigned int witness_v0_scripthash_spend_count = 0;
    unsigned int witness_v1_taproot_spend_count = 0;
    unsigned int witness_unknown_spend_count = 0;

    unsigned int outputs_count = 0;
    unsigned int inputs_count = 0;
    CAmount total_output_value = 0;
    CAmount total_input_value = 0;
    CAmount total_fees = 0;
    CAmount coinbase = 0;

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

    std::ostringstream addresses_string_stream;
    addresses_string_stream << "network, "

                               "input_height, "
                               "input_median_time, "
                               "input_txid, "
                               "input_wtxid, "
                               "input_vector, "
                               "input_size, "

                               "output_height, "
                               "output_median_time, "
                               "output_txid, "
                               "output_wtxid, "
                               "output_vector, "
                               "output_size, "
                               "output_script_type, "

                               "address, "
                               "amount\n";

    for (std::size_t transaction_index = 0; transaction_index < block.vtx.size(); ++transaction_index) {
        const CTransactionRef &transaction = block.vtx[transaction_index];

        TransactionData transaction_data = TransactionData{transaction_index, block.vtx[transaction_index]};

        // Outputs
        for (std::size_t output_vector = 0; output_vector < transaction->vout.size(); ++output_vector) {
            const CTxOut &txout_data = transaction->vout[output_vector];
            int64_t output_size = GetSerializeSize(txout_data, PROTOCOL_VERSION);
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

            switch (which_type) {
                case TxoutType::NONSTANDARD:
                    nonstandard_create_count += 1;
                    break;
                case TxoutType::PUBKEY:
                    pubkey_create_count += 1;
                    break;
                case TxoutType::PUBKEYHASH:
                    pubkeyhash_create_count += 1;
                    break;
                case TxoutType::SCRIPTHASH:
                    scripthash_create_count += 1;
                    break;
                case TxoutType::MULTISIG:
                    multisig_create_count += 1;
                    break;
                case TxoutType::NULL_DATA:
                    null_data_create_count += 1;
                    break;
                case TxoutType::WITNESS_V0_KEYHASH:
                    witness_v0_keyhash_create_count += 1;
                    break;
                case TxoutType::WITNESS_V0_SCRIPTHASH:
                    witness_v0_scripthash_create_count += 1;
                    break;
                case TxoutType::WITNESS_V1_TAPROOT:
                    witness_v1_taproot_create_count += 1;
                    break;
                case TxoutType::WITNESS_UNKNOWN:
                    witness_unknown_create_count += 1;
                    break;
            }

            const unsigned int script_type = GetTxnOutputTypeEnum(which_type);
            output_script_types[script_type][0] += 1;
            output_script_types[script_type][1] += output_size;
            output_script_types[script_type][2] += txout_data.nValue;
            output_script_types[script_type][3] += this_output_legacy_signature_operations;


            if (transaction_data.is_coinbase) {
                coinbase += txout_data.nValue;
            }

            output_data_string_stream << "[";
            output_data_string_stream << output_size << ",";
            output_data_string_stream << txout_data.nValue << ",";
            output_data_string_stream << transaction_data.GetFeeRate() << ",";
            output_data_string_stream << script_type;
            output_data_string_stream << "]";
            if (transaction_index != block.vtx.size() - 1 || output_vector != transaction->vout.size() - 1) {
                output_data_string_stream << ",";
            }

            CTxDestination address;
            std::string address_string = "no-address";
            bool has_address = ExtractDestination(txout_data.scriptPubKey, address);
            if (has_address) {
                address_string = EncodeDestination(address);
            };

            addresses_string_stream << ChainToString() << ","; // network

            addresses_string_stream << ","; // input_height
            addresses_string_stream << ","; // input_median_time
            addresses_string_stream << ","; // input_txid
            addresses_string_stream << ","; // input_wtxid
            addresses_string_stream << ","; // input_vector
            addresses_string_stream << ","; // input_size

            addresses_string_stream << block_index->nHeight << ","; // output_height
            addresses_string_stream << block_index->GetMedianTimePast() << ","; // output_median_time
            addresses_string_stream << transaction->GetHash().GetHex() << ","; // output_txid
            addresses_string_stream << transaction->GetWitnessHash().GetHex() << ","; // output_wtxid
            addresses_string_stream << output_vector << ","; // output_vector
            addresses_string_stream << output_size << ","; // output_size
            addresses_string_stream << script_type << ","; // output_script_type

            addresses_string_stream << address_string << ","; // address
            addresses_string_stream << txout_data.nValue << "\n"; // amount

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

            switch (which_type) {
                case TxoutType::NONSTANDARD:
                    nonstandard_spend_count += 1;
                    break;
                case TxoutType::PUBKEY:
                    pubkey_spend_count += 1;
                    break;
                case TxoutType::PUBKEYHASH:
                    pubkeyhash_spend_count += 1;
                    break;
                case TxoutType::SCRIPTHASH:
                    scripthash_spend_count += 1;
                    break;
                case TxoutType::MULTISIG:
                    multisig_spend_count += 1;
                    break;
                case TxoutType::NULL_DATA:
                    null_data_spend_count += 1;
                    break;
                case TxoutType::WITNESS_V0_KEYHASH:
                    witness_v0_keyhash_spend_count += 1;
                    break;
                case TxoutType::WITNESS_V0_SCRIPTHASH:
                    witness_v0_scripthash_spend_count += 1;
                    break;
                case TxoutType::WITNESS_V1_TAPROOT:
                    witness_v1_taproot_spend_count += 1;
                    break;
                case TxoutType::WITNESS_UNKNOWN:
                    witness_unknown_spend_count += 1;
                    break;
            }

            const unsigned int spent_script_type = GetTxnOutputTypeEnum(which_type);

            uint64_t input_size = GetSerializeSize(txin_data, PROTOCOL_VERSION) +
                                  GetSerializeSize(txin_data.scriptWitness.stack, PROTOCOL_VERSION);
            uint64_t input_weight = GetTransactionInputWeight(txin_data);

            typedef std::vector<unsigned char> valtype;

            uint64_t luke_inscription_filter = 0;

            int witnessversion;
            std::vector<unsigned char> witnessprogram;

            if (spent_output_data.scriptPubKey.IsWitnessProgram(witnessversion, witnessprogram)) {
                Span stack{txin_data.scriptWitness.stack};
                const valtype& script_bytes = SpanPopBack(stack);
                CScript exec_script = CScript(script_bytes.begin(), script_bytes.end());

                CScript::const_iterator pc = exec_script.begin();
                CScript::const_iterator pend = exec_script.end();
                uint32_t opcode_pos = 0;
                for (; pc < pend; ++opcode_pos) {
                    opcodetype opcode;
                    valtype vchPushValue;
                    exec_script.GetOp(pc, opcode, vchPushValue);
                    if (opcode == OP_FALSE) {
                        auto pc_tmp = pc;
                        opcodetype next_opcode;
                        valtype dummy_data;
                        exec_script.GetOp(pc_tmp, next_opcode, dummy_data);
                        if (next_opcode == OP_IF) {
                            luke_inscription_filter = 1;
                            break;
                        }
                    }
                };
            };

            uint64_t the_stack_filter = 0;
            if (spent_output_data.scriptPubKey.IsWitnessProgram(witnessversion, witnessprogram)) {

                Span stack{txin_data.scriptWitness.stack};

                const valtype& script_bytes = SpanPopBack(stack);

                CScript exec_script = CScript(script_bytes.begin(), script_bytes.end());

                CScript::const_iterator pc = exec_script.begin();
                CScript::const_iterator pend = exec_script.end();
                uint32_t opcode_pos = 0;
                for (; pc < pend; ++opcode_pos) {
                    opcodetype opcode;
                    valtype vchPushValue;
                    exec_script.GetOp(pc, opcode, vchPushValue);
                    const valtype ord_prefix{OP_FALSE, OP_IF, 0x03, 'o', 'r', 'd'};
                    if (opcode == ord_prefix[0] &&
                        std::mismatch(
                                ord_prefix.begin()+1,
                                ord_prefix.end(),
                                pc,
                                pend
                                ).first == ord_prefix.end()) {
                        the_stack_filter = 1;
                        break;
                    };
                };
            };

            bool found_ord_prefix = false;
            if (spent_output_data.scriptPubKey.IsWitnessProgram(witnessversion, witnessprogram)) {

                const unsigned char ord_prefix[] = {OP_FALSE, OP_IF, 0x03, 'o', 'r', 'd'};
                // iterate over txin_data.scriptWitness.stack using the length of the ord_prefix
                // to check if any 6 byte array of the scriptWitness stack is the ord_prefix
                for (const auto &item : txin_data.scriptWitness.stack) {
                    if (item.size() >= sizeof(ord_prefix)) {
                        if (std::equal(std::begin(ord_prefix), std::end(ord_prefix), item.begin())) {
                            found_ord_prefix = true;
                            break;
                        }
                    }
                }
            };

            block_inputs_total_size += input_size;

            input_script_types[spent_script_type][0] += 1;
            input_script_types[spent_script_type][1] += input_weight;
            input_script_types[spent_script_type][2] += input_size;
            input_script_types[spent_script_type][3] += spent_output_size;
            input_script_types[spent_script_type][4] += spent_output_data.nValue;
            input_script_types[spent_script_type][5] += this_input_legacy_signature_operations;
            input_script_types[spent_script_type][5] += this_input_p2sh_signature_operations;
            input_script_types[spent_script_type][5] += this_input_witness_signature_operations;

            input_data_string_stream << "[";
            input_data_string_stream << input_weight << ",";
            input_data_string_stream << input_size << ",";
            input_data_string_stream << spent_output_size << ",";
            input_data_string_stream << spent_output_data.nValue << ",";
            input_data_string_stream << spent_script_type << ",";
            input_data_string_stream << luke_inscription_filter << ",";
            input_data_string_stream << the_stack_filter << ",";
            input_data_string_stream << found_ord_prefix;

            input_data_string_stream << "]";
            if (transaction_index != block.vtx.size() - 1 || input_vector != transaction->vin.size() - 1) {
                input_data_string_stream << ",";
            }

            CTxDestination address;
            std::string address_string = "no-address";
            bool has_address = ExtractDestination(spent_output_data.scriptPubKey, address);
            if (has_address) {
                address_string = EncodeDestination(address);
            };

            addresses_string_stream << ChainToString() << ","; // network

            addresses_string_stream << block_index->nHeight << ","; // input_height
            addresses_string_stream << block_index->GetMedianTimePast() << ","; // input_median_time
            addresses_string_stream << transaction->GetHash().GetHex() << ","; // input_txid
            addresses_string_stream << transaction->GetWitnessHash().GetHex() << ","; // input_wtxid
            addresses_string_stream << input_vector << ","; // input_vector
            addresses_string_stream << input_size << ","; // input_size

            addresses_string_stream << coin.nHeight << ","; // output_height
            addresses_string_stream << ","; // output_median_time
            addresses_string_stream << txin_data.prevout.hash.GetHex() << ","; // output_txid
            addresses_string_stream << ","; // output_wtxid
            addresses_string_stream << txin_data.prevout.n << ","; // output_vector
            addresses_string_stream << spent_output_size << ","; // output_size
            addresses_string_stream << spent_script_type << ","; // output_script_type

            addresses_string_stream << address_string << ","; // address
            addresses_string_stream << -spent_output_data.nValue << "\n"; // amount
        }

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

                             "hash_prev_block, "
                             "network, "

                             "nonstandard_create_count           , "
                             "pubkey_create_count                , "
                             "pubkeyhash_create_count            , "
                             "scripthash_create_count            , "
                             "multisig_create_count              , "
                             "null_data_create_count             , "
                             "witness_v0_keyhash_create_count    , "
                             "witness_v0_scripthash_create_count , "
                             "witness_v1_taproot_create_count    , "
                             "witness_unknown_create_count       , "

                             "nonstandard_spend_count            , "
                             "pubkey_spend_count                 , "
                             "pubkeyhash_spend_count             , "
                             "scripthash_spend_count             , "
                             "multisig_spend_count               , "
                             "null_data_spend_count              , "
                             "witness_v0_keyhash_spend_count     , "
                             "witness_v0_scripthash_spend_count  , "
                             "witness_v1_taproot_spend_count     , "
                             "witness_unknown_spend_count        , "
                             "coinbase"

                             ") "

                             "VALUES "
                             "("
                             "$1, " // hash
                             "$2, " // merkle_root
                             "to_timestamp($3), " // time

                             "to_timestamp($4), " // median_time
                             "$5, " // height
                             "$6, " // subsidy

                             "$7, " // transactions_count
                             "$8, " // version
                             "$9, " // status

                             "$10, " // bits
                             "$11, " // nonce
                             "$12, " // difficulty

                             "$13, " // chain_work
                             "$14, " // outputs_count
                             "$15, " // inputs_count

                             "$16, " // total_output_value
                             "$17, " // total_input_value
                             "$18, " // total_fees

                             "$19, " // total_size
                             "$20, " // total_vsize
                             "$21, " // total_weight

                             "$22, " // fee_rates
                             "$23, " // output_data
                             "$24, " // input_data

                             "$25, " // transaction_data
                             "$26, " // output_script_types
                             "$27, " // input_script_types

                             "$28, " // output_legacy_signature_operations
                             "$29, " // input_legacy_signature_operations
                             "$30, " // input_p2sh_signature_operations
                             "$31, " // input_witness_signature_operations

                             "$32, " // outputs_total_size
                             "$33, " // inputs_total_size
                             "$34, " // net_utxo_size_impact

                             "$35, " // hash_prev_block
                             "$36, "   // network

                             "$37, " // nonstandard_create_count
                             "$38, " // pubkey_create_count
                             "$39, " // pubkeyhash_create_count
                             "$40, " // scripthash_create_count
                             "$41, " // multisig_create_count
                             "$42, " // null_data_create_count
                             "$43, " // witness_v0_keyhash_create_count
                             "$44, " // witness_v0_scripthash_create_count
                             "$45, " // witness_v1_taproot_create_count
                             "$46, " // witness_unknown_create_count

                             "$47, " // nonstandard_spend_count
                             "$48, " // pubkey_spend_count
                             "$49, " // pubkeyhash_spend_count
                             "$50, " // scripthash_spend_count
                             "$51, " // multisig_spend_count
                             "$52, " // null_data_spend_count
                             "$53, " // witness_v0_keyhash_spend_count
                             "$54, " // witness_v0_scripthash_spend_count
                             "$55, " // witness_v1_taproot_spend_count
                             "$56, " // witness_unknown_spend_count
                             "$57 "  // coinbase
                             ") ON CONFLICT (hash) DO UPDATE "
                             "SET input_data = EXCLUDED.input_data;"
    );

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
            block.GetBlockHeader().hashPrevBlock.ToString(),

            ChainToString(),                                        // network
            nonstandard_create_count,
            pubkey_create_count,
            pubkeyhash_create_count,
            scripthash_create_count,
            multisig_create_count,
            null_data_create_count,
            witness_v0_keyhash_create_count,
            witness_v0_scripthash_create_count,
            witness_v1_taproot_create_count,
            witness_unknown_create_count,

            nonstandard_spend_count,
            pubkey_spend_count,
            pubkeyhash_spend_count,
            scripthash_spend_count,
            multisig_spend_count,
            null_data_spend_count,
            witness_v0_keyhash_spend_count,
            witness_v0_scripthash_spend_count,
            witness_v1_taproot_spend_count,
            witness_unknown_spend_count,
            coinbase
    )};
    w.commit();

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

}

TransactionData::TransactionData(const int &transaction_index, const CTransactionRef &transaction) :
        m_transaction_index(transaction_index),
        m_transaction(transaction) {
    transaction_hash = transaction->GetHash().GetHex();
    is_coinbase = transaction->IsCoinBase();
    weight = GetTransactionWeight(*transaction);
    vsize = GetVirtualTransactionSize(*transaction);
};
