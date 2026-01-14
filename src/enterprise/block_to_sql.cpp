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
#include <array>
#include <algorithm>
#include <atomic>
#include <iterator>
#include <map>
#include <mutex>
#include <sstream>
#include <unordered_set>
#include <utility>
#include <vector>


#include <enterprise/block_to_sql.h>
#include <enterprise/utilities.h>

#include <enterprise/db.h>
#include <enterprise/dotenv.h>
#include <pqxx/pqxx>


std::string ChainToString() {
    switch (gArgs.GetChainType()) {
        case ChainType::TESTNET:
            return "testnet";
        case ChainType::SIGNET:
            return "signet";
        case ChainType::REGTEST:
            return "regtest";
        case ChainType::MAIN:
            return "mainnet";
    }
    return "unknown";
}

namespace {

std::atomic<int64_t> g_exported_max_height{-1};
std::atomic<int64_t> g_initial_exported_max_height{-1};
std::once_flag g_exported_height_loaded;
std::once_flag g_missing_heights_loaded;
std::mutex g_missing_heights_mutex;
std::unordered_set<int64_t> g_missing_heights;

int64_t DbMaxHeightForNetwork(const std::string& network)
{
    std::call_once(g_exported_height_loaded, [&] {
        try {
            auto& conn = enterprise::PgConnection();
            pqxx::work w(conn);
            const auto res = w.exec(pqxx::zview{"SELECT COALESCE(MAX(height), -1) FROM blocks WHERE network = $1"},
                                    pqxx::params{network});
            const int64_t db_height = res.empty() ? -1 : res[0][0].as<int64_t>();
            g_exported_max_height.store(db_height);
            g_initial_exported_max_height.store(db_height);
            w.commit();
        } catch (const std::exception& e) {
            LogWarning("Enterprise DB worker failed to fetch max block height: %s", e.what());
            g_exported_max_height.store(-1);
            g_initial_exported_max_height.store(-1);
        }
    });
    return g_exported_max_height.load();
}

void LoadMissingBlockHeights(const std::string& network)
{
    std::call_once(g_missing_heights_loaded, [&] {
        try {
            auto& conn = enterprise::PgConnection();
            pqxx::work w(conn);
            const auto res = w.exec(pqxx::zview{
                "SELECT s "
                "FROM generate_series(1, (SELECT COALESCE(MAX(height), 0) FROM blocks WHERE network = $1)) AS s "
                "LEFT JOIN blocks b ON b.height = s AND b.network = $1 "
                "WHERE b.height IS NULL"
            }, pqxx::params{network});
            w.commit();
            std::lock_guard<std::mutex> lock(g_missing_heights_mutex);
            g_missing_heights.reserve(res.size());
            for (const auto& row : res) {
                g_missing_heights.insert(row[0].as<int64_t>());
            }
        } catch (const std::exception& e) {
            LogWarning("Enterprise DB worker failed to fetch missing block heights: %s", e.what());
        }
    });
}

bool IsMissingBlockHeight(int64_t height, const std::string& network)
{
    if (height <= 0) {
        return false;
    }
    LoadMissingBlockHeights(network);
    std::lock_guard<std::mutex> lock(g_missing_heights_mutex);
    return g_missing_heights.find(height) != g_missing_heights.end();
}

bool ShouldExportBlockToSqlInternal(int64_t height, const std::string& network)
{
    LoadMissingBlockHeights(network);
    const int64_t db_max_height = DbMaxHeightForNetwork(network);
    const int64_t initial_max_height = g_initial_exported_max_height.load();
    if (db_max_height < 0 || height > db_max_height) {
        return true;
    }
    if (initial_max_height >= 0 && height > initial_max_height) {
        return true;
    }
    return IsMissingBlockHeight(height, network);
}

void UpdateExportedHeight(int64_t height)
{
    int64_t prev = g_exported_max_height.load();
    while (height > prev && !g_exported_max_height.compare_exchange_weak(prev, height)) {
    }
}

struct BlockInsertData {
    std::string hash;
    std::string merkle_root;
    int64_t time;
    int64_t median_time;
    int64_t height;
    int64_t subsidy;

