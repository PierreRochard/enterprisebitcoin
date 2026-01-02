#ifndef UTXO_SET_TO_SQL_H
#define UTXO_SET_TO_SQL_H
#include <vector>
#include <map>
#include <algorithm>
#include <cmath>
#include <script/verify_flags.h>

std::map<int, double> calculatePercentiles(std::vector<double>& data);

class UtxoSetToSql {
public:
    UtxoSetToSql(CBlockIndex *block_index, const CBlock &block, CCoinsViewCache &view, script_verify_flags flags,
                 CCoinsViewCursor *cursor);
};

#endif //UTXO_SET_TO_SQL_H
