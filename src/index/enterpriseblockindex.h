#ifndef BITCOIN_INDEX_ENTERPRISEBLOCKINDEX_H
#define BITCOIN_INDEX_ENTERPRISEBLOCKINDEX_H

#include <index/base.h>

#include <memory>
#include <string>

namespace interfaces {
class Chain;
}

class EnterpriseBlockIndex final : public BaseIndex
{
private:
    std::unique_ptr<BaseIndex::DB> m_db;
    const std::string m_network;

    bool AllowPrune() const override { return false; }

protected:
    bool CustomInit(const std::optional<interfaces::BlockRef>& block) override;
    bool CustomAppend(const interfaces::BlockInfo& block) override;
    bool CustomRemove(const interfaces::BlockInfo& block) override;
    interfaces::Chain::NotifyOptions CustomOptions() override;
    void BlockDisconnected(const std::shared_ptr<const CBlock>& block, const CBlockIndex* pindex) override;
    BaseIndex::DB& GetDB() const override { return *m_db; }

public:
    explicit EnterpriseBlockIndex(std::unique_ptr<interfaces::Chain> chain, size_t n_cache_size, bool f_memory = false, bool f_wipe = false);
};

extern std::unique_ptr<EnterpriseBlockIndex> g_enterprise_block_index;

#endif // BITCOIN_INDEX_ENTERPRISEBLOCKINDEX_H
