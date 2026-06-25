#include <enterprise/enterprise_index.h>

#include <chain.h>
#include <common/args.h>
#include <enterprise/block_to_sql.h>
#include <enterprise/pg.h>
#include <interfaces/chain.h>
#include <interfaces/types.h>
#include <kernel/types.h>
#include <node/blockstorage.h>
#include <primitives/block.h>
#include <script/verify_flags.h>
#include <tinyformat.h>
#include <uint256.h>
#include <undo.h>
#include <util/check.h>
#include <util/fs.h>
#include <util/log.h>
#include <util/signalinterrupt.h>
#include <util/thread.h>
#include <validation.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <exception>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace std::chrono_literals;

std::unique_ptr<EnterpriseIndex> g_enterprise_index;

static constexpr std::size_t COVERED_CONNECT_BATCH_SIZE{2048};

EnterpriseIndex::EnterpriseIndex(std::unique_ptr<interfaces::Chain> chain, size_t n_cache_size, bool f_memory, bool f_wipe)
    : BaseIndex(std::move(chain), "enterpriseindex", "enterpriseidx"),
      m_db{std::make_unique<BaseIndex::DB>(gArgs.GetDataDirNet() / "indexes" / "enterpriseindex" / "db", n_cache_size, f_memory, f_wipe)},
      m_spool{gArgs.GetDataDirNet() / "enterprise" / "block_spool"}
{
    const int64_t spool_max_mib{std::max<int64_t>(0, gArgs.GetIntArg("-enterprisespoolmax", DEFAULT_ENTERPRISE_SPOOL_MAX_MIB))};
    m_spool_max_bytes = static_cast<uint64_t>(spool_max_mib) * 1024 * 1024;
}

EnterpriseIndex::~EnterpriseIndex()
{
    StopWriter();
}

void EnterpriseIndex::Stop()
{
    StopWriter();
    BaseIndex::Stop();
}

interfaces::Chain::NotifyOptions EnterpriseIndex::CustomOptions()
{
    interfaces::Chain::NotifyOptions options;
    options.connect_undo_data = true;
    return options;
}

bool EnterpriseIndex::CustomInit(const std::optional<interfaces::BlockRef>&)
{
    StartWriter();
    return true;
}

bool EnterpriseIndex::CustomAppend(const interfaces::BlockInfo& block)
{
    if (!ApplySpoolBackpressure()) return false;

    Assert(block.data);
    if (block.height > 0) Assert(block.undo_data);

    EnterpriseBlockDelta delta;
    delta.event_type = EnterpriseSpoolEventType::CONNECT;
    delta.block_hash = block.hash;
    delta.height = block.height;
    delta.source = "index";
    delta.block = *block.data;
    if (block.undo_data) delta.undo = *block.undo_data;

    const CBlockIndex* pindex{WITH_LOCK(::cs_main, return m_chainstate->m_blockman.LookupBlockIndex(block.hash))};
    if (!pindex) {
        LogError("enterprise: cannot find block index entry for %s", block.hash.ToString());
        return false;
    }
    delta.script_flags = GetBlockScriptFlags(*pindex, m_chainstate->m_chainman).as_int();

    if (!m_spool.Write(delta)) return false;
    NotifyWriter();
    if (!ApplySpoolBackpressure()) return false;
    return true;
}

bool EnterpriseIndex::CustomRemove(const interfaces::BlockInfo& block)
{
    if (!ApplySpoolBackpressure()) return false;

    EnterpriseBlockDelta delta;
    delta.event_type = EnterpriseSpoolEventType::DISCONNECT;
    delta.block_hash = block.hash;
    delta.height = block.height;
    delta.source = "reorg";
    if (!m_spool.Write(delta)) return false;
    NotifyWriter();
    if (!ApplySpoolBackpressure()) return false;
    return true;
}

void EnterpriseIndex::NotifyWriter()
{
    m_writer_cv.notify_one();
}

void EnterpriseIndex::StartWriter()
{
    if (m_writer_thread.joinable()) return;
    {
        std::lock_guard<std::mutex> lock{m_writer_mutex};
        m_writer_stop = false;
    }
    m_writer_thread = std::thread(&util::TraceThread, "enterprisepg", [this] { WriterLoop(); });
}

void EnterpriseIndex::StopWriter()
{
    {
        std::lock_guard<std::mutex> lock{m_writer_mutex};
        m_writer_stop = true;
    }
    m_writer_cv.notify_all();
    if (m_writer_thread.joinable()) {
        m_writer_thread.join();
    }
}

void EnterpriseIndex::WriterLoop()
{
    while (true) {
        if (!DrainOnce()) {
            std::unique_lock<std::mutex> lock{m_writer_mutex};
            m_writer_cv.wait_for(lock, 30s, [this] { return m_writer_stop; });
        } else {
            std::unique_lock<std::mutex> lock{m_writer_mutex};
            if (m_writer_stop) return;
            m_writer_cv.wait_for(lock, 5s, [this] { return m_writer_stop; });
        }

        std::lock_guard<std::mutex> lock{m_writer_mutex};
        if (m_writer_stop) return;
    }
}

