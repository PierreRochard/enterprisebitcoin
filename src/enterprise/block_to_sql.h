#ifndef BLOCK_TO_SQL_H
#define BLOCK_TO_SQL_H

#include <chain.h>
#include <coins.h>
#include <validation.h>
#include <consensus/tx_verify.h>
#include <txmempool.h>


struct FeeData {
    unsigned int fee;
    unsigned int size;
    unsigned int vsize;
    unsigned int weight;
};

struct TransactionData {
    const int &m_transaction_index;
    const CTransactionRef &m_transaction;

    CAmount total_output_value = 0;
    CAmount total_input_value = 0;
    CAmount fees = 0;
    int64_t utxo_size_inc = 0;
    unsigned int weight;
    unsigned int vsize;

    std::string transaction_hash;
    bool is_coinbase;

    TransactionData(const int &transaction_index, const CTransactionRef &transaction);

    CAmount GetFee() {
        return is_coinbase ? 0 : total_input_value - total_output_value;
    };

    CAmount GetFeeRate() {
        return this->GetFee() / this->vsize;
    };
};

class BlockToSql {
public:
    BlockToSql(CBlockIndex *block_index, const CBlock &block, CCoinsViewCache &view, unsigned int flags);
};

#endif //BLOCK_TO_SQL_H
