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
#include <thread>
#include <utility>
#include <vector>

class EnterpriseIndex final : public BaseIndex
{
private:
    std::unique_ptr<BaseIndex::DB> m_db;
    EnterpriseBlockSpool m_spool;

    std::thread m_writer_thread;
    std::mutex m_writer_mutex;
    std::condition_variable m_writer_cv;
    bool m_writer_stop{false};
    uint64_t m_spool_max_bytes{0};

    bool AllowPrune() const override { return true; }

    void NotifyWriter();
    void StartWriter();
    void StopWriter();
    void WriterLoop();
    bool ApplySpoolBackpressure();
    bool DrainOnce();
    bool ProcessCoveredConnectBatch(const std::vector<std::pair<fs::path, EnterpriseBlockSpoolFileInfo>>& batch);
    bool ProcessSpoolFile(const fs::path& path);
    bool ReconcileGaps();
    bool QueueLocalBackfill(const CBlockIndex& block_index);

    NodeClock::time_point m_next_gap_scan{};

protected:
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
