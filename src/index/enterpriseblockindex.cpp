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
#include <util/fs.h>
#include <validation.h>

#include <memory>
#include <utility>

std::unique_ptr<EnterpriseBlockIndex> g_enterprise_block_index;

EnterpriseBlockIndex::EnterpriseBlockIndex(std::unique_ptr<interfaces::Chain> chain, size_t n_cache_size, bool f_memory, bool f_wipe)
    : BaseIndex(std::move(chain), "enterpriseblockindex"),
      m_db(std::make_unique<BaseIndex::DB>(gArgs.GetDataDirNet() / "indexes" / "enterpriseblockindex" / "db", n_cache_size, f_memory, f_wipe)),
      m_network(enterprise::ChainName())
{
}

bool EnterpriseBlockIndex::CustomInit(const std::optional<interfaces::BlockRef>& block)
{
    auto& conn = enterprise::PgConnection();
    enterprise::EnsureBlockExportSchema(conn, m_network);
    enterprise::EnsureUtxoSnapshotSchema(conn, m_network);

    if (block) {
        // Drop any block rows that got ahead of the index's committed best block.
        pqxx::work w(conn);
        w.exec(pqxx::zview{"DELETE FROM blocks WHERE network = $1 AND height > $2"},
               pqxx::params{m_network, block->height});
        w.commit();
    }

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
