#ifndef BITCOIN_ENTERPRISE_NOTIFICATION_INTERFACE_H
#define BITCOIN_ENTERPRISE_NOTIFICATION_INTERFACE_H

#include <validationinterface.h>

class EnterpriseNotificationInterface final : public CValidationInterface {
private:
    void BlockConnected(const std::shared_ptr<const CBlock>& block, const CBlockIndex* pindex) override;
};

#endif // BITCOIN_ENTERPRISE_NOTIFICATION_INTERFACE_H