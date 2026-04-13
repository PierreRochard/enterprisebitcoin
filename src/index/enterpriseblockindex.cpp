#include <index/enterpriseblockindex.h>

#include <common/args.h>
#include <enterprise/block_to_sql.h>
#include <enterprise/common.h>
#include <enterprise/db.h>
#include <enterprise/schema_setup.h>
#include <enterprise/utxo_set_to_sql.h>
#include <interfaces/chain.h>
#include <interfaces/types.h>
#include <kernel/cs_main.h>
#include <util/thread.h>
#include <util/fs.h>
#include <validation.h>

#include <algorithm>
#include <chrono>
#include <memory>
#include <utility>

namespace {

constexpr size_t MIN_FLOW_WORKER_COUNT{4};
constexpr size_t MAX_FLOW_WORKER_COUNT{8};

size_t FlowWorkerCount()
{
    const unsigned int hardware_threads = std::thread::hardware_concurrency();
    if (hardware_threads == 0) {
        return MIN_FLOW_WORKER_COUNT;
    }
    return std::min(MAX_FLOW_WORKER_COUNT,
                    std::max(MIN_FLOW_WORKER_COUNT, static_cast<size_t>(hardware_threads / 4)));
}

} // namespace

std::unique_ptr<EnterpriseBlockIndex> g_enterprise_block_index;

EnterpriseBlockIndex::EnterpriseBlockIndex(std::unique_ptr<interfaces::Chain> chain, size_t n_cache_size, bool f_memory, bool f_wipe)
    : BaseIndex(std::move(chain), "enterpriseblockindex"),
      m_db(std::make_unique<BaseIndex::DB>(gArgs.GetDataDirNet() / "indexes" / "enterpriseblockindex" / "db", n_cache_size, f_memory, f_wipe)),
      m_network(enterprise::ChainName())
{
}

EnterpriseBlockIndex::~EnterpriseBlockIndex()
{
    StopFlowWorker();
}

void EnterpriseBlockIndex::StartFlowWorker()
{
    if (!m_flow_workers.empty()) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_flow_worker_mutex);
        m_flow_worker_shutdown = false;
    }

    const size_t flow_worker_count = FlowWorkerCount();
    m_flow_workers.reserve(flow_worker_count);
    for (size_t worker_index = 0; worker_index < flow_worker_count; ++worker_index) {
        (void)worker_index;
        m_flow_workers.emplace_back(&util::TraceThread, "entflows", [this] { RunFlowWorker(); });
    }
}

void EnterpriseBlockIndex::StopFlowWorker()
{
    {
        std::lock_guard<std::mutex> lock(m_flow_worker_mutex);
        m_flow_worker_shutdown = true;
    }
    m_flow_worker_cv.notify_all();
    for (auto& worker : m_flow_workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    m_flow_workers.clear();
}

void EnterpriseBlockIndex::WakeFlowWorker()
{
    m_flow_worker_cv.notify_all();
}

void EnterpriseBlockIndex::RunFlowWorker()
{
    for (;;) {
        {
            std::unique_lock<std::mutex> lock(m_flow_worker_mutex);
            if (m_flow_worker_shutdown) {
                break;
            }
        }

        if (ProcessNextQueuedBlockAddressFlowExport(*m_chain, *m_chainstate, m_network)) {
            continue;
        }

        std::unique_lock<std::mutex> lock(m_flow_worker_mutex);
        m_flow_worker_cv.wait_for(lock, std::chrono::seconds{5}, [this] { return m_flow_worker_shutdown; });
        if (m_flow_worker_shutdown) {
            break;
        }
    }
}

bool EnterpriseBlockIndex::CustomInit(const std::optional<interfaces::BlockRef>& block)
{
    auto& conn = enterprise::PgConnection();
    enterprise::EnsureBlockExportSchema(conn, m_network);
    enterprise::EnsureUtxoSnapshotSchema(conn, m_network);

    if (block) {
        RewindBlockExportToHeight(m_network, block->height);
    }

    StartFlowWorker();
    WakeFlowWorker();
    SchedulePendingUtxoSnapshotRetries(*m_chain);
    return true;
}

bool EnterpriseBlockIndex::CustomAppend(const interfaces::BlockInfo& block)
{
    const CBlockIndex* block_index;
    {
        LOCK(cs_main);
        block_index = m_chainstate->m_blockman.LookupBlockIndex(block.hash);
    }
    if (block_index == nullptr) {
        LogError("%s: missing block index entry for %s", GetName(), block.hash.ToString());
        return false;
    }

    const script_verify_flags flags{GetBlockScriptFlags(*block_index, m_chainstate->m_chainman)};
    try {
        BlockToSql writer(block, *block_index, flags);
        WakeFlowWorker();
    } catch (const std::exception& e) {
        LogError("%s: failed to export block %s to PostgreSQL: %s", GetName(), block.hash.ToString(), e.what());
        return false;
    }
    return true;
}

bool EnterpriseBlockIndex::CustomRemove(const interfaces::BlockInfo& block)
{
    try {
        RemoveBlockFromSql(m_network, block.hash);
    } catch (const std::exception& e) {
        LogError("%s: failed to remove block %s from PostgreSQL: %s", GetName(), block.hash.ToString(), e.what());
        return false;
    }
    return true;
}

void EnterpriseBlockIndex::BlockDisconnected(const std::shared_ptr<const CBlock>& block, const CBlockIndex* pindex)
{
    (void)block;

    if (pindex == nullptr) {
        return;
    }

    try {
        RemoveBlockFromSql(m_network, pindex->GetBlockHash());
    } catch (const std::exception& e) {
        LogError("%s: failed to remove disconnected block %s from PostgreSQL: %s",
                 GetName(), pindex->GetBlockHash().ToString(), e.what());
    }
}

interfaces::Chain::NotifyOptions EnterpriseBlockIndex::CustomOptions()
{
    interfaces::Chain::NotifyOptions options;
    options.connect_undo_data = true;
    return options;
}
