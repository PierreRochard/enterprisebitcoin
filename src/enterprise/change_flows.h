#ifndef BITCOIN_ENTERPRISE_CHANGE_FLOWS_H
#define BITCOIN_ENTERPRISE_CHANGE_FLOWS_H

#include <consensus/amount.h>
#include <primitives/transaction.h>

#include <cstdint>
#include <vector>

namespace enterprise {

//! A coin a transaction spends: the output and the height that created it.
struct SpentCoin {
    CTxOut out;
    int created_height;
};

//! A same-day forward spends one coin of at least this value...
inline constexpr CAmount FORWARD_MIN_VALUE{100 * COIN};
//! ...created fewer than this many blocks earlier...
inline constexpr int FORWARD_MAX_AGE_BLOCKS{144};
//! ...and passes at least this many thousandths of it to a different script in
//! one of exactly two outputs.
inline constexpr int64_t FORWARD_KEEP_PER_MILLE{990};

//! Output value a block's transactions send back to their own wallets, which
//! raw output value counts as if it were paid to someone else:
//!
//!   self-change  outputs paying a script that one of the same transaction's
//!                inputs spends: change returned to a reused address
//!   forwards     the kept output of a same-day forward, as defined above: a
//!                wallet moving a large, freshly received coin on to a new
//!                address of its own, as the 2021-22 hop chains did
//!
//! The two never count the same output: a forward's kept output pays a
//! different script than the coin it spends.
class ChangeFlowStats
{
public:
    //! Count one transaction spending `spent`, in input order, in a block at `height`.
    void AddTransaction(const CTransaction& tx, const std::vector<SpentCoin>& spent, int height);

    [[nodiscard]] int64_t SelfChangeOutputs() const { return m_self_change_outputs; }
    [[nodiscard]] CAmount SelfChangeValue() const { return m_self_change_value; }
    [[nodiscard]] int64_t ForwardOutputs() const { return m_forward_outputs; }
    [[nodiscard]] CAmount ForwardValue() const { return m_forward_value; }

private:
    int64_t m_self_change_outputs{0};
    CAmount m_self_change_value{0};
    int64_t m_forward_outputs{0};
    CAmount m_forward_value{0};
};

} // namespace enterprise

#endif // BITCOIN_ENTERPRISE_CHANGE_FLOWS_H
