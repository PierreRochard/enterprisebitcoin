#ifndef UTXO_SET_TO_SQL_H
#define UTXO_SET_TO_SQL_H

#include <cstdint>
#include <script/verify_flags.h>
#include <vector>
#include <map>

namespace interfaces {
class Chain;
}

std::map<int, double> calculatePercentiles(std::vector<double>& data);
constexpr int64_t UTXO_EXPORT_INTERVAL{4032};
class CBlockIndex;
class CBlock;
class CCoinsViewCache;
class CCoinsViewCursor;
bool ShouldExportUtxoSetToSql(const CBlockIndex& block_index);
void RemoveUtxoSnapshotFromSql(const CBlockIndex& block_index);
void SchedulePendingUtxoSnapshotRetries(interfaces::Chain& chain);

class UtxoSetToSql {
public:
    UtxoSetToSql(CBlockIndex *block_index, const CBlock &block, CCoinsViewCache &view, script_verify_flags flags,
                 CCoinsViewCursor *cursor);
};

#endif //UTXO_SET_TO_SQL_H
