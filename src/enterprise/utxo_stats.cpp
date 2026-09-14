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
#include <future>
#include <memory>
#include <stdexcept>
#include <thread>

using namespace std::chrono_literals;

namespace {
std::mutex g_export_mutex;

enterprise::UtxoAgeHistogram ScanCurrentUtxoAge(node::NodeContext& node)
{
    std::lock_guard<std::mutex> export_lock{g_export_mutex};

    ChainstateManager& chainman{*Assert(node.chainman)};
    Chainstate& chainstate{chainman.ActiveChainstate()};
    LogInfo("utxostats: flushing chainstate before UTXO cursor");
    chainstate.ForceFlushStateToDisk(/*wipe_cache=*/false);
    LogInfo("utxostats: chainstate flush finished, opening UTXO cursor");

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
    LogInfo("utxostats: UTXO cursor tip height %d hash=%s", pindex->nHeight, pindex->GetBlockHash().ToString());

    const std::function<void()> interruption{[&] {
        if (node.rpc_interruption_point) {
            node.rpc_interruption_point();
        }
        if (static_cast<bool>(chainman.m_interrupt)) {
            throw std::runtime_error("interrupted");
        }
    }};
    return enterprise::ScanUtxoAgeFromCursor(*pindex, *cursor, interruption);
}
} // namespace

