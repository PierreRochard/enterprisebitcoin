#include <enterprise/block_to_sql.h>

#include <addresstype.h>
#include <chain.h>
#include <chainparams.h>
#include <common/system.h>
#include <consensus/validation.h>
#include <core_io.h>
#include <deploymentinfo.h>
#include <deploymentstatus.h>
#include <enterprise/common.h>
#include <enterprise/db.h>
#include <enterprise/utilities.h>
#include <key_io.h>
#include <logging.h>
#include <node/transaction.h>
#include <primitives/block.h>
#include <pubkey.h>
#include <rpc/blockchain.h>
#include <script/interpreter.h>
#include <script/script.h>
#include <serialize.h>
#include <txmempool.h>
#include <undo.h>
#include <util/time.h>
#include <validation.h>
#include <versionbits.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <iterator>
#include <map>
#include <optional>
#include <pqxx/pqxx>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

struct AddressFlowRow {
    std::string network;
    std::string block_hash;

    std::optional<int64_t> input_height;
    std::optional<std::string> input_median_time;
    std::optional<std::string> input_txid;
    std::optional<std::string> input_wtxid;
    std::optional<int64_t> input_vector;
    std::optional<int64_t> input_size;

    int64_t output_height;
    std::optional<std::string> output_median_time;
    std::string output_txid;
    std::optional<std::string> output_wtxid;
    int64_t output_vector;
    int64_t output_size;
    int64_t output_script_type;

    std::optional<std::string> address;
    int64_t amount;
};

struct FeeRateSummary {
    double min{0.0};
    double median{0.0};
    double p90{0.0};
    double max{0.0};
};

struct VersionBitsExportData {
    bool top_bits_valid{false};
    std::string signalling_json{"{}"};
    std::string unknown_bits_json{"[]"};
};

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

    int64_t spent_age_blocks_sum;
    double avg_spent_age_blocks;
    double coinblocks_destroyed;
    double coindays_destroyed;

    double min_fee_rate;
    double median_fee_rate;
    double p90_fee_rate;
    double max_fee_rate;

    int64_t inputs_total_witness_size;
    int64_t taproot_inputs_total_witness_size;
    int64_t taproot_key_path_spend_count;
    int64_t taproot_script_path_spend_count;
    int64_t taproot_annex_spend_count;
    int64_t tapscript_spend_count;

    int64_t coinbase_script_sig_size;
    int64_t coinbase_witness_stack_items;
    int64_t coinbase_witness_size;
    int64_t coinbase_outputs_count;
    bool has_witness_commitment;
    std::optional<int64_t> witness_commitment_index;
    std::optional<std::string> coinbase_tag;

    bool version_bits_top_bits_valid;
    std::string version_bits_signalling;
    std::string unknown_version_bits;
};

template <typename Value>
void AppendInsertField(std::ostringstream& columns,
                       std::ostringstream& values,
                       pqxx::params& params,
                       pqxx::placeholders<>& placeholders,
                       bool& first,
                       std::string_view column,
                       const Value& value)
{
    if (!first) {
        columns << ", ";
        values << ", ";
    }

    columns << column;
    values << placeholders.get();
    params.append(value);
    placeholders.next();
    first = false;
}

template <typename Value>
void AppendInsertTimestampField(std::ostringstream& columns,
                                std::ostringstream& values,
                                pqxx::params& params,
                                pqxx::placeholders<>& placeholders,
                                bool& first,
                                std::string_view column,
                                const Value& value)
{
    if (!first) {
        columns << ", ";
        values << ", ";
    }

    const std::string placeholder = placeholders.get();
    columns << column;
    values << "to_timestamp(" << placeholder << ")";
    params.append(value);
    placeholders.next();
    first = false;
}

std::optional<std::string> ExtractAddressString(const CScript& script_pub_key)
{
    CTxDestination address;
    if (!ExtractDestination(script_pub_key, address)) {
        return std::nullopt;
    }

    return EncodeDestination(address);
}

std::string FormatPgTimestamp(int64_t time)
{
    return FormatISO8601DateTime(time);
}

std::optional<std::string> GetMedianTimePastString(const CBlockIndex* block_index)
{
    if (block_index == nullptr) {
        return std::nullopt;
    }

    return FormatPgTimestamp(block_index->GetMedianTimePast());
}

std::string JsonEscape(std::string_view input)
{
    std::ostringstream escaped;
    for (const unsigned char ch : input) {
        switch (ch) {
        case '"':
            escaped << "\\\"";
            break;
        case '\\':
            escaped << "\\\\";
            break;
        case '\b':
            escaped << "\\b";
            break;
        case '\f':
            escaped << "\\f";
            break;
        case '\n':
            escaped << "\\n";
            break;
        case '\r':
            escaped << "\\r";
            break;
        case '\t':
            escaped << "\\t";
            break;
        default:
            if (ch < 0x20) {
                escaped << "\\u00";
                constexpr char HEX[] = "0123456789abcdef";
                escaped << HEX[(ch >> 4) & 0x0f] << HEX[ch & 0x0f];
            } else {
                escaped << static_cast<char>(ch);
            }
            break;
        }
    }
    return escaped.str();
}

