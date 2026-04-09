#ifndef BLOCK_TO_SQL_H
#define BLOCK_TO_SQL_H

#include <kernel/chain.h>
#include <primitives/transaction.h>
#include <script/verify_flags.h>
#include <uint256.h>

#include <cstddef>
#include <string>
#include <string_view>

class CBlockIndex;

struct FeeData {
    unsigned int fee;
    unsigned int size;
    unsigned int vsize;
    unsigned int weight;
};

struct TransactionData {
    const std::size_t m_transaction_index;
    const CTransactionRef& m_transaction;

    CAmount total_output_value = 0;
    CAmount total_input_value = 0;
    CAmount fees = 0;
    int64_t utxo_size_inc = 0;
    unsigned int weight;
    unsigned int vsize;

    std::string transaction_hash;
    bool is_coinbase;

    TransactionData(std::size_t transaction_index, const CTransactionRef& transaction);

    CAmount GetFee()
    {
        return is_coinbase ? 0 : total_input_value - total_output_value;
    }

    CAmount GetFeeRate()
    {
        return this->GetFee() / this->vsize;
    }
};

class BlockToSql {
public:
    BlockToSql(const interfaces::BlockInfo& block_info, const CBlockIndex& block_index, script_verify_flags flags);
};

void RemoveBlockFromSql(std::string_view network, const uint256& block_hash);

#endif // BLOCK_TO_SQL_H
