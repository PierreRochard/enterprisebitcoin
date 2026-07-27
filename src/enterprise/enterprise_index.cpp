#include <enterprise/enterprise_index.h>

#include <chain.h>
#include <common/args.h>
#include <enterprise/block_to_sql.h>
#include <enterprise/pg.h>
#include <interfaces/chain.h>
#include <interfaces/types.h>
#include <kernel/chain.h>
#include <kernel/types.h>
#include <node/abort.h>
#include <node/blockstorage.h>
#include <node/context.h>
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
#include <util/translation.h>
#include <validation.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <exception>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace std::chrono_literals;

std::unique_ptr<EnterpriseIndex> g_enterprise_index;

static constexpr std::size_t COVERED_CONNECT_BATCH_SIZE{2048};
static constexpr auto PRICE_FINALIZATION_SCAN_INTERVAL{60s};

namespace {
class PriceFinalizationPruneLock
{
private:
    node::BlockManager& m_blockman;

public:
    PriceFinalizationPruneLock(node::BlockManager& blockman, int height)
        : m_blockman{blockman}
    {
        WITH_LOCK(::cs_main, m_blockman.UpdatePruneLock("enterprise-price-finalization", {height}));
    }

    ~PriceFinalizationPruneLock()
    {
        WITH_LOCK(::cs_main, m_blockman.DeletePruneLock("enterprise-price-finalization"));
    }
};
} // namespace

