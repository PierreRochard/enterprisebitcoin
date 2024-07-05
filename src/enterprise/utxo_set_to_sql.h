#ifndef UTXO_SET_TO_SQL_H
#define UTXO_SET_TO_SQL_H
#include <vector>
#include <map>
#include <algorithm>
#include <cmath>

std::map<int, double> calculatePercentiles(std::vector<double>& data);

class UtxoSetToSql {
public:
    UtxoSetToSql(CBlockIndex *block_index, const CBlock &block, CCoinsViewCache &view, unsigned int flags,
                               CCoinsViewCursor *cursor);
};

#endif //UTXO_SET_TO_SQL_H
