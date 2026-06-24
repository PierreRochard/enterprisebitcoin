#ifndef UTXO_SET_TO_SQL_H
#define UTXO_SET_TO_SQL_H

#include <chain.h>
#include <coins.h>
#include <script/verify_flags.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

std::map<int, double> calculatePercentiles(std::vector<double>& data);

class UtxoSetToSql {
public:
    UtxoSetToSql(const CBlockIndex *block_index, const CBlock &block, CCoinsViewCache &view, script_verify_flags flags,
                               CCoinsViewCursor *cursor);
};

#endif //UTXO_SET_TO_SQL_H
