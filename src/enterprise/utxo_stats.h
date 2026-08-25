#ifndef BITCOIN_ENTERPRISE_UTXO_STATS_H
#define BITCOIN_ENTERPRISE_UTXO_STATS_H

#include <enterprise/utxo_set_to_sql.h>
#include <validationinterface.h>

#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

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

protected:
    void BlockConnected(const kernel::ChainstateRole& role, const std::shared_ptr<const CBlock>& block, const CBlockIndex* pindex) override;

private:
    void WorkerLoop();
    void RequestExport();

    node::NodeContext& m_node;
    std::thread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_stop{false};
    bool m_pending{false};
    bool m_started{false};
};

extern std::shared_ptr<UtxoStatsExporter> g_utxo_stats;

} // namespace enterprise

#endif // BITCOIN_ENTERPRISE_UTXO_STATS_H
