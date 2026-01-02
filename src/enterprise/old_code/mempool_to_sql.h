class MempoolEntryToSql {
public:
    MempoolEntryToSql(CTxMemPoolEntry mempool_entry);
};

class RemoveMempoolEntry {
public:
    RemoveMempoolEntry(const uint256 hash, MemPoolRemovalReason reason);
};