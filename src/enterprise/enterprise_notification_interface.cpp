#include <primitives/block.h>
#include <chain.h>
#include <logging.h>

#include <enterprise/enterprise_notification_interface.h>
#include <enterprise/block_to_sql.h>


void EnterpriseNotificationInterface::BlockConnected(const std::shared_ptr<const CBlock>& block,
                                                     const CBlockIndex* pindex) {
    BlockToSql block_to_sql(*pindex, *block);
};