bool EnterpriseIndex::ApplySpoolBackpressure()
{
    if (m_spool_max_bytes == 0) return true;

    auto shutdown_interrupted = [this] {
        return m_chainstate && static_cast<bool>(m_chainstate->m_chainman.m_interrupt);
    };

    bool logged{false};
    while (m_spool.UsageBytes() > m_spool_max_bytes) {
        if (shutdown_interrupted()) {
            LogInfo("enterprise: shutdown interrupt bypassing block spool backpressure at %d MiB",
                m_spool.UsageBytes() / (1024 * 1024));
            return true;
        }
        if (!logged) {
            LogInfo("enterprise: block spool is %d MiB, waiting for postgres writer to drain below %d MiB",
                m_spool.UsageBytes() / (1024 * 1024),
                m_spool_max_bytes / (1024 * 1024));
            logged = true;
        }

        NotifyWriter();
        {
            std::unique_lock<std::mutex> lock{m_writer_mutex};
            if (m_writer_stop) return true;
            m_writer_cv.wait_for(lock, 5s, [this, &shutdown_interrupted] { return m_writer_stop || shutdown_interrupted(); });
            if (m_writer_stop || shutdown_interrupted()) return true;
        }
    }

    if (logged) {
        LogInfo("enterprise: block spool drained to %d MiB; resuming validation", m_spool.UsageBytes() / (1024 * 1024));
    }
    return true;
}

bool EnterpriseIndex::DrainOnce()
{
    bool all_drained{true};
    std::vector<std::pair<fs::path, EnterpriseBlockSpoolFileInfo>> covered_connect_batch;

    auto flush_covered_connect_batch = [&] {
        if (covered_connect_batch.empty()) return true;
        const bool processed{ProcessCoveredConnectBatch(covered_connect_batch)};
        covered_connect_batch.clear();
        return processed;
    };

    for (const auto& path : m_spool.Pending()) {
        {
            std::lock_guard<std::mutex> lock{m_writer_mutex};
            if (m_writer_stop) return false;
        }

        if (const auto info{EnterpriseBlockSpool::InfoFromFileName(path)}; info && info->event_type == EnterpriseSpoolEventType::CONNECT) {
            covered_connect_batch.emplace_back(path, *info);
            if (covered_connect_batch.size() >= COVERED_CONNECT_BATCH_SIZE && !flush_covered_connect_batch()) {
                all_drained = false;
                break;
            }
            continue;
        }

        if (!flush_covered_connect_batch()) {
            all_drained = false;
            break;
        }
        if (!ProcessSpoolFile(path)) {
            all_drained = false;
            break;
        }
    }
    if (all_drained && !flush_covered_connect_batch()) {
        all_drained = false;
    }
    if (all_drained && ReconcileGaps()) {
        return true;
    }
    return all_drained;
}

bool EnterpriseIndex::ProcessCoveredConnectBatch(const std::vector<std::pair<fs::path, EnterpriseBlockSpoolFileInfo>>& batch)
{
    if (batch.empty()) return true;

    std::vector<enterprise::BlockRowKey> keys;
    keys.reserve(batch.size());
    for (const auto& [_, info] : batch) {
        keys.push_back(enterprise::BlockRowKey{info.height, info.block_hash});
    }

    std::set<std::pair<int, std::string>> matching_rows;
    try {
        for (const enterprise::BlockRowKey& row : enterprise::FindMatchingBlockRows(keys)) {
            matching_rows.emplace(row.height, row.hash.GetHex());
        }
    } catch (const std::exception& e) {
        LogError("enterprise: failed to batch-check spooled blocks in postgres: %s", e.what());
        return false;
    }

    std::vector<fs::path> covered_paths;
    covered_paths.reserve(batch.size());
    for (const auto& [path, info] : batch) {
        if (matching_rows.contains({info.height, info.block_hash.GetHex()})) {
            covered_paths.push_back(path);
        }
    }

    if (!covered_paths.empty()) {
        if (!m_spool.RemoveMany(covered_paths)) return false;
        m_writer_cv.notify_all();
        LogDebug(BCLog::ALL, "enterprise: removed %u already-covered block spool files\n", covered_paths.size());
    }

    for (const auto& [path, info] : batch) {
        if (!matching_rows.contains({info.height, info.block_hash.GetHex()}) && !ProcessSpoolFile(path)) {
            return false;
        }
    }
    return true;
}

