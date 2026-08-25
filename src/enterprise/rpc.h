#ifndef BITCOIN_ENTERPRISE_RPC_H
#define BITCOIN_ENTERPRISE_RPC_H

class CRPCTable;

namespace enterprise {
void RegisterEnterpriseRPCCommands(CRPCTable& t);
} // namespace enterprise

#endif // BITCOIN_ENTERPRISE_RPC_H
