#ifndef ENTERPRISE_MEMPOOL_TO_SQL_H
#define ENTERPRISE_MEMPOOL_TO_SQL_H

#include <kernel/mempool_entry.h>
#include <kernel/mempool_removal_reason.h>
#include <primitives/transaction_identifier.h>

class MempoolEntryToSql {
public:
    explicit MempoolEntryToSql(const CTxMemPoolEntry& mempool_entry);
};

class RemoveMempoolEntry {
public:
    RemoveMempoolEntry(const Txid& hash, MemPoolRemovalReason reason);
};

#endif // ENTERPRISE_MEMPOOL_TO_SQL_H
