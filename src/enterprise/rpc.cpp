#include <enterprise/rpc.h>

#include <core_io.h>
#include <enterprise/utxo_stats.h>
#include <rpc/request.h>
#include <rpc/server.h>
#include <rpc/server_util.h>
#include <rpc/util.h>
#include <univalue.h>

#include <stdexcept>

namespace {

UniValue ExportStatsToUniValue(const enterprise::UtxoSetExportStats& stats)
{
    UniValue result{UniValue::VOBJ};
    result.pushKV("height", stats.height);
    if (!stats.block_hash.IsNull()) {
        result.pushKV("bestblock", stats.block_hash.GetHex());
    }
    if (!stats.median_time.empty()) {
        result.pushKV("median_time", stats.median_time);
    }
    result.pushKV("txouts", stats.utxo_count);
    result.pushKV("total_amount", ValueFromAmount(stats.total_value));
    result.pushKV("total_size", stats.total_size);
    result.pushKV("age_buckets", static_cast<int64_t>(stats.age_buckets));
    result.pushKV("skipped", stats.skipped);
    if (!stats.skip_reason.empty()) {
        result.pushKV("skip_reason", stats.skip_reason);
    }
    result.pushKV("wrote_usd", stats.wrote_usd);
    result.pushKV("wrote_optional", stats.wrote_optional);
    return result;
}

RPCMethod exportutxostats()
{
    return RPCMethod{
        "exportutxostats",
        "Scan the current UTXO set and write age histograms to PostgreSQL.\n"
        "This can take several minutes. Only the live chainstate can be snapshotted;\n"
        "historical heights are not available on a pruned node.\n",
        {
            {"force", RPCArg::Type::BOOL, RPCArg::Default{true}, "Export the current tip even if a snapshot was taken in the current 4032-block interval."},
            {"include_optional", RPCArg::Type::BOOL, RPCArg::Default{true}, "Also write utxo_balances, utxo_script_types, and USD tables when a price is available."},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::NUM, "height", "Block height of the exported UTXO set"},
                {RPCResult::Type::STR_HEX, "bestblock", /*optional=*/true, "Hash of that block"},
                {RPCResult::Type::STR, "median_time", /*optional=*/true, "Median time past of that block, UTC"},
                {RPCResult::Type::NUM, "txouts", "Number of unspent outputs scanned"},
                {RPCResult::Type::STR_AMOUNT, "total_amount", "Total value of the UTXO set"},
                {RPCResult::Type::NUM, "total_size", "Serialized UTXO size including per-entry overhead"},
                {RPCResult::Type::NUM, "age_buckets", "Number of week-of-age buckets written"},
                {RPCResult::Type::BOOL, "skipped", "True if Postgres already had this height or a snapshot was not due"},
                {RPCResult::Type::STR, "skip_reason", /*optional=*/true, "Why the export was skipped"},
                {RPCResult::Type::BOOL, "wrote_usd", "Whether USD denomination tables were written"},
                {RPCResult::Type::BOOL, "wrote_optional", "Whether optional non-age tables were written"},
            },
        },
        RPCExamples{
            HelpExampleCli("exportutxostats", "") +
            HelpExampleCli("exportutxostats", "true false") +
            HelpExampleRpc("exportutxostats", "true")
        },
        [](const RPCMethod& self, const JSONRPCRequest& request) -> UniValue {
            node::NodeContext& node{EnsureAnyNodeContext(request.context)};
            const bool force{self.Arg<bool>("force")};
            const bool include_optional{self.Arg<bool>("include_optional")};
            try {
                return ExportStatsToUniValue(enterprise::ExportCurrentUtxoSet(node, force, include_optional));
            } catch (const std::exception& e) {
                throw JSONRPCError(RPC_INTERNAL_ERROR, e.what());
            }
        },
    };
}

} // namespace

namespace enterprise {

void RegisterEnterpriseRPCCommands(CRPCTable& t)
{
    static const CRPCCommand commands[]{
        {"blockchain", &exportutxostats},
    };
    for (const auto& c : commands) {
        t.appendCommand(c.name, &c);
    }
}

} // namespace enterprise