bool EnterpriseIndex::ProcessSpoolFile(const fs::path& path)
{
    if (const auto info{EnterpriseBlockSpool::InfoFromFileName(path)}; info && info->event_type == EnterpriseSpoolEventType::CONNECT) {
        try {
            if (enterprise::BlockRowMatches(info->height, info->block_hash)) {
                const bool removed{m_spool.Remove(path)};
                if (removed) m_writer_cv.notify_all();
                return removed;
            }
        } catch (const std::exception& e) {
            LogError("enterprise: failed to check spooled block %s in postgres: %s", info->block_hash.ToString(), e.what());
            return false;
        }
    }

    EnterpriseBlockDelta delta;
    if (!m_spool.Read(path, delta)) return false;

    try {
        enterprise::MarkIngestStarted(delta, path, delta.source);
        if (delta.event_type == EnterpriseSpoolEventType::DISCONNECT) {
            DeleteBlockFromSql(delta.block_hash);
        } else {
            if (enterprise::BlockRowMatches(delta.height, delta.block_hash)) {
                enterprise::MarkGapResolved(delta.height, delta.block_hash, delta.source);
                enterprise::MarkIngestSucceeded(delta, path, delta.source);
                const bool removed{m_spool.Remove(path)};
                if (removed) m_writer_cv.notify_all();
                return removed;
            }

            const CBlockIndex* pindex{WITH_LOCK(::cs_main, return m_chainstate->m_blockman.LookupBlockIndex(delta.block_hash))};
            if (!pindex) {
                LogError("enterprise: cannot find block index entry for spooled block %s", delta.block_hash.ToString());
                return false;
            }
            if (pindex->nHeight != delta.height) {
                LogError("enterprise: spooled block %s height mismatch: index=%d spool=%d", delta.block_hash.ToString(), pindex->nHeight, delta.height);
                return false;
            }
            BlockToSql block_to_sql{pindex, delta.block, delta.undo, delta.ScriptFlags()};
            enterprise::MarkGapResolved(delta.height, delta.block_hash, delta.source);
        }
        enterprise::MarkIngestSucceeded(delta, path, delta.source);
    } catch (const std::exception& e) {
        enterprise::MarkIngestFailed(delta, path, delta.source, e.what());
        LogError("enterprise: failed to write spooled block %s to postgres: %s", delta.block_hash.ToString(), e.what());
        return false;
    }

    const bool removed{m_spool.Remove(path)};
    if (removed) m_writer_cv.notify_all();
    return removed;
}

bool EnterpriseIndex::ReconcileGaps()
{
    const auto now{NodeClock::now()};
    if (now < m_next_gap_scan) return false;

    const int chain_tip_height{WITH_LOCK(::cs_main, return m_chainstate->m_chain.Height())};
    std::vector<int> gaps;
    try {
        gaps = enterprise::FindBlockTableGaps(chain_tip_height, DEFAULT_ENTERPRISE_GAP_BATCH_SIZE);
    } catch (const std::exception& e) {
        m_next_gap_scan = now + 60s;
        LogDebug(BCLog::ALL, "enterprise: block gap scan skipped: %s\n", e.what());
        return false;
    }

    if (gaps.empty()) {
        if (chain_tip_height >= 0) {
            try {
                const enterprise::BlockTableCoverage coverage{enterprise::GetBlockTableCoverage(chain_tip_height)};
                if (coverage.Complete()) {
                    LogInfo("enterprise: blocks table coverage complete: %d/%d heights populated",
                        coverage.populated_heights, coverage.expected_blocks);
                } else {
                    LogInfo("enterprise: blocks table coverage incomplete: populated=%d expected=%d missing=%d duplicate_rows=%d unavailable=%d",
                        coverage.populated_heights,
                        coverage.expected_blocks,
                        coverage.missing_heights,
                        coverage.duplicate_rows,
                        coverage.unavailable_heights);
                }
            } catch (const std::exception& e) {
                LogDebug(BCLog::ALL, "enterprise: block coverage scan skipped: %s\n", e.what());
            }
        }
        m_next_gap_scan = now + 60s;
        return false;
    }

    bool queued{false};
    for (const int height : gaps) {
        const CBlockIndex* pindex{WITH_LOCK(::cs_main, return m_chainstate->m_chain[height])};
        if (!pindex) continue;
        if (QueueLocalBackfill(*pindex)) {
            enterprise::MarkGapQueued(height, pindex->GetBlockHash(), "local");
            queued = true;
            continue;
        }
        enterprise::MarkGapUnavailable(height, pindex->GetBlockHash(), "local", "block delta not available locally; rewind or reindex chainstate to replay the enterprise index");
    }
    if (!queued) {
        m_next_gap_scan = now + 60s;
    }
    return queued;
}

bool EnterpriseIndex::QueueLocalBackfill(const CBlockIndex& block_index)
{
    CBlock block;
    if (!m_chainstate->m_blockman.ReadBlock(block, block_index)) return false;

    CBlockUndo undo;
    if (block_index.nHeight > 0 && !m_chainstate->m_blockman.ReadBlockUndo(undo, block_index)) return false;

    EnterpriseBlockDelta delta;
    delta.event_type = EnterpriseSpoolEventType::CONNECT;
    delta.block_hash = block_index.GetBlockHash();
    delta.height = block_index.nHeight;
    delta.source = "local";
    delta.script_flags = GetBlockScriptFlags(block_index, m_chainstate->m_chainman).as_int();
    delta.block = std::move(block);
    delta.undo = std::move(undo);
    return m_spool.Write(delta);
}
