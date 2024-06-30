#ifndef UTXO_SET_TO_SQL_H
#define UTXO_SET_TO_SQL_H

class UtxoSetToSql {
public:
    UtxoSetToSql(CBlockIndex *block_index, const CBlock &block, CCoinsViewCache &view, unsigned int flags,
                               CCoinsViewCursor *cursor);
};

#endif //UTXO_SET_TO_SQL_H