std::string TrimAsciiWhitespace(std::string value)
{
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
        value.erase(value.begin());
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }
    return value;
}

std::optional<std::string> ExtractCoinbaseTag(const CScript& script_sig)
{
    std::string printable;
    printable.reserve(script_sig.size());
    for (const unsigned char ch : script_sig) {
        printable.push_back((ch >= 0x20 && ch <= 0x7e) ? static_cast<char>(ch) : ' ');
    }

    std::string best_delimited;
    for (size_t start = printable.find('/'); start != std::string::npos; start = printable.find('/', start + 1)) {
        const size_t end = printable.find('/', start + 1);
        if (end == std::string::npos) {
            break;
        }
        const std::string candidate = TrimAsciiWhitespace(printable.substr(start, end - start + 1));
        if (candidate.size() > best_delimited.size()) {
            best_delimited = candidate;
        }
        start = end;
    }
    if (best_delimited.size() >= 4) {
        return best_delimited;
    }

    std::string best_run;
    std::string current_run;
    for (const char ch : printable) {
        if (std::isalnum(static_cast<unsigned char>(ch)) || ch == ' ' || ch == '/' || ch == '-' || ch == '_' || ch == '.' || ch == ':') {
            current_run.push_back(ch);
            continue;
        }

        current_run = TrimAsciiWhitespace(std::move(current_run));
        if (current_run.size() > best_run.size()) {
            best_run = current_run;
        }
        current_run.clear();
    }

    current_run = TrimAsciiWhitespace(std::move(current_run));
    if (current_run.size() > best_run.size()) {
        best_run = current_run;
    }

    if (best_run.size() < 4) {
        return std::nullopt;
    }

    return best_run;
}

double WeightedPercentile(std::vector<std::pair<double, uint64_t>> values, double percentile)
{
    if (values.empty()) {
        return 0.0;
    }

    std::sort(values.begin(), values.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.first == rhs.first) {
            return lhs.second < rhs.second;
        }
        return lhs.first < rhs.first;
    });

    uint64_t total_weight = 0;
    for (const auto& [fee_rate, weight] : values) {
        (void)fee_rate;
        total_weight += weight;
    }

    if (total_weight == 0) {
        return values.front().first;
    }

    const double threshold = percentile * static_cast<double>(total_weight);
    uint64_t cumulative_weight = 0;
    for (const auto& [fee_rate, weight] : values) {
        cumulative_weight += weight;
        if (static_cast<double>(cumulative_weight) >= threshold) {
            return fee_rate;
        }
    }

    return values.back().first;
}

FeeRateSummary SummarizeFeeRates(const std::vector<std::pair<double, uint64_t>>& fee_rate_samples)
{
    FeeRateSummary summary;
    if (fee_rate_samples.empty()) {
        return summary;
    }

    auto sorted_samples = fee_rate_samples;
    std::sort(sorted_samples.begin(), sorted_samples.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.first == rhs.first) {
            return lhs.second < rhs.second;
        }
        return lhs.first < rhs.first;
    });

    summary.min = sorted_samples.front().first;
    summary.max = sorted_samples.back().first;
    summary.median = WeightedPercentile(sorted_samples, 0.50);
    summary.p90 = WeightedPercentile(sorted_samples, 0.90);
    return summary;
}

VersionBitsExportData BuildVersionBitsExportData(int32_t version)
{
    VersionBitsExportData export_data;
    const uint32_t version_u = static_cast<uint32_t>(version);
    export_data.top_bits_valid = (version_u & static_cast<uint32_t>(VERSIONBITS_TOP_MASK)) == static_cast<uint32_t>(VERSIONBITS_TOP_BITS);

    const Consensus::Params& consensus = Params().GetConsensus();
    std::array<bool, VERSIONBITS_NUM_BITS> known_bits{};

    std::ostringstream signalling_json;
    signalling_json << "{";
    bool first = true;
    for (int deployment = 0; deployment < Consensus::MAX_VERSION_BITS_DEPLOYMENTS; ++deployment) {
        const auto position = static_cast<Consensus::DeploymentPos>(deployment);
        if (!DeploymentEnabled(consensus, position)) {
            continue;
        }

        const int bit = consensus.vDeployments[position].bit;
        if (bit < 0 || bit >= VERSIONBITS_NUM_BITS) {
            continue;
        }

        known_bits[bit] = true;
        if (!first) {
            signalling_json << ",";
        }
        first = false;

        const bool signalled = export_data.top_bits_valid && ((version_u & (uint32_t{1} << bit)) != 0);
        signalling_json << "\"" << JsonEscape(DeploymentName(position)) << "\":{";
        signalling_json << "\"bit\":" << bit << ",";
        signalling_json << "\"signalled\":" << (signalled ? "true" : "false");
        signalling_json << "}";
    }
    signalling_json << "}";
    export_data.signalling_json = signalling_json.str();

    std::ostringstream unknown_bits_json;
    unknown_bits_json << "[";
    bool first_unknown = true;
    if (export_data.top_bits_valid) {
        for (int bit = 0; bit < VERSIONBITS_NUM_BITS; ++bit) {
            if ((version_u & (uint32_t{1} << bit)) == 0 || known_bits[bit]) {
                continue;
            }
            if (!first_unknown) {
                unknown_bits_json << ",";
            }
            first_unknown = false;
            unknown_bits_json << bit;
        }
    }
    unknown_bits_json << "]";
    export_data.unknown_bits_json = unknown_bits_json.str();

    return export_data;
}

