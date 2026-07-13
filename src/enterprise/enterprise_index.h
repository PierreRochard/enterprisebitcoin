#ifndef BITCOIN_ENTERPRISE_INDEX_H
#define BITCOIN_ENTERPRISE_INDEX_H

#include <enterprise/block_spool.h>
#include <enterprise/options.h>
#include <index/base.h>
#include <interfaces/chain.h>
#include <sync.h>
#include <util/fs.h>
#include <util/time.h>

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace enterprise {
class PgSession;
}

class EnterpriseIndex final : public BaseIndex
{
private:
    enum class ConnectRowCoverage {
        CHECK,
        BATCH_UNCOVERED,
    };

    std::unique_ptr<BaseIndex::DB> m_db;
    EnterpriseBlockSpool m_spool;

    std::thread m_writer_thread;
    std::mutex m_writer_mutex;
    std::condition_variable m_writer_cv;
    bool m_writer_stop{false};
    uint64_t m_writer_wakeup_generation{0};
    uint64_t m_spool_max_bytes{0};
    int m_backfill_height{static_cast<int>(DEFAULT_ENTERPRISE_BACKFILL_HEIGHT)};
    int m_price_finalization_lookback{DEFAULT_ENTERPRISE_PRICE_FINALIZATION_LOOKBACK};
    bool m_reindex_with_pending_spool{false};
    std::set<std::pair<int, std::string>> m_price_finalization_deferred;

    bool AllowPrune() const override { return true; }

    void NotifyWriter();
    void StartWriter();
    void StopWriter();
    void WriterLoop();
    bool ApplySpoolBackpressure();
    bool DrainOnce(enterprise::PgSession& session);
    bool ProcessCoveredConnectBatch(enterprise::PgSession& session, const std::vector<std::pair<fs::path, EnterpriseBlockSpoolFileInfo>>& batch);
    bool ProcessCoveredSpoolFile(enterprise::PgSession& session, const fs::path& path, const EnterpriseBlockSpoolFileInfo& info);
    bool ProcessSpoolFile(enterprise::PgSession& session, const fs::path& path, ConnectRowCoverage coverage = ConnectRowCoverage::CHECK);
    bool ReconcileGaps();
    bool FinalizeAvailablePrices(enterprise::PgSession& session);
    bool QueueLocalBackfill(const CBlockIndex& block_index);
    std::string IngestSource(const EnterpriseBlockDelta& delta) const;

    NodeClock::time_point m_next_gap_scan{};
    NodeClock::time_point m_next_price_finalization_scan{};

protected:
    void BlockDisconnected(const std::shared_ptr<const CBlock>& block, const CBlockIndex* pindex) override;
    bool CustomInit(const std::optional<interfaces::BlockRef>& block) override;
    bool CustomAppend(const interfaces::BlockInfo& block) override;
    bool CustomRemove(const interfaces::BlockInfo& block) override;
    interfaces::Chain::NotifyOptions CustomOptions() override;
    BaseIndex::DB& GetDB() const override { return *m_db; }

public:
    explicit EnterpriseIndex(std::unique_ptr<interfaces::Chain> chain, size_t n_cache_size, bool f_memory = false, bool f_wipe = false);
    ~EnterpriseIndex() override;
    void Stop() override;
};

extern std::unique_ptr<EnterpriseIndex> g_enterprise_index;

#endif // BITCOIN_ENTERPRISE_INDEX_H
