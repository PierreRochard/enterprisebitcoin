#include <enterprise/utxo_stats.h>

#include <chain.h>
#include <coins.h>
#include <kernel/cs_main.h>
#include <kernel/types.h>
#include <logging.h>
#include <node/context.h>
#include <primitives/block.h>
#include <sync.h>
#include <util/check.h>
#include <util/log.h>
#include <util/signalinterrupt.h>
#include <util/thread.h>
#include <validation.h>

#include <chrono>
#include <exception>
#include <functional>
#include <stdexcept>

using namespace std::chrono_literals;

namespace {
std::mutex g_export_mutex;
} // namespace

namespace enterprise {

std::shared_ptr<UtxoStatsExporter> g_utxo_stats;

UtxoSetExportStats ExportCurrentUtxoSet(node::NodeContext& node, bool force, bool include_optional_tables)
{
    std::lock_guard<std::mutex> export_lock{g_export_mutex};

    ChainstateManager& chainman{*Assert(node.chainman)};
    Chainstate& chainstate{chainman.ActiveChainstate()};
    chainstate.ForceFlushStateToDisk(/*wipe_cache=*/false);

    std::unique_ptr<CCoinsViewCursor> cursor;
    const CBlockIndex* pindex{nullptr};
    {
        LOCK(::cs_main);
        cursor = chainstate.CoinsDB().Cursor();
        if (!cursor) {
            throw std::runtime_error("UTXO set cursor is unavailable");
        }
        pindex = chainstate.m_blockman.LookupBlockIndex(cursor->GetBestBlock());
    }
    if (!pindex) {
        throw std::runtime_error("Cannot find block index for the UTXO set tip");
    }

    const int height{pindex->nHeight};
    const bool already{UtxoHeightExported(height)};
    const int64_t db_max{MaxExportedUtxoHeight()};
    if (!ShouldExportUtxoSet(height, db_max, already, force)) {
        UtxoSetExportStats skipped;
        skipped.height = height;
        skipped.block_hash = pindex->GetBlockHash();
        skipped.skipped = true;
        skipped.skip_reason = already ? "already exported" : "not due yet";
        return skipped;
    }

    const std::function<void()> interruption{[&] {
        if (node.rpc_interruption_point) {
            node.rpc_interruption_point();
        }
        if (static_cast<bool>(chainman.m_interrupt)) {
            throw std::runtime_error("interrupted");
        }
    }};

    return ExportUtxoSetFromCursor(*pindex, *cursor, interruption, include_optional_tables);
}

UtxoStatsExporter::UtxoStatsExporter(node::NodeContext& node) : m_node{node} {}

UtxoStatsExporter::~UtxoStatsExporter()
{
    Stop();
}

void UtxoStatsExporter::Start()
{
    std::lock_guard<std::mutex> lock{m_mutex};
    if (m_started) return;
    m_stop = false;
    m_started = true;
    m_thread = std::thread(&util::TraceThread, "utxostats", [this] { WorkerLoop(); });
}

void UtxoStatsExporter::Stop()
{
    {
        std::lock_guard<std::mutex> lock{m_mutex};
        if (!m_started) return;
        m_stop = true;
    }
    m_cv.notify_all();
    if (m_thread.joinable()) {
        m_thread.join();
    }
    std::lock_guard<std::mutex> lock{m_mutex};
    m_started = false;
}

void UtxoStatsExporter::RequestCatchUp()
{
    RequestExport();
}

void UtxoStatsExporter::MaybeExportOnConnect(const CBlockIndex& tip)
{
    if (tip.nHeight <= 0 || tip.nHeight % UTXO_EXPORT_INTERVAL != 0) return;
    if (!m_node.chainman || !m_node.chainman->IsInitialBlockDownload()) return;

    try {
        const UtxoSetExportStats stats{ExportCurrentUtxoSet(m_node, /*force=*/false, /*include_optional_tables=*/false)};
        if (stats.skipped) {
            LogInfo("utxostats: skipped height %d during IBD (%s)", stats.height, stats.skip_reason);
        } else {
            LogInfo("utxostats: exported height %d during IBD with %u coins in %u age buckets",
                stats.height, stats.utxo_count, stats.age_buckets);
        }
    } catch (const std::exception& e) {
        LogError("utxostats: IBD export failed at height %d: %s", tip.nHeight, e.what());
    }
}

void UtxoStatsExporter::BlockConnected(const kernel::ChainstateRole& role, const std::shared_ptr<const CBlock>&, const CBlockIndex* pindex)
{
    if (role.historical || !pindex) return;
    if (pindex->nHeight > 0 && pindex->nHeight % UTXO_EXPORT_INTERVAL == 0) {
        RequestExport();
    }
}

void UtxoStatsExporter::RequestExport()
{
    {
        std::lock_guard<std::mutex> lock{m_mutex};
        m_pending = true;
    }
    m_cv.notify_one();
}

void UtxoStatsExporter::WorkerLoop()
{
    while (true) {
        {
            std::unique_lock<std::mutex> lock{m_mutex};
            m_cv.wait(lock, [this] { return m_stop || m_pending; });
            if (m_stop) return;
            m_pending = false;
        }

        try {
            const UtxoSetExportStats stats{ExportCurrentUtxoSet(m_node, /*force=*/false, /*include_optional_tables=*/false)};
            if (stats.skipped) {
                LogInfo("utxostats: skipped height %d (%s)", stats.height, stats.skip_reason);
            } else {
                LogInfo("utxostats: exported height %d with %u coins in %u age buckets",
                    stats.height, stats.utxo_count, stats.age_buckets);
            }
        } catch (const std::exception& e) {
            if (m_node.chainman && static_cast<bool>(m_node.chainman->m_interrupt)) {
                LogInfo("utxostats: export interrupted: %s", e.what());
                return;
            }
            LogError("utxostats: export failed: %s", e.what());
        }
    }
}

} // namespace enterprise