void WriteBlockRow(pqxx::connection& conn, const BlockInsertData& data, const std::vector<AddressFlowRow>& address_flows)
{
    pqxx::work w(conn);
    w.exec(pqxx::zview{"DELETE FROM blocks WHERE hash = $1"}, pqxx::params{data.hash});
    w.exec(pqxx::zview{"DELETE FROM blocks WHERE network = $1 AND height = $2"},
           pqxx::params{data.network, data.height});

    pqxx::params params;
    params.reserve(96);
    pqxx::placeholders<> placeholders;
    std::ostringstream columns;
    std::ostringstream values;
    bool first = true;

    AppendInsertField(columns, values, params, placeholders, first, "hash", data.hash);
    AppendInsertField(columns, values, params, placeholders, first, "merkle_root", data.merkle_root);
    AppendInsertTimestampField(columns, values, params, placeholders, first, "time", data.time);
    AppendInsertTimestampField(columns, values, params, placeholders, first, "median_time", data.median_time);
    AppendInsertField(columns, values, params, placeholders, first, "height", data.height);
    AppendInsertField(columns, values, params, placeholders, first, "subsidy", data.subsidy);
    AppendInsertField(columns, values, params, placeholders, first, "transactions_count", data.transactions_count);
    AppendInsertField(columns, values, params, placeholders, first, "version", data.version);
    AppendInsertField(columns, values, params, placeholders, first, "status", data.status);
    AppendInsertField(columns, values, params, placeholders, first, "bits", data.bits);
    AppendInsertField(columns, values, params, placeholders, first, "nonce", data.nonce);
    AppendInsertField(columns, values, params, placeholders, first, "difficulty", data.difficulty);
    AppendInsertField(columns, values, params, placeholders, first, "chain_work", data.chain_work);
    AppendInsertField(columns, values, params, placeholders, first, "outputs_count", data.outputs_count);
    AppendInsertField(columns, values, params, placeholders, first, "inputs_count", data.inputs_count);
    AppendInsertField(columns, values, params, placeholders, first, "total_output_value", data.total_output_value);
    AppendInsertField(columns, values, params, placeholders, first, "total_input_value", data.total_input_value);
    AppendInsertField(columns, values, params, placeholders, first, "total_fees", data.total_fees);
    AppendInsertField(columns, values, params, placeholders, first, "total_size", data.total_size);
    AppendInsertField(columns, values, params, placeholders, first, "total_vsize", data.total_vsize);
    AppendInsertField(columns, values, params, placeholders, first, "total_weight", data.total_weight);
    AppendInsertField(columns, values, params, placeholders, first, "fee_rates", data.fee_rates);
    AppendInsertField(columns, values, params, placeholders, first, "output_data", data.output_data);
    AppendInsertField(columns, values, params, placeholders, first, "input_data", data.input_data);
    AppendInsertField(columns, values, params, placeholders, first, "transaction_data", data.transaction_data);
    AppendInsertField(columns, values, params, placeholders, first, "output_script_types", data.output_script_types);
    AppendInsertField(columns, values, params, placeholders, first, "input_script_types", data.input_script_types);
    AppendInsertField(columns, values, params, placeholders, first, "output_legacy_signature_operations", data.output_legacy_signature_operations);
    AppendInsertField(columns, values, params, placeholders, first, "input_legacy_signature_operations", data.input_legacy_signature_operations);
    AppendInsertField(columns, values, params, placeholders, first, "input_p2sh_signature_operations", data.input_p2sh_signature_operations);
    AppendInsertField(columns, values, params, placeholders, first, "input_witness_signature_operations", data.input_witness_signature_operations);
    AppendInsertField(columns, values, params, placeholders, first, "outputs_total_size", data.outputs_total_size);
    AppendInsertField(columns, values, params, placeholders, first, "inputs_total_size", data.inputs_total_size);
    AppendInsertField(columns, values, params, placeholders, first, "net_utxo_size_impact", data.net_utxo_size_impact);
    AppendInsertField(columns, values, params, placeholders, first, "hash_prev_block", data.hash_prev_block);
    AppendInsertField(columns, values, params, placeholders, first, "network", data.network);
    AppendInsertField(columns, values, params, placeholders, first, "nonstandard_create_count", data.nonstandard_create_count);
    AppendInsertField(columns, values, params, placeholders, first, "pubkey_create_count", data.pubkey_create_count);
    AppendInsertField(columns, values, params, placeholders, first, "pubkeyhash_create_count", data.pubkeyhash_create_count);
    AppendInsertField(columns, values, params, placeholders, first, "scripthash_create_count", data.scripthash_create_count);
    AppendInsertField(columns, values, params, placeholders, first, "multisig_create_count", data.multisig_create_count);
    AppendInsertField(columns, values, params, placeholders, first, "null_data_create_count", data.null_data_create_count);
    AppendInsertField(columns, values, params, placeholders, first, "witness_v0_keyhash_create_count", data.witness_v0_keyhash_create_count);
    AppendInsertField(columns, values, params, placeholders, first, "witness_v0_scripthash_create_count", data.witness_v0_scripthash_create_count);
    AppendInsertField(columns, values, params, placeholders, first, "witness_v1_taproot_create_count", data.witness_v1_taproot_create_count);
    AppendInsertField(columns, values, params, placeholders, first, "witness_unknown_create_count", data.witness_unknown_create_count);
    AppendInsertField(columns, values, params, placeholders, first, "nonstandard_spend_count", data.nonstandard_spend_count);
    AppendInsertField(columns, values, params, placeholders, first, "pubkey_spend_count", data.pubkey_spend_count);
    AppendInsertField(columns, values, params, placeholders, first, "pubkeyhash_spend_count", data.pubkeyhash_spend_count);
    AppendInsertField(columns, values, params, placeholders, first, "scripthash_spend_count", data.scripthash_spend_count);
    AppendInsertField(columns, values, params, placeholders, first, "multisig_spend_count", data.multisig_spend_count);
    AppendInsertField(columns, values, params, placeholders, first, "null_data_spend_count", data.null_data_spend_count);
    AppendInsertField(columns, values, params, placeholders, first, "witness_v0_keyhash_spend_count", data.witness_v0_keyhash_spend_count);
    AppendInsertField(columns, values, params, placeholders, first, "witness_v0_scripthash_spend_count", data.witness_v0_scripthash_spend_count);
    AppendInsertField(columns, values, params, placeholders, first, "witness_v1_taproot_spend_count", data.witness_v1_taproot_spend_count);
    AppendInsertField(columns, values, params, placeholders, first, "witness_unknown_spend_count", data.witness_unknown_spend_count);
    AppendInsertField(columns, values, params, placeholders, first, "coinbase", data.coinbase);
    AppendInsertField(columns, values, params, placeholders, first, "ordinals_weight", data.ordinals_weight);
    AppendInsertField(columns, values, params, placeholders, first, "ordinals_count", data.ordinals_count);
    AppendInsertField(columns, values, params, placeholders, first, "ordinals_size", data.ordinals_size);
    AppendInsertField(columns, values, params, placeholders, first, "ordinals_vsize", data.ordinals_vsize);
    AppendInsertField(columns, values, params, placeholders, first, "ordinals_fees", data.ordinals_fees);
    AppendInsertField(columns, values, params, placeholders, first, "non_ordinals_weight", data.non_ordinals_weight);
    AppendInsertField(columns, values, params, placeholders, first, "non_ordinals_count", data.non_ordinals_count);
    AppendInsertField(columns, values, params, placeholders, first, "non_ordinals_size", data.non_ordinals_size);
    AppendInsertField(columns, values, params, placeholders, first, "non_ordinals_vsize", data.non_ordinals_vsize);
    AppendInsertField(columns, values, params, placeholders, first, "non_ordinals_fees", data.non_ordinals_fees);
    AppendInsertField(columns, values, params, placeholders, first, "spent_age_blocks_sum", data.spent_age_blocks_sum);
    AppendInsertField(columns, values, params, placeholders, first, "avg_spent_age_blocks", data.avg_spent_age_blocks);
    AppendInsertField(columns, values, params, placeholders, first, "coinblocks_destroyed", data.coinblocks_destroyed);
    AppendInsertField(columns, values, params, placeholders, first, "coindays_destroyed", data.coindays_destroyed);
    AppendInsertField(columns, values, params, placeholders, first, "min_fee_rate", data.min_fee_rate);
    AppendInsertField(columns, values, params, placeholders, first, "median_fee_rate", data.median_fee_rate);
    AppendInsertField(columns, values, params, placeholders, first, "p90_fee_rate", data.p90_fee_rate);
    AppendInsertField(columns, values, params, placeholders, first, "max_fee_rate", data.max_fee_rate);
    AppendInsertField(columns, values, params, placeholders, first, "inputs_total_witness_size", data.inputs_total_witness_size);
    AppendInsertField(columns, values, params, placeholders, first, "taproot_inputs_total_witness_size", data.taproot_inputs_total_witness_size);
    AppendInsertField(columns, values, params, placeholders, first, "taproot_key_path_spend_count", data.taproot_key_path_spend_count);
    AppendInsertField(columns, values, params, placeholders, first, "taproot_script_path_spend_count", data.taproot_script_path_spend_count);
    AppendInsertField(columns, values, params, placeholders, first, "taproot_annex_spend_count", data.taproot_annex_spend_count);
    AppendInsertField(columns, values, params, placeholders, first, "tapscript_spend_count", data.tapscript_spend_count);
    AppendInsertField(columns, values, params, placeholders, first, "coinbase_script_sig_size", data.coinbase_script_sig_size);
    AppendInsertField(columns, values, params, placeholders, first, "coinbase_witness_stack_items", data.coinbase_witness_stack_items);
    AppendInsertField(columns, values, params, placeholders, first, "coinbase_witness_size", data.coinbase_witness_size);
    AppendInsertField(columns, values, params, placeholders, first, "coinbase_outputs_count", data.coinbase_outputs_count);
    AppendInsertField(columns, values, params, placeholders, first, "has_witness_commitment", data.has_witness_commitment);
    AppendInsertField(columns, values, params, placeholders, first, "witness_commitment_index", data.witness_commitment_index);
    AppendInsertField(columns, values, params, placeholders, first, "coinbase_tag", data.coinbase_tag);
    AppendInsertField(columns, values, params, placeholders, first, "version_bits_top_bits_valid", data.version_bits_top_bits_valid);
    AppendInsertField(columns, values, params, placeholders, first, "version_bits_signalling", data.version_bits_signalling);
    AppendInsertField(columns, values, params, placeholders, first, "unknown_version_bits", data.unknown_version_bits);

    w.exec("INSERT INTO blocks (" + columns.str() + ") VALUES (" + values.str() + ")", params);

    if (!address_flows.empty()) {
        auto stream = pqxx::stream_to::table(
            w,
            {"block_address_flows"},
            {"network", "block_hash", "input_height", "input_median_time", "input_txid", "input_wtxid",
             "input_vector", "input_size", "output_height", "output_median_time", "output_txid",
             "output_wtxid", "output_vector", "output_size", "output_script_type", "address", "amount"});
        for (const auto& row : address_flows) {
            stream.write_values(row.network,
                                row.block_hash,
                                row.input_height,
                                row.input_median_time,
                                row.input_txid,
                                row.input_wtxid,
                                row.input_vector,
                                row.input_size,
                                row.output_height,
                                row.output_median_time,
                                row.output_txid,
                                row.output_wtxid,
                                row.output_vector,
                                row.output_size,
                                row.output_script_type,
                                row.address,
                                row.amount);
        }
        stream.complete();
    }

    w.commit();
}

} // namespace