namespace enterprise {

std::shared_ptr<UtxoStatsExporter> g_utxo_stats;

UtxoSetExportStats ExportCurrentUtxoSet(node::NodeContext& node, bool force, bool include_optional_tables)
{
    std::lock_guard<std::mutex> export_lock{g_export_mutex};

    ChainstateManager& chainman{*Assert(node.chainman)};
    Chainstate& chainstate{chainman.ActiveChainstate()};
    LogInfo("utxostats: flushing chainstate before UTXO cursor (force=%d include_optional=%d)",
        force, include_optional_tables);
    chainstate.ForceFlushStateToDisk(/*wipe_cache=*/false);
    LogInfo("utxostats: chainstate flush finished, opening UTXO cursor");

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
    LogInfo("utxostats: UTXO cursor tip height %d hash=%s", height, pindex->GetBlockHash().ToString());
    const bool already{UtxoHeightExported(height)};
    const int64_t db_max{MaxExportedUtxoHeight()};
    LogInfo("utxostats: export decision height=%d already=%d db_max=%d force=%d",
        height, already, db_max, force);
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

void UtxoStatsExporter::LoadCatalog()
{
    std::unordered_set<int> heights;
    int64_t max_height{-1};
    LoadExportedUtxoHeights(heights, max_height);
    const size_t count{heights.size()};
    {
        std::lock_guard<std::mutex> lock{m_catalog_mutex};
        m_exported_heights = std::move(heights);
        m_db_max = max_height;
    }
    m_catalog_ready.store(true, std::memory_order_release);
    LogInfo("utxostats: loaded %u exported heights (max %d)", count, max_height);
}

bool UtxoStatsExporter::CatalogHasHeight(int height) const
{
    std::lock_guard<std::mutex> lock{m_catalog_mutex};
    return m_exported_heights.contains(height);
}

int64_t UtxoStatsExporter::CatalogMaxHeight() const
{
    std::lock_guard<std::mutex> lock{m_catalog_mutex};
    return m_db_max;
}

void UtxoStatsExporter::CatalogAddHeight(int height)
{
    std::lock_guard<std::mutex> lock{m_catalog_mutex};
    m_exported_heights.insert(height);
    if (height > m_db_max) m_db_max = height;
}

UtxoSetExportStats UtxoStatsExporter::SubmitSql(UtxoAgeHistogram histogram)
{
    auto job{std::make_shared<SqlJob>()};
    job->histogram = std::move(histogram);
    std::future<UtxoSetExportStats> done{job->done.get_future()};
    {
        std::lock_guard<std::mutex> lock{m_mutex};
        if (m_stop) {
            throw std::runtime_error("utxostats thread is stopped");
        }
        m_sql_queue.push_back(std::move(job));
    }
    m_cv.notify_one();
    return done.get();
}

void UtxoStatsExporter::MaybeExportOnConnect(const CBlockIndex& tip)
{
    if (tip.nHeight <= 0 || tip.nHeight % UTXO_EXPORT_INTERVAL != 0) return;
    if (!m_node.chainman || !m_node.chainman->IsInitialBlockDownload()) return;

    // ConnectTip holds cs_main on msghand. Opening libpq here forks a
    // multithreaded bitcoind and has corrupted chainstate / coins cache.
    LogInfo("utxostats: IBD connect hook height %d hash=%s",
        tip.nHeight, tip.GetBlockHash().ToString());
    try {
        if (!m_catalog_ready.load(std::memory_order_acquire)) {
            LogWarning("utxostats: exported-height catalog not ready; skipping height %d", tip.nHeight);
            return;
        }
        const bool already{CatalogHasHeight(tip.nHeight)};
        const int64_t db_max{CatalogMaxHeight()};
        const bool should{ShouldExportUtxoSet(tip.nHeight, db_max, already, /*force=*/false)};
        LogInfo("utxostats: IBD decision height=%d already=%d db_max=%d should_export=%d",
            tip.nHeight, already, db_max, should);
        if (!should) {
            LogInfo("utxostats: skipped height %d during IBD (%s)",
                tip.nHeight, already ? "already exported" : "not due yet");
            return;
        }
        LogInfo("utxostats: IBD export starting at height %d (scan on msghand, SQL on utxostats thread)",
            tip.nHeight);
        UtxoAgeHistogram histogram{ScanCurrentUtxoAge(m_node)};
        LogInfo("utxostats: handing %u age buckets for height %d to SQL thread",
            histogram.stats.age_buckets, histogram.stats.height);
        const UtxoSetExportStats stats{SubmitSql(std::move(histogram))};
        LogInfo("utxostats: exported height %d during IBD with %u coins in %u age buckets",
            stats.height, stats.utxo_count, stats.age_buckets);
    } catch (const std::exception& e) {
        LogError("utxostats: IBD export failed at height %d: %s", tip.nHeight, e.what());
    }
}

void UtxoStatsExporter::BlockConnected(const kernel::ChainstateRole& role, const std::shared_ptr<const CBlock>&, const CBlockIndex* pindex)
{
    if (role.historical || !pindex) return;
    // IBD snapshots are taken on the connect thread at the aligned height.
    // A background ForceFlush here races ConnectTip and has segfaulted the coins cache.
    if (m_node.chainman && m_node.chainman->IsInitialBlockDownload()) return;
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
    try {
        LoadCatalog();
    } catch (const std::exception& e) {
        LogError("utxostats: failed to load exported-height catalog: %s", e.what());
        m_catalog_ready.store(true, std::memory_order_release);
    }

    while (true) {
        std::shared_ptr<SqlJob> sql_job;
        bool run_catch_up{false};
        {
            std::unique_lock<std::mutex> lock{m_mutex};
            m_cv.wait(lock, [this] { return m_stop || m_pending || !m_sql_queue.empty(); });
            if (m_stop && m_sql_queue.empty()) return;
            if (!m_sql_queue.empty()) {
                sql_job = std::move(m_sql_queue.front());
                m_sql_queue.pop_front();
            } else if (m_pending) {
                m_pending = false;
                run_catch_up = true;
            }
        }

        if (sql_job) {
            try {
                WriteUtxoAgeHistogram(sql_job->histogram);
                CatalogAddHeight(sql_job->histogram.stats.height);
                sql_job->done.set_value(sql_job->histogram.stats);
            } catch (...) {
                sql_job->done.set_exception(std::current_exception());
            }
            continue;
        }

        if (!run_catch_up) continue;

        try {
            if (m_node.chainman && m_node.chainman->IsInitialBlockDownload()) {
                LogInfo("utxostats: background export deferred during IBD");
                continue;
            }
            LogInfo("utxostats: background export requested");
            const UtxoSetExportStats stats{ExportCurrentUtxoSet(m_node, /*force=*/false, /*include_optional_tables=*/false)};
            if (!stats.skipped) {
                CatalogAddHeight(stats.height);
            }
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
