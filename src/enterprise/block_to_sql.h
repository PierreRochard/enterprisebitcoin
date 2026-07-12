#ifndef BLOCK_TO_SQL_H
#define BLOCK_TO_SQL_H

#include <chain.h>
#include <coins.h>
#include <consensus/amount.h>
#include <consensus/tx_verify.h>
#include <enterprise/pg.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/verify_flags.h>
#include <txmempool.h>
#include <uint256.h>

#include <cstddef>
#include <cstdint>
#include <string>

class CBlockUndo;

struct FeeData {
    unsigned int fee;
    unsigned int size;
    unsigned int vsize;
    unsigned int weight;
};

struct TransactionData {
    std::size_t m_transaction_index;
    const CTransactionRef &m_transaction;

    CAmount total_output_value = 0;
    CAmount total_input_value = 0;
    CAmount fees = 0;
    int64_t utxo_size_inc = 0;
    unsigned int weight;
    unsigned int vsize;

    std::string transaction_hash;
    bool is_coinbase;

    TransactionData(std::size_t transaction_index, const CTransactionRef &transaction);

    CAmount GetFee() {
        return is_coinbase ? 0 : total_input_value - total_output_value;
    };

    CAmount GetFeeRate() {
        return this->GetFee() / this->vsize;
    };
};

class BlockToSql {
public:
    BlockToSql(enterprise::PgSession& session, pqxx::work& work, const CBlockIndex* block_index, const CBlock& block,
               CCoinsViewCache& view, script_verify_flags flags, CCoinsViewCursor* cursor);
    BlockToSql(enterprise::PgSession& session, pqxx::work& work, const CBlockIndex* block_index, const CBlock& block,
               const CBlockUndo& undo, script_verify_flags flags);
    BlockToSql(const CBlockIndex *block_index, const CBlock &block, CCoinsViewCache &view, script_verify_flags flags,
               CCoinsViewCursor *cursor);
    BlockToSql(const CBlockIndex* block_index, const CBlock& block, const CBlockUndo& undo, script_verify_flags flags);
};

void DeleteBlockFromSql(const uint256& hash);
void DeleteBlockFromSql(enterprise::PgSession& session, pqxx::work& work, const uint256& hash);

#endif //BLOCK_TO_SQL_H