    int64_t transactions_count;
    int64_t version;
    int64_t status;

    int64_t bits;
    int64_t nonce;
    double difficulty;

    std::string chain_work;
    int64_t outputs_count;
    int64_t inputs_count;

    int64_t total_output_value;
    int64_t total_input_value;
    int64_t total_fees;

    int64_t total_size;
    int64_t total_vsize;
    int64_t total_weight;

    std::string fee_rates;
    std::string output_data;
    std::string input_data;

    std::string transaction_data;
    std::string output_script_types;
    std::string input_script_types;

    int64_t output_legacy_signature_operations;
    int64_t input_legacy_signature_operations;
    int64_t input_p2sh_signature_operations;
    int64_t input_witness_signature_operations;

    int64_t outputs_total_size;
    int64_t inputs_total_size;
    int64_t net_utxo_size_impact;

    std::string hash_prev_block;
    std::string network;

    int64_t nonstandard_create_count;
    int64_t pubkey_create_count;
    int64_t pubkeyhash_create_count;
    int64_t scripthash_create_count;
    int64_t multisig_create_count;
    int64_t null_data_create_count;
    int64_t witness_v0_keyhash_create_count;
    int64_t witness_v0_scripthash_create_count;
    int64_t witness_v1_taproot_create_count;
    int64_t witness_unknown_create_count;

    int64_t nonstandard_spend_count;
    int64_t pubkey_spend_count;
    int64_t pubkeyhash_spend_count;
    int64_t scripthash_spend_count;
    int64_t multisig_spend_count;
    int64_t null_data_spend_count;
    int64_t witness_v0_keyhash_spend_count;
    int64_t witness_v0_scripthash_spend_count;
    int64_t witness_v1_taproot_spend_count;
    int64_t witness_unknown_spend_count;
    int64_t coinbase;

    int64_t ordinals_weight;
    int64_t ordinals_count;
    int64_t ordinals_size;
    int64_t ordinals_vsize;
    int64_t ordinals_fees;

    int64_t non_ordinals_weight;
    int64_t non_ordinals_count;
    int64_t non_ordinals_size;
    int64_t non_ordinals_vsize;
    int64_t non_ordinals_fees;
};

inline void PrepareBlockStatements(pqxx::connection& c)
{
    thread_local bool prepared = false;
    if (prepared) return;
    c.prepare("DeleteBlock", "DELETE FROM blocks WHERE hash = $1;");
    c.prepare("InsertBlock", "INSERT INTO blocks "
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
                             "coinbase , "

                             "ordinals_weight , "
                             "ordinals_count , "
                             "ordinals_size , "
                             "ordinals_vsize , "
                             "ordinals_fees, "

                             "non_ordinals_weight , "
                             "non_ordinals_count , "
                             "non_ordinals_size , "
                             "non_ordinals_vsize , "
                             "non_ordinals_fees"

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
                             "$57, "  // coinbase

                             "$58, "  // ordinals_weight
                             "$59, "  // ordinals_count
                             "$60, "  // ordinals_size
                             "$61, "  // ordinals_vsize
                             "$62, "  // ordinals_fees

                             "$63, "  // non_ordinals_weight
                             "$64, "  // non_ordinals_count
                             "$65, "  // non_ordinals_size
                             "$66, "  // non_ordinals_vsize
                             "$67 "  // non_ordinals_fees

                             ") ON CONFLICT DO NOTHING ;");
    prepared = true;
}

inline void EnqueueBlockInsert(BlockInsertData&& data)
{
    enterprise::DbWorkQueue::Instance().Enqueue([data = std::move(data)](pqxx::connection& conn) mutable {
        PrepareBlockStatements(conn);
        pqxx::work w(conn);
        w.exec(pqxx::prepped{"DeleteBlock"}, pqxx::params{data.hash});
        w.exec(pqxx::prepped{"InsertBlock"}, pqxx::params{
                data.hash,
                data.merkle_root,
                data.time,
                data.median_time,
                data.height,
                data.subsidy,
                data.transactions_count,
                data.version,
                data.status,
                data.bits,
                data.nonce,
                data.difficulty,
                data.chain_work,
                data.outputs_count,
                data.inputs_count,
                data.total_output_value,
                data.total_input_value,
                data.total_fees,
                data.total_size,
                data.total_vsize,
                data.total_weight,
                data.fee_rates,
                data.output_data,
                data.input_data,
                data.transaction_data,
                data.output_script_types,
                data.input_script_types,
                data.output_legacy_signature_operations,
                data.input_legacy_signature_operations,
                data.input_p2sh_signature_operations,
                data.input_witness_signature_operations,
                data.outputs_total_size,
                data.inputs_total_size,
                data.net_utxo_size_impact,
                data.hash_prev_block,
                data.network,
                data.nonstandard_create_count,
                data.pubkey_create_count,
                data.pubkeyhash_create_count,
                data.scripthash_create_count,
                data.multisig_create_count,
                data.null_data_create_count,
                data.witness_v0_keyhash_create_count,
                data.witness_v0_scripthash_create_count,
                data.witness_v1_taproot_create_count,
                data.witness_unknown_create_count,
                data.nonstandard_spend_count,
                data.pubkey_spend_count,
                data.pubkeyhash_spend_count,
                data.scripthash_spend_count,
                data.multisig_spend_count,
                data.null_data_spend_count,
                data.witness_v0_keyhash_spend_count,
                data.witness_v0_scripthash_spend_count,
                data.witness_v1_taproot_spend_count,
                data.witness_unknown_spend_count,
                data.coinbase,
                data.ordinals_weight,
                data.ordinals_count,
                data.ordinals_size,
                data.ordinals_vsize,
                data.ordinals_fees,
                data.non_ordinals_weight,
                data.non_ordinals_count,
                data.non_ordinals_size,
                data.non_ordinals_vsize,
                data.non_ordinals_fees});
        w.commit();
    });
}

} // namespace