EnterpriseIndex::EnterpriseIndex(std::unique_ptr<interfaces::Chain> chain, size_t n_cache_size, bool f_memory, bool f_wipe)
    : BaseIndex(std::move(chain), "enterpriseindex", "enterpriseidx"),
      m_db{std::make_unique<BaseIndex::DB>(gArgs.GetDataDirNet() / "indexes" / "enterpriseindex" / "db", n_cache_size, f_memory, f_wipe)},
      m_spool{gArgs.GetDataDirNet() / "enterprise" / "block_spool"},
      m_backfill_height{static_cast<int>(gArgs.GetIntArg("-enterprisebackfillheight", DEFAULT_ENTERPRISE_BACKFILL_HEIGHT))},
      m_price_finalization_lookback{static_cast<int>(gArgs.GetIntArg(
          "-enterprisepricefinalizationlookback",
          DEFAULT_ENTERPRISE_PRICE_FINALIZATION_LOOKBACK))},
      m_bootstrap_from_postgres{
          !f_wipe &&
          gArgs.GetBoolArg(
              "-enterprisebootstrapfrompostgres",
              DEFAULT_ENTERPRISE_BOOTSTRAP_FROM_POSTGRES)}
{
    if (f_wipe) {
        const std::vector<fs::path> stale_spool{m_spool.Pending()};
        if (!stale_spool.empty()) {
            m_reindex_with_pending_spool = true;
            LogError("enterprise: refusing automatic removal of %u pending block spool files for -reindex", stale_spool.size());
        }
    }

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

void EnterpriseIndex::BlockDisconnected(const std::shared_ptr<const CBlock>& block, const CBlockIndex* pindex)
{
    // BaseIndex normally rewinds only when the replacement BlockConnected
    // notification arrives. The SQL export must also reflect a tip
    // invalidation while the chain is temporarily one block shorter, so queue
    // the disconnect as soon as it is announced. BaseIndex may queue the same
    // idempotent delete again during its later rewind.
    if (!GetSummary().synced) return;
    const interfaces::BlockInfo block_info{kernel::MakeBlockInfo(pindex, block.get())};
    if (!CustomRemove(block_info)) {
        const std::string message{strprintf(
            "enterprise: failed to durably spool disconnected block %s",
            pindex->GetBlockHash().ToString())};
        LogError("%s", message);
        node::AbortNode(
            m_chain->context()->shutdown_request,
            m_chain->context()->exit_status,
            Untranslated(message),
            m_chain->context()->warnings.get());
    }
}

bool EnterpriseIndex::CustomInitBestBlock(std::optional<interfaces::BlockRef>& block)
{
    block.reset();
    if (!m_bootstrap_from_postgres) return true;
    if (m_backfill_height >= 0) {
        LogError("enterprise: PostgreSQL bootstrap requires -enterprisebackfillheight=-1");
        return false;
    }

    try {
        enterprise::ValidateEnterpriseSchema();
        const int chain_tip_height{WITH_LOCK(::cs_main, return m_chainstate->m_chain.Height())};
        const std::vector<enterprise::BlockRowKey> rows{
            enterprise::LoadBlockRowsThroughHeight(chain_tip_height)};

        enterprise::BlockRowKey verified_tip;
        {
            LOCK(::cs_main);
            const CChain& active_chain{m_chainstate->m_chain};
            verified_tip = enterprise::VerifyContiguousBlockRows(
                rows,
                active_chain.Height(),
                [&active_chain](int height) {
                    return Assert(active_chain[height])->GetBlockHash();
                });
        }

        block = interfaces::BlockRef{verified_tip.hash, verified_tip.height};
        LogInfo(
            "enterprise: verified contiguous PostgreSQL coverage against the active chain through height %d (%u rows)",
            verified_tip.height,
            rows.size());
        return true;
    } catch (const std::exception& e) {
        LogError("enterprise: PostgreSQL bootstrap verification failed: %s", e.what());
        return false;
    }
}

bool EnterpriseIndex::CustomInit(const std::optional<interfaces::BlockRef>&)
{
    if (m_reindex_with_pending_spool) {
        LogError("enterprise: archive or reconcile the pending block spool before restarting with -reindex");
        return false;
    }
    try {
        enterprise::ValidateEnterpriseSchema();
    } catch (const std::exception& e) {
        LogError("enterprise: PostgreSQL schema validation failed: %s", e.what());
        LogError("enterprise: apply and verify migration 20260711_pg17_reuse_v1 before restarting");
        return false;
    }
    LogInfo("enterprise: PostgreSQL 17 schema migration 20260711_pg17_reuse_v1 validated");
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
    delta.source = block.height <= m_backfill_height ? "denomination-backfill" : "index";
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
    {
        std::lock_guard<std::mutex> lock{m_writer_mutex};
        ++m_writer_wakeup_generation;
    }
    m_writer_cv.notify_all();
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
    enterprise::PgSession session;
    while (true) {
        uint64_t observed_generation;
        {
            std::lock_guard<std::mutex> lock{m_writer_mutex};
            if (m_writer_stop) return;
            observed_generation = m_writer_wakeup_generation;
        }

        bool drained{false};
        try {
            drained = DrainOnce(session);
        } catch (const std::exception& e) {
            session.Reset();
            LogError("enterprise: PostgreSQL writer iteration failed; durable spool retained for retry: %s", e.what());
        } catch (...) {
            session.Reset();
            LogError("enterprise: PostgreSQL writer iteration failed with an unknown error; durable spool retained for retry");
        }

        std::unique_lock<std::mutex> lock{m_writer_mutex};
        if (m_writer_stop) return;
        m_writer_cv.wait_for(lock, drained ? 5s : 30s, [this, observed_generation] {
            return m_writer_stop || m_writer_wakeup_generation != observed_generation;
        });
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
            const uint64_t observed_generation{m_writer_wakeup_generation};
            if (m_spool.UsageBytes() <= m_spool_max_bytes) continue;
            m_writer_cv.wait_for(lock, 5s, [this, &shutdown_interrupted, observed_generation] {
                return m_writer_stop || shutdown_interrupted() || m_writer_wakeup_generation != observed_generation;
            });
            if (m_writer_stop || shutdown_interrupted()) return true;
        }
    }

    if (logged) {
        LogInfo("enterprise: block spool drained to %d MiB; resuming validation", m_spool.UsageBytes() / (1024 * 1024));
    }
    return true;
}