void RemoveBlockFromSql(std::string_view network, const uint256& block_hash)
{
    auto& conn = enterprise::PgConnection();
    pqxx::work w(conn);
    w.exec(pqxx::zview{"DELETE FROM blocks WHERE network = $1 AND hash = $2"},
           pqxx::params{std::string(network), block_hash.GetHex()});
    w.commit();
}

BlockToSql::BlockToSql(const interfaces::BlockInfo& block_info, const CBlockIndex& block_index, script_verify_flags flags)
{
    static constexpr size_t PER_UTXO_OVERHEAD = sizeof(COutPoint) + sizeof(uint32_t) + sizeof(bool);
    assert(block_info.data != nullptr);
    const CBlock& block = *block_info.data;
    const std::string network = enterprise::ChainName();
    const std::string block_hash = block.GetHash().GetHex();
    const int64_t block_median_time = block_index.GetMedianTimePast();
    const std::string block_median_time_text = FormatPgTimestamp(block_median_time);
    const CTransaction& coinbase_tx = *block.vtx.front();

    std::map<CAmount, unsigned int> fee_rates;
    std::vector<std::pair<double, uint64_t>> fee_rate_samples;
    fee_rate_samples.reserve(block.vtx.size() > 0 ? block.vtx.size() - 1 : 0);

    std::unordered_map<std::string, std::string> block_wtxids;
    block_wtxids.reserve(block.vtx.size());
    size_t address_flow_reserve = 0;
    for (const CTransactionRef& transaction : block.vtx) {
        block_wtxids.emplace(transaction->GetHash().GetHex(), transaction->GetWitnessHash().GetHex());
        address_flow_reserve += transaction->vout.size();
        if (!transaction->IsCoinBase()) {
            address_flow_reserve += transaction->vin.size();
        }
    }

    std::vector<AddressFlowRow> address_flows;
    address_flows.reserve(address_flow_reserve);

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
    uint64_t block_inputs_total_witness_size = 0;
    uint64_t taproot_inputs_total_witness_size = 0;

    int64_t block_net_utxo_size_impact = 0;
    int64_t spent_age_blocks_sum = 0;
    uint64_t spent_inputs_count = 0;
    double coinblocks_destroyed = 0.0;
    double coindays_destroyed = 0.0;

    uint64_t taproot_key_path_spend_count = 0;
    uint64_t taproot_script_path_spend_count = 0;
    uint64_t taproot_annex_spend_count = 0;
    uint64_t tapscript_spend_count = 0;

    std::ostringstream output_data_string_stream;
    output_data_string_stream << "[";

    std::ostringstream input_data_string_stream;
    input_data_string_stream << "[";

    std::ostringstream transaction_data_string_stream;
    transaction_data_string_stream << "[";

    for (std::size_t transaction_index = 0; transaction_index < block.vtx.size(); ++transaction_index) {
        const CTransactionRef& transaction = block.vtx[transaction_index];
        TransactionData transaction_data{transaction_index, transaction};

        bool transaction_found_ordinal_prefix = false;

        for (std::size_t input_vector = 0; input_vector < transaction->vin.size(); ++input_vector) {
            if (transaction_data.is_coinbase) {
                continue;
            }

            const CTxIn& txin_data = transaction->vin[input_vector];
            assert(block_info.undo_data != nullptr);
            const Coin& coin = block_info.undo_data->vtxundo.at(transaction_index - 1).vprevout.at(input_vector);
            const CTxOut& spent_output_data = coin.out;

            int64_t this_input_legacy_signature_operations =
                txin_data.scriptSig.GetSigOpCount(false) * WITNESS_SCALE_FACTOR;
            block_input_legacy_signature_operations += this_input_legacy_signature_operations;

            int64_t this_input_p2sh_signature_operations = 0;
            if ((flags & SCRIPT_VERIFY_P2SH) && spent_output_data.scriptPubKey.IsPayToScriptHash()) {
                this_input_p2sh_signature_operations =
                    spent_output_data.scriptPubKey.GetSigOpCount(txin_data.scriptSig) * WITNESS_SCALE_FACTOR;
            }
            block_input_p2sh_signature_operations += this_input_p2sh_signature_operations;

            int64_t this_input_witness_signature_operations =
                CountWitnessSigOps(txin_data.scriptSig, spent_output_data.scriptPubKey, txin_data.scriptWitness, flags);
            block_input_witness_signature_operations += this_input_witness_signature_operations;

            const int64_t spent_output_size = GetSerializeSize(spent_output_data);
            const int64_t spent_utxo_size = spent_output_size + PER_UTXO_OVERHEAD;
            transaction_data.total_input_value += spent_output_data.nValue;
            transaction_data.utxo_size_inc -= spent_utxo_size;
            block_net_utxo_size_impact -= spent_utxo_size;

            std::vector<std::vector<unsigned char>> solutions_data;
            const TxoutType which_type = Solver(spent_output_data.scriptPubKey, solutions_data);

            switch (which_type) {
            case TxoutType::NONSTANDARD:
                nonstandard_spend_count += 1;
                break;
            case TxoutType::ANCHOR:
                witness_unknown_spend_count += 1;
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

            const int64_t input_size =
                GetSerializeSize(txin_data) + GetSerializeSize(txin_data.scriptWitness.stack);
            const int64_t input_witness_size = GetSerializeSize(txin_data.scriptWitness.stack);
            const int64_t input_weight = GetTransactionInputWeight(txin_data);

            bool input_found_ord_prefix = false;
            int witnessversion;
            std::vector<unsigned char> witnessprogram;
            if (spent_output_data.scriptPubKey.IsWitnessProgram(witnessversion, witnessprogram)) {
                for (const std::vector<unsigned char>& script_bytes : txin_data.scriptWitness.stack) {
                    const CScript exec_script{script_bytes.begin(), script_bytes.end()};
                    const std::string exec_script_string = ScriptToAsmStr(exec_script);
                    if (exec_script_string.find("0 OP_IF 6582895 1") != std::string::npos) {
                        input_found_ord_prefix = true;
                        transaction_found_ordinal_prefix = true;
                    }
                }
            }

            block_inputs_total_size += input_size;
            block_inputs_total_witness_size += input_witness_size;

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

            const int64_t age_blocks = std::max<int64_t>(block_index.nHeight - coin.nHeight, 0);
            spent_age_blocks_sum += age_blocks;
            spent_inputs_count += 1;

            const CBlockIndex* spent_output_index = block_index.GetAncestor(coin.nHeight);
            if (spent_output_index != nullptr) {
                const int64_t age_seconds =
                    std::max<int64_t>(block_index.GetBlockTime() - spent_output_index->GetBlockTime(), 0);
                const double spent_output_btc =
                    static_cast<double>(spent_output_data.nValue) / static_cast<double>(COIN);
                coinblocks_destroyed += spent_output_btc * static_cast<double>(age_blocks);
                coindays_destroyed += spent_output_btc * (static_cast<double>(age_seconds) / 86400.0);
            }

            if (which_type == TxoutType::WITNESS_V1_TAPROOT) {
                taproot_inputs_total_witness_size += input_witness_size;
                std::span witness_stack{txin_data.scriptWitness.stack};
                if (witness_stack.size() >= 2 && !witness_stack.back().empty() &&
                    witness_stack.back().front() == ANNEX_TAG) {
                    taproot_annex_spend_count += 1;
                    witness_stack = witness_stack.first(witness_stack.size() - 1);
                }

                if (witness_stack.size() == 1) {
                    taproot_key_path_spend_count += 1;
                } else if (witness_stack.size() > 1) {
                    taproot_script_path_spend_count += 1;
                    const auto& control_block = witness_stack.back();
                    if (!control_block.empty() &&
                        (control_block[0] & TAPROOT_LEAF_MASK) == TAPROOT_LEAF_TAPSCRIPT) {
                        tapscript_spend_count += 1;
                    }
                }
            }

            const std::optional<std::string> address_string = ExtractAddressString(spent_output_data.scriptPubKey);
            const std::string prev_txid = txin_data.prevout.hash.GetHex();
            std::optional<std::string> prev_wtxid;
            if (const auto same_block = block_wtxids.find(prev_txid); same_block != block_wtxids.end()) {
                prev_wtxid = same_block->second;
            }

            address_flows.push_back(AddressFlowRow{
                network,
                block_hash,
                static_cast<int64_t>(block_index.nHeight),
                block_median_time_text,
                transaction_data.transaction_hash,
                transaction_data.transaction_witness_hash,
                static_cast<int64_t>(input_vector),
                input_size,
                static_cast<int64_t>(coin.nHeight),
                GetMedianTimePastString(spent_output_index),
                prev_txid,
                prev_wtxid,
                static_cast<int64_t>(txin_data.prevout.n),
                spent_output_size,
                static_cast<int64_t>(spent_script_type),
                address_string,
                -spent_output_data.nValue});
        }

        for (std::size_t output_vector = 0; output_vector < transaction->vout.size(); ++output_vector) {
            const CTxOut& txout_data = transaction->vout[output_vector];
            const int64_t output_size = GetSerializeSize(txout_data);
            block_outputs_total_size += output_size;
            const int64_t utxo_size = output_size + PER_UTXO_OVERHEAD;
            const int64_t this_output_legacy_signature_operations =
                txout_data.scriptPubKey.GetSigOpCount(false) * WITNESS_SCALE_FACTOR;
            block_output_legacy_signature_operations += this_output_legacy_signature_operations;

            transaction_data.total_output_value += txout_data.nValue;
            transaction_data.utxo_size_inc += utxo_size;
            block_net_utxo_size_impact += utxo_size;

            std::vector<std::vector<unsigned char>> solutions_data;
            const TxoutType which_type = Solver(txout_data.scriptPubKey, solutions_data);

            switch (which_type) {
            case TxoutType::NONSTANDARD:
                nonstandard_create_count += 1;
                break;
            case TxoutType::ANCHOR:
                witness_unknown_create_count += 1;
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

            address_flows.push_back(AddressFlowRow{
                network,
                block_hash,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                static_cast<int64_t>(block_index.nHeight),
                block_median_time_text,
                transaction_data.transaction_hash,
                transaction_data.transaction_witness_hash,
                static_cast<int64_t>(output_vector),
                output_size,
                static_cast<int64_t>(script_type),
                ExtractAddressString(txout_data.scriptPubKey),
                txout_data.nValue});
        }

        outputs_count += transaction_data.m_transaction->vout.size();
        inputs_count += transaction_data.m_transaction->vin.size();
        total_output_value += transaction_data.total_output_value;
        total_input_value += transaction_data.total_input_value;
        total_fees += transaction_data.GetFee();

        transaction_data_string_stream << "[";
        transaction_data_string_stream << transaction_data.m_transaction->ComputeTotalSize() << ",";
        transaction_data_string_stream << transaction_data.vsize << ",";
        transaction_data_string_stream << transaction_data.weight << ",";
        transaction_data_string_stream << transaction_data.GetFee() << ",";
        transaction_data_string_stream << transaction_found_ordinal_prefix;
        transaction_data_string_stream << "]";
        if (transaction_index != block.vtx.size() - 1) {
            transaction_data_string_stream << ",";
        }

        fee_rates[transaction_data.GetFeeRate()] += transaction_data.weight;
        if (!transaction_data.is_coinbase) {
            fee_rate_samples.emplace_back(
                static_cast<double>(transaction_data.GetFee()) / static_cast<double>(transaction_data.vsize),
                transaction_data.weight);
        }

        if (transaction_found_ordinal_prefix) {
            ordinals_weight += transaction_data.weight;
            ordinals_count += 1;
            ordinals_size += transaction_data.m_transaction->ComputeTotalSize();
            ordinals_vsize += transaction_data.vsize;
            ordinals_fees += transaction_data.GetFee();
        } else {
            non_ordinals_weight += transaction_data.weight;
            non_ordinals_count += 1;
            non_ordinals_size += transaction_data.m_transaction->ComputeTotalSize();
            non_ordinals_vsize += transaction_data.vsize;
            non_ordinals_fees += transaction_data.GetFee();
        }
    }

    std::ostringstream fee_rates_string_stream;
    fee_rates_string_stream << "[";
    for (auto it = fee_rates.begin(); it != fee_rates.end(); ++it) {
        fee_rates_string_stream << "[";
        fee_rates_string_stream << it->first;
        fee_rates_string_stream << ",";
        fee_rates_string_stream << it->second;
        fee_rates_string_stream << "]";
        if ((it != fee_rates.end()) && (std::next(it) == fee_rates.end())) {
            continue;
        }
        fee_rates_string_stream << ",";
    }

    std::ostringstream output_script_types_string_stream;
    output_script_types_string_stream << "[";
    for (auto it = output_script_types.begin(); it != output_script_types.end(); ++it) {
        output_script_types_string_stream << "[";
        output_script_types_string_stream << it->first;
        output_script_types_string_stream << ",";
        const auto arr = it->second;
        for (auto it2 = std::begin(arr); it2 != std::end(arr); ++it2) {
            output_script_types_string_stream << *it2;
            if ((it2 != std::end(arr)) && (std::next(it2) == std::end(arr))) {
                continue;
            }
            output_script_types_string_stream << ",";
        }
        output_script_types_string_stream << "]";
        if ((it != output_script_types.end()) && (std::next(it) == output_script_types.end())) {
            continue;
        }
        output_script_types_string_stream << ",";
    }

    std::ostringstream input_script_types_string_stream;
    input_script_types_string_stream << "[";
    for (auto it = input_script_types.begin(); it != input_script_types.end(); ++it) {
        input_script_types_string_stream << "[";
        input_script_types_string_stream << it->first;
        input_script_types_string_stream << ",";
        const auto arr = it->second;
        for (auto it2 = std::begin(arr); it2 != std::end(arr); ++it2) {
            input_script_types_string_stream << *it2;
            if ((it2 != std::end(arr)) && (std::next(it2) == std::end(arr))) {
                continue;
            }
            input_script_types_string_stream << ",";
        }
        input_script_types_string_stream << "]";
        if ((it != input_script_types.end()) && (std::next(it) == input_script_types.end())) {
            continue;
        }
        input_script_types_string_stream << ",";
    }

    fee_rates_string_stream << "]";
    output_data_string_stream << "]";
    input_data_string_stream << "]";
    transaction_data_string_stream << "]";
    input_script_types_string_stream << "]";
    output_script_types_string_stream << "]";

    const FeeRateSummary fee_rate_summary = SummarizeFeeRates(fee_rate_samples);
    const double avg_spent_age_blocks =
        spent_inputs_count == 0 ? 0.0 : static_cast<double>(spent_age_blocks_sum) / static_cast<double>(spent_inputs_count);
    const int witness_commitment_index = GetWitnessCommitmentIndex(block);
    const VersionBitsExportData version_bits = BuildVersionBitsExportData(block_index.nVersion);

    BlockInsertData data{
        block_hash,
        block_index.hashMerkleRoot.GetHex(),
        block_index.GetBlockTime(),
        block_median_time,
        block_index.nHeight,
        GetBlockSubsidy(block_index.nHeight, Params().GetConsensus()),
        static_cast<int64_t>(block_index.nTx),
        static_cast<int64_t>(block_index.nVersion),
        static_cast<int64_t>(block_index.nStatus),
        static_cast<int64_t>(block_index.nBits),
        static_cast<int64_t>(block_index.nNonce),
        GetDifficulty(block_index),
        block_index.nChainWork.GetHex(),
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
        block.hashPrevBlock.ToString(),
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
        static_cast<int64_t>(non_ordinals_fees),
        spent_age_blocks_sum,
        avg_spent_age_blocks,
        coinblocks_destroyed,
        coindays_destroyed,
        fee_rate_summary.min,
        fee_rate_summary.median,
        fee_rate_summary.p90,
        fee_rate_summary.max,
        static_cast<int64_t>(block_inputs_total_witness_size),
        static_cast<int64_t>(taproot_inputs_total_witness_size),
        static_cast<int64_t>(taproot_key_path_spend_count),
        static_cast<int64_t>(taproot_script_path_spend_count),
        static_cast<int64_t>(taproot_annex_spend_count),
        static_cast<int64_t>(tapscript_spend_count),
        static_cast<int64_t>(coinbase_tx.vin.front().scriptSig.size()),
        static_cast<int64_t>(coinbase_tx.vin.front().scriptWitness.stack.size()),
        static_cast<int64_t>(GetSerializeSize(coinbase_tx.vin.front().scriptWitness.stack)),
        static_cast<int64_t>(coinbase_tx.vout.size()),
        witness_commitment_index != NO_WITNESS_COMMITMENT,
        witness_commitment_index == NO_WITNESS_COMMITMENT ? std::nullopt : std::optional<int64_t>{static_cast<int64_t>(witness_commitment_index)},
        ExtractCoinbaseTag(coinbase_tx.vin.front().scriptSig),
        version_bits.top_bits_valid,
        version_bits.signalling_json,
        version_bits.unknown_bits_json};

    auto& conn = enterprise::PgConnection();
    WriteBlockRow(conn, data, address_flows);
}

TransactionData::TransactionData(std::size_t transaction_index, const CTransactionRef& transaction)
    : m_transaction_index(transaction_index), m_transaction(transaction)
{
    transaction_hash = transaction->GetHash().GetHex();
    transaction_witness_hash = transaction->GetWitnessHash().GetHex();
    is_coinbase = transaction->IsCoinBase();
    weight = GetTransactionWeight(*transaction);
    vsize = GetVirtualTransactionSize(*transaction);
}
