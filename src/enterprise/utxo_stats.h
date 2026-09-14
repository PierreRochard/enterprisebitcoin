#ifndef BITCOIN_ENTERPRISE_UTXO_STATS_H
#define BITCOIN_ENTERPRISE_UTXO_STATS_H

#include <enterprise/utxo_set_to_sql.h>
#include <validationinterface.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_set>

namespace node {
struct NodeContext;
} // namespace node

namespace enterprise {

UtxoSetExportStats ExportCurrentUtxoSet(node::NodeContext& node, bool force, bool include_optional_tables);

class UtxoStatsExporter final : public CValidationInterface
{
public:
    explicit UtxoStatsExporter(node::NodeContext& node);
    ~UtxoStatsExporter();

    void Start();
    void Stop();
    void RequestCatchUp();
    //! During IBD, snapshot the live chainstate while this block is still tip.
    //! Called from ConnectTip with cs_main held. Must not open PostgreSQL.
    void MaybeExportOnConnect(const CBlockIndex& tip);

protected:
    void BlockConnected(const kernel::ChainstateRole& role, const std::shared_ptr<const CBlock>& block, const CBlockIndex* pindex) override;

private:
    struct SqlJob {
        UtxoAgeHistogram histogram;
        std::promise<UtxoSetExportStats> done;
    };

    void WorkerLoop();
    void RequestExport();
    void LoadCatalog();
    bool CatalogHasHeight(int height) const;
    int64_t CatalogMaxHeight() const;
    void CatalogAddHeight(int height);
    UtxoSetExportStats SubmitSql(UtxoAgeHistogram histogram);

    node::NodeContext& m_node;
    std::thread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_stop{false};
    bool m_pending{false};
    bool m_started{false};
    std::deque<std::shared_ptr<SqlJob>> m_sql_queue;

    mutable std::mutex m_catalog_mutex;
    std::unordered_set<int> m_exported_heights;
    int64_t m_db_max{-1};
    std::atomic<bool> m_catalog_ready{false};
};

extern std::shared_ptr<UtxoStatsExporter> g_utxo_stats;

} // namespace enterprise

#endif // BITCOIN_ENTERPRISE_UTXO_STATS_H