bool EnterpriseIndex::DrainOnce(enterprise::PgSession& session)
{
    bool all_drained{true};
    std::vector<std::pair<fs::path, EnterpriseBlockSpoolFileInfo>> covered_connect_batch;

    auto flush_covered_connect_batch = [&] {
        if (covered_connect_batch.empty()) return true;
        const bool processed{ProcessCoveredConnectBatch(session, covered_connect_batch)};
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
        if (!ProcessSpoolFile(session, path)) {
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
    if (all_drained && FinalizeAvailablePrices(session)) {
        return true;
    }
    return all_drained;
}

bool EnterpriseIndex::ProcessCoveredConnectBatch(enterprise::PgSession& session, const std::vector<std::pair<fs::path, EnterpriseBlockSpoolFileInfo>>& batch)
{
    if (batch.empty()) return true;

    std::vector<enterprise::BlockRowKey> keys;
    keys.reserve(batch.size());
    for (const auto& [_, info] : batch) {
        keys.push_back(enterprise::BlockRowKey{info.height, info.block_hash});
    }

    std::set<std::pair<int, std::string>> matching_rows;
    try {
        pqxx::work work{session.Connection()};
        for (const enterprise::BlockRowKey& row : enterprise::FindCoveredBlockRows(work, keys, m_backfill_height)) {
            matching_rows.emplace(row.height, row.hash.GetHex());
        }
        work.commit();
    } catch (const std::exception& e) {
        session.Reset();
        LogError("enterprise: failed to batch-check spooled blocks in postgres: %s", e.what());
        return false;
    }

    for (const auto& [path, info] : batch) {
        if (matching_rows.contains({info.height, info.block_hash.GetHex()})) {
            if (!ProcessCoveredSpoolFile(session, path, info)) return false;
        } else if (!ProcessSpoolFile(session, path, ConnectRowCoverage::BATCH_UNCOVERED)) {
            return false;
        }
    }
    return true;
}

std::string EnterpriseIndex::IngestSource(const EnterpriseBlockDelta& delta) const
{
    if (delta.event_type == EnterpriseSpoolEventType::CONNECT &&
        m_backfill_height >= 0 &&
        delta.height <= m_backfill_height) {
        return "denomination-backfill";
    }
    return delta.source;
}

bool EnterpriseIndex::ProcessCoveredSpoolFile(enterprise::PgSession& session, const fs::path& path, const EnterpriseBlockSpoolFileInfo& info)
{
    EnterpriseBlockDelta delta;
    if (!m_spool.Read(path, delta)) return false;
    if (delta.event_type != EnterpriseSpoolEventType::CONNECT ||
        delta.height != info.height ||
        delta.block_hash != info.block_hash) {
        LogError("enterprise: covered spool filename does not match its payload: %s", fs::PathToString(path));
        return false;
    }

    delta.source = IngestSource(delta);
    try {
        // A database commit may have succeeded immediately before a crash or
        // shutdown prevented the durable spool file from being removed. Repair
        // the monitoring records atomically before acknowledging that file.
        pqxx::work work{session.Connection()};
        enterprise::ReconcileCoveredIngest(session, work, delta, path, delta.source);
        work.commit();
    } catch (const std::exception& e) {
        session.Reset();
        LogError("enterprise: failed to reconcile covered spooled block %s: %s", delta.block_hash.ToString(), e.what());
        return false;
    }

    const bool removed{m_spool.Remove(path)};
    if (removed) {
        NotifyWriter();
        LogDebug(BCLog::ALL, "enterprise: reconciled and removed already-covered block %s\n", delta.block_hash.ToString());
    }
    return removed;
}

bool EnterpriseIndex::ProcessSpoolFile(enterprise::PgSession& session, const fs::path& path, ConnectRowCoverage coverage)
{
    EnterpriseBlockDelta delta;
    if (!m_spool.Read(path, delta)) return false;
    delta.source = IngestSource(delta);

    if (delta.event_type == EnterpriseSpoolEventType::CONNECT && coverage == ConnectRowCoverage::CHECK) {
        try {
            if (enterprise::BlockRowCovered(delta.height, delta.block_hash, m_backfill_height)) {
                const EnterpriseBlockSpoolFileInfo info{
                    .event_type = delta.event_type,
                    .height = delta.height,
                    .block_hash = delta.block_hash,
                };
                return ProcessCoveredSpoolFile(session, path, info);
            }
        } catch (const std::exception& e) {
            LogError("enterprise: failed to check spooled block %s in postgres: %s", delta.block_hash.ToString(), e.what());
            return false;
        }
    }

    // FindCoveredBlockRows already checked batch-uncovered connect rows. The
    // database may change after that snapshot, so BlockToSql must still run:
    // its transactional write-mode check resolves concurrent inserts and
    // classifier updates before it changes the row.

    try {
        // Historical backfill transactions are idempotent, and a crash after
        // the block update is repaired by the covered-spool reconciliation
        // path. Non-historical writes retain a durable "started" record before
        // attempting the block transaction.
        if (delta.source != "denomination-backfill") {
            pqxx::work started_work{session.Connection()};
            enterprise::MarkIngestStarted(session, started_work, delta, path, delta.source);
            started_work.commit();
        }

        // The block mutation and its succeeded/gap bookkeeping are one durable
        // transaction. The spool file is removed only after this commit.
        pqxx::work work{session.Connection()};
        if (delta.event_type == EnterpriseSpoolEventType::DISCONNECT) {
            DeleteBlockFromSql(session, work, delta.block_hash);
        } else {
            const CBlockIndex* pindex{WITH_LOCK(::cs_main, return m_chainstate->m_blockman.LookupBlockIndex(delta.block_hash))};
            if (!pindex) {
                throw std::runtime_error(strprintf(
                    "cannot find block index entry for spooled block %s",
                    delta.block_hash.ToString()));
            }
            if (pindex->nHeight != delta.height) {
                throw std::runtime_error(strprintf(
                    "spooled block %s height mismatch: index=%d spool=%d",
                    delta.block_hash.ToString(),
                    pindex->nHeight,
                    delta.height));
            }
            BlockToSql block_to_sql{session, work, pindex, delta.block, delta.undo, delta.ScriptFlags()};
        }
        enterprise::MarkIngestSucceeded(session, work, delta, path, delta.source);
        work.commit();
    } catch (const pqxx::in_doubt_error& e) {
        // PostgreSQL may have committed even though the acknowledgement was
        // lost. Keep the durable spool file and let the next covered-row pass
        // reconcile the outcome; recording a failure here could overwrite a
        // successful ingest record and an immediate blind retry is unsafe.
        session.Reset();
        LogError(
            "enterprise: PostgreSQL commit outcome is unknown for spooled block %s; durable spool retained for reconciliation: %s",
            delta.block_hash.ToString(),
            e.what());
        return false;
    } catch (const std::exception& e) {
        session.Reset();
        enterprise::MarkIngestFailed(delta, path, delta.source, e.what());
        LogError("enterprise: failed to write spooled block %s to postgres: %s", delta.block_hash.ToString(), e.what());
        return false;
    }

    const bool removed{m_spool.Remove(path)};
    if (removed) NotifyWriter();
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
        const std::string source{
            m_backfill_height >= 0 && height <= m_backfill_height ? "denomination-backfill" : "local"};
        if (QueueLocalBackfill(*pindex)) {
            queued = true;
            try {
                enterprise::MarkGapQueued(height, pindex->GetBlockHash(), source);
            } catch (const std::exception& e) {
                // The block delta is already durable. Leave it queued and let
                // the normal spool path repair gap/ingest bookkeeping once
                // PostgreSQL is available again.
                LogWarning("enterprise: failed to mark queued gap at height %d; durable spool retained: %s", height, e.what());
            }
            NotifyWriter();
            continue;
        }
        enterprise::MarkGapUnavailable(height, pindex->GetBlockHash(), source, "block delta not available locally; rewind or run full -reindex to replay the enterprise index");
    }
    if (!queued) {
        m_next_gap_scan = now + 60s;
    }
    return queued;
}

bool EnterpriseIndex::FinalizeAvailablePrices(enterprise::PgSession& session)
{
    const auto now{NodeClock::now()};
    if (now < m_next_price_finalization_scan) return false;
    m_next_price_finalization_scan = now + PRICE_FINALIZATION_SCAN_INTERVAL;
    if (m_price_finalization_lookback == 0) return false;

    const int chain_tip_height{WITH_LOCK(::cs_main, return m_chainstate->m_chain.Height())};
    if (chain_tip_height < 0) return false;
    const int min_height{static_cast<int>(std::max<int64_t>(
        0,
        static_cast<int64_t>(chain_tip_height) - m_price_finalization_lookback))};

    uint64_t finalization_generation;
    {
        std::lock_guard<std::mutex> lock{m_writer_mutex};
        if (m_writer_stop) return false;
        finalization_generation = m_writer_wakeup_generation;
    }
    // A spool event may have arrived after DrainOnce() took its pending-file
    // snapshot. Always give chain updates priority over maintenance work.
    if (!m_spool.Pending().empty()) return false;

    for (auto it = m_price_finalization_deferred.begin(); it != m_price_finalization_deferred.end();) {
        if (it->first < min_height || it->first > chain_tip_height) {
            it = m_price_finalization_deferred.erase(it);
        } else {
            ++it;
        }
    }

    std::vector<enterprise::BlockRowKey> candidates;
    {
        pqxx::work work{session.Connection()};
        candidates = enterprise::FindPriceFinalizationCandidates(
            session,
            work,
            min_height,
            chain_tip_height,
            static_cast<std::size_t>(chain_tip_height - min_height) + 1);
        work.commit();
    }
    if (candidates.empty()) return false;

    const auto lowest_candidate{std::min_element(
        candidates.begin(),
        candidates.end(),
        [](const auto& left, const auto& right) { return left.height < right.height; })};
    PriceFinalizationPruneLock prune_lock{
        m_chainstate->m_blockman,
        lowest_candidate->height};

    std::size_t finalized{0};
    for (const enterprise::BlockRowKey& candidate : candidates) {
        {
            std::lock_guard<std::mutex> lock{m_writer_mutex};
            if (m_writer_stop || m_writer_wakeup_generation != finalization_generation) {
                return finalized > 0;
            }
        }

        const auto deferred_key{std::make_pair(candidate.height, candidate.hash.GetHex())};
        const CBlockIndex* pindex{
            WITH_LOCK(::cs_main, return m_chainstate->m_chain[candidate.height])};
        if (!pindex || pindex->GetBlockHash() != candidate.hash) {
            LogDebug(BCLog::ALL,
                     "enterprise: price finalization skipped inactive block row at height %d (%s)",
                     candidate.height,
                     candidate.hash.ToString());
            continue;
        }

        CBlock block;
        if (!m_chainstate->m_blockman.ReadBlock(block, *pindex)) {
            if (m_price_finalization_deferred.insert(deferred_key).second) {
                LogWarning(
                    "enterprise: price finalization cannot read retained block at height %d (%s); NULL price retained",
                    candidate.height,
                    candidate.hash.ToString());
            } else {
                LogDebug(BCLog::ALL,
                         "enterprise: price finalization still cannot read retained block at height %d (%s)\n",
                         candidate.height,
                         candidate.hash.ToString());
            }
            continue;
        }

        try {
            pqxx::work work{session.Connection()};
            const PriceFinalizationResult result{
                FinalizeBlockDenominationPrice(session, work, *pindex, block)};
            if (result == PriceFinalizationResult::UPDATED) {
                enterprise::MarkPriceFinalizationSucceeded(
                    session,
                    work,
                    candidate.height,
                    candidate.hash);
            } else if (result == PriceFinalizationResult::PRICE_UNAVAILABLE &&
                       m_price_finalization_deferred.insert(deferred_key).second) {
                LogWarning(
                    "enterprise: available price row is not a usable denomination window at height %d (%s); NULL price retained",
                    candidate.height,
                    candidate.hash.ToString());
            }
            work.commit();
            if (result == PriceFinalizationResult::UPDATED) {
                ++finalized;
                m_price_finalization_deferred.erase(deferred_key);
                LogInfo(
                    "enterprise: finalized BTC/USD denomination price at height %d (%s)",
                    candidate.height,
                    candidate.hash.ToString());
            } else if (result == PriceFinalizationResult::NOT_PENDING) {
                m_price_finalization_deferred.erase(deferred_key);
            }
            if (finalized >= DEFAULT_ENTERPRISE_PRICE_FINALIZATION_BATCH_SIZE) break;
        } catch (const pqxx::in_doubt_error& e) {
            session.Reset();
            LogError(
                "enterprise: PostgreSQL price-finalization commit outcome is unknown at height %d; NULL state will drive safe reconciliation: %s",
                candidate.height,
                e.what());
            break;
        } catch (const std::exception& e) {
            session.Reset();
            LogError(
                "enterprise: price finalization failed at height %d; NULL price retained for retry: %s",
                candidate.height,
                e.what());
            break;
        }
    }

    if (finalized == DEFAULT_ENTERPRISE_PRICE_FINALIZATION_BATCH_SIZE) {
        m_next_price_finalization_scan = NodeClock::now();
    }
    if (finalized > 0) {
        LogInfo(
            "enterprise: finalized %u block prices from available daily closes",
            finalized);
    }
    return finalized > 0;
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
    delta.source = block_index.nHeight <= m_backfill_height ? "denomination-backfill" : "local";
    delta.script_flags = GetBlockScriptFlags(block_index, m_chainstate->m_chainman).as_int();
    delta.block = std::move(block);
    delta.undo = std::move(undo);
    return m_spool.Write(delta);
}
