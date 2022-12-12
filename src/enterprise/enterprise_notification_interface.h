#ifndef BITCOIN_ENTERPRISE_NOTIFICATION_INTERFACE_H
#define BITCOIN_ENTERPRISE_NOTIFICATION_INTERFACE_H

#include <validationinterface.h>
#include <validation.h>

class EnterpriseNotificationInterface final : public CValidationInterface {
public:
    EnterpriseNotificationInterface(Chainstate& active_chainstate);
private:
    Chainstate* m_chainstate{nullptr};
    void BlockConnected(const std::shared_ptr<const CBlock>& block, const CBlockIndex* pindex) override;
};

#endif // BITCOIN_ENTERPRISE_NOTIFICATION_INTERFACE_H