bool ShouldExportBlockToSql(int64_t height)
{
    return ShouldExportBlockToSqlInternal(height, ChainToString());
}


BlockToSql::BlockToSql(CBlockIndex *block_index, const CBlock &block, CCoinsViewCache &view, script_verify_flags flags,
                       CCoinsViewCursor *cursor) {
    static constexpr size_t PER_UTXO_OVERHEAD = sizeof(COutPoint) + sizeof(uint32_t) + sizeof(bool);
    const std::string network = ChainToString();
    if (!ShouldExportBlockToSqlInternal(block_index->nHeight, network)) {
        // Skip exporting blocks that already exist in the database.
        return;
    }

    std::map<CAmount, unsigned int> fee_rates;

    std::map<unsigned int, std::array<uint64_t, 4>> output_script_types;
    std::map<unsigned int, std::array<uint64_t, 7>> input_script_types;

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

    unsigned int ordinals_weight = 0;
    unsigned int ordinals_count = 0;
    unsigned int ordinals_size = 0;
    unsigned int ordinals_vsize = 0;
    CAmount ordinals_fees = 0;

    unsigned int non_ordinals_weight = 0;
    unsigned int non_ordinals_count = 0;
    unsigned int non_ordinals_size = 0;
    unsigned int non_ordinals_vsize = 0;
    CAmount non_ordinals_fees = 0;

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

        bool transaction_found_ordinal_prefix = false;

        // Outputs
        for (std::size_t output_vector = 0; output_vector < transaction->vout.size(); ++output_vector) {
            const CTxOut &txout_data = transaction->vout[output_vector];
            int64_t output_size = GetSerializeSize(txout_data);
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
                    if (txin_data.prevout.hash == transaction->GetHash()) {
                        spent_output_transaction = transaction;
                    }
                }
                spent_output_data = spent_output_transaction->vout[txin_data.prevout.n];
            }

            int64_t this_input_legacy_signature_operations =
                    txin_data.scriptSig.GetSigOpCount(false) * WITNESS_SCALE_FACTOR;
            block_input_legacy_signature_operations += this_input_legacy_signature_operations;

            int64_t this_input_p2sh_signature_operations = 0;
            if ((flags & SCRIPT_VERIFY_P2SH) && spent_output_data.scriptPubKey.IsPayToScriptHash()) {
                this_input_p2sh_signature_operations = spent_output_data.scriptPubKey.GetSigOpCount(
                        txin_data.scriptSig) * WITNESS_SCALE_FACTOR;
            }
            block_input_p2sh_signature_operations += this_input_p2sh_signature_operations;


            int64_t this_input_witness_signature_operations = 0;
            this_input_witness_signature_operations += CountWitnessSigOps(txin_data.scriptSig,
                                                                          spent_output_data.scriptPubKey,
                                                                          txin_data.scriptWitness,
                                                                          flags);
            block_input_witness_signature_operations += this_input_witness_signature_operations;

            unsigned int spent_output_size = GetSerializeSize(spent_output_data);

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

            uint64_t input_size = GetSerializeSize(txin_data) +
                                  GetSerializeSize(txin_data.scriptWitness.stack);
            uint64_t input_witness_size = GetSerializeSize(txin_data.scriptWitness.stack);
            uint64_t input_weight = GetTransactionInputWeight(txin_data);

            bool input_found_ord_prefix = false;

            int witnessversion;
            std::vector<unsigned char> witnessprogram;
            if (spent_output_data.scriptPubKey.IsWitnessProgram(witnessversion, witnessprogram)) {
                for (const std::vector<unsigned char> &script_bytes: txin_data.scriptWitness.stack) {
                    CScript exec_script = CScript(script_bytes.begin(), script_bytes.end());
                    const std::string exec_script_string = ScriptToAsmStr(exec_script);
                    if (exec_script_string.find("0 OP_IF 6582895 1") != std::string::npos) {
                        input_found_ord_prefix = true;
                        transaction_found_ordinal_prefix = true;
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
            input_script_types[spent_script_type][6] += input_witness_size;

            input_data_string_stream << "[";
            input_data_string_stream << input_weight << ",";
            input_data_string_stream << input_size << ",";
            input_data_string_stream << spent_output_size << ",";
            input_data_string_stream << spent_output_data.nValue << ",";
            input_data_string_stream << spent_script_type << ",";
            input_data_string_stream << input_found_ord_prefix;

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
        transaction_data_string_stream << transaction_data.GetFee() << ",";
        transaction_data_string_stream << transaction_found_ordinal_prefix;
        transaction_data_string_stream << "]";
        if (transaction_index != block.vtx.size() - 1) {
            transaction_data_string_stream << ",";
        }

        CAmount fee_rate = transaction_data.GetFee() / transaction_data.vsize;
        fee_rates[fee_rate] += transaction_data.weight;

        if (transaction_found_ordinal_prefix) {
            ordinals_weight += transaction_data.weight;
            ordinals_count += 1;
            ordinals_size += transaction_data.m_transaction->GetTotalSize();
            ordinals_vsize += transaction_data.vsize;
            ordinals_fees += transaction_data.GetFee();
        } else {
            non_ordinals_weight += transaction_data.weight;
            non_ordinals_count += 1;
            non_ordinals_size += transaction_data.m_transaction->GetTotalSize();
            non_ordinals_vsize += transaction_data.vsize;
            non_ordinals_fees += transaction_data.GetFee();
        };
    }

    std::ostringstream fee_rates_string_stream;
    fee_rates_string_stream << "[";
    for (auto it = fee_rates.begin(); it != fee_rates.end(); ++it) {
        fee_rates_string_stream << "[";
        fee_rates_string_stream << it->first;
        fee_rates_string_stream << ",";
        fee_rates_string_stream << it->second;
        fee_rates_string_stream << "]";
        if ((it != fee_rates.end()) && (std::next(it) == fee_rates.end())) continue;
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
        if ((it != output_script_types.end()) && (std::next(it) == output_script_types.end())) continue;
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
        if ((it != input_script_types.end()) && (std::next(it) == input_script_types.end())) continue;
        input_script_types_string_stream << ",";
    }

    fee_rates_string_stream << "]";
    output_data_string_stream << "]";
    input_data_string_stream << "]";
    transaction_data_string_stream << "]";
    input_script_types_string_stream << "]";
    output_script_types_string_stream << "]";

    BlockInsertData data{
            block.GetBlockHeader().GetHash().GetHex(),
            block_index->hashMerkleRoot.GetHex(),
            block_index->GetBlockTime(),
            block_index->GetMedianTimePast(),
            block_index->nHeight,
            GetBlockSubsidy(block_index->nHeight, Params().GetConsensus()),
            static_cast<int64_t>(block_index->nTx),
            static_cast<int64_t>(block_index->nVersion),
            static_cast<int64_t>(block_index->nStatus),
            static_cast<int64_t>(block_index->nBits),
            static_cast<int64_t>(block_index->nNonce),
            GetDifficulty(*block_index),
            block_index->nChainWork.GetHex(),
            static_cast<int64_t>(outputs_count),
            static_cast<int64_t>(inputs_count),
            total_output_value,
            total_input_value,
            total_fees,
            static_cast<int64_t>(GetSerializeSize(TX_WITH_WITNESS(block))),
            static_cast<int64_t>(GetBlockWeight(block) / WITNESS_SCALE_FACTOR),
            static_cast<int64_t>(GetBlockWeight(block)),
            fee_rates_string_stream.str(),
            output_data_string_stream.str(),
            input_data_string_stream.str(),
            transaction_data_string_stream.str(),
            output_script_types_string_stream.str(),
            input_script_types_string_stream.str(),
            static_cast<int64_t>(block_output_legacy_signature_operations),
            static_cast<int64_t>(block_input_legacy_signature_operations),
            static_cast<int64_t>(block_input_p2sh_signature_operations),
            static_cast<int64_t>(block_input_witness_signature_operations),
            static_cast<int64_t>(block_outputs_total_size),
            static_cast<int64_t>(block_inputs_total_size),
            static_cast<int64_t>(block_net_utxo_size_impact),
            block.GetBlockHeader().hashPrevBlock.ToString(),
            network,
            static_cast<int64_t>(nonstandard_create_count),
            static_cast<int64_t>(pubkey_create_count),
            static_cast<int64_t>(pubkeyhash_create_count),
            static_cast<int64_t>(scripthash_create_count),
            static_cast<int64_t>(multisig_create_count),
            static_cast<int64_t>(null_data_create_count),
            static_cast<int64_t>(witness_v0_keyhash_create_count),
            static_cast<int64_t>(witness_v0_scripthash_create_count),
            static_cast<int64_t>(witness_v1_taproot_create_count),
            static_cast<int64_t>(witness_unknown_create_count),
            static_cast<int64_t>(nonstandard_spend_count),
            static_cast<int64_t>(pubkey_spend_count),
            static_cast<int64_t>(pubkeyhash_spend_count),
            static_cast<int64_t>(scripthash_spend_count),
            static_cast<int64_t>(multisig_spend_count),
            static_cast<int64_t>(null_data_spend_count),
            static_cast<int64_t>(witness_v0_keyhash_spend_count),
            static_cast<int64_t>(witness_v0_scripthash_spend_count),
            static_cast<int64_t>(witness_v1_taproot_spend_count),
            static_cast<int64_t>(witness_unknown_spend_count),
            static_cast<int64_t>(coinbase),
            static_cast<int64_t>(ordinals_weight),
            static_cast<int64_t>(ordinals_count),
            static_cast<int64_t>(ordinals_size),
            static_cast<int64_t>(ordinals_vsize),
            static_cast<int64_t>(ordinals_fees),
            static_cast<int64_t>(non_ordinals_weight),
            static_cast<int64_t>(non_ordinals_count),
            static_cast<int64_t>(non_ordinals_size),
            static_cast<int64_t>(non_ordinals_vsize),
            static_cast<int64_t>(non_ordinals_fees)
    };
    EnqueueBlockInsert(std::move(data));
    UpdateExportedHeight(block_index->nHeight);
}

TransactionData::TransactionData(std::size_t transaction_index, const CTransactionRef &transaction) :
        m_transaction_index(transaction_index),
        m_transaction(transaction) {
    transaction_hash = transaction->GetHash().GetHex();
    is_coinbase = transaction->IsCoinBase();
    weight = GetTransactionWeight(*transaction);
    vsize = GetVirtualTransactionSize(*transaction);
};
