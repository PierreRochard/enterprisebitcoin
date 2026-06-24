#ifndef ENTERPRISE_BITCOIN_UTILITIES_H
#define ENTERPRISE_BITCOIN_UTILITIES_H

#include <script/solver.h>
#include <txmempool.h>

#include <cassert>

inline unsigned int GetTxnOutputTypeEnum(TxoutType t)
{
    switch (t) {
    case TxoutType::NONSTANDARD:
        return 1;
    case TxoutType::PUBKEY:
        return 2;
    case TxoutType::PUBKEYHASH:
        return 3;
    case TxoutType::SCRIPTHASH:
        return 4;
    case TxoutType::MULTISIG:
        return 5;
    case TxoutType::NULL_DATA:
        return 6;
    case TxoutType::WITNESS_V0_KEYHASH:
        return 7;
    case TxoutType::WITNESS_V0_SCRIPTHASH:
        return 8;
    case TxoutType::WITNESS_V1_TAPROOT:
        return 9;
    case TxoutType::WITNESS_UNKNOWN:
        return 10;
    case TxoutType::ANCHOR:
        return 11;
    }
    assert(false);
    return 0;
}

inline unsigned int GetMemPoolRemovalReasonEnum(MemPoolRemovalReason r)
{
    switch (r) {
    case MemPoolRemovalReason::EXPIRY:
        return 1;
    case MemPoolRemovalReason::SIZELIMIT:
        return 2;
    case MemPoolRemovalReason::REORG:
        return 3;
    case MemPoolRemovalReason::BLOCK:
        return 4;
    case MemPoolRemovalReason::CONFLICT:
        return 5;
    case MemPoolRemovalReason::REPLACED:
        return 6;
    }
    assert(false);
    return 0;
}

#endif // ENTERPRISE_BITCOIN_UTILITIES_H
