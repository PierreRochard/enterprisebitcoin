#include <enterprise/change_flows.h>

#include <script/script.h>

#include <set>

namespace enterprise {

void ChangeFlowStats::AddTransaction(const CTransaction& tx, const std::vector<SpentCoin>& spent, int height)
{
    if (tx.IsCoinBase() || spent.empty()) return;

    std::set<CScript> spent_scripts;
    for (const SpentCoin& coin : spent) {
        spent_scripts.insert(coin.out.scriptPubKey);
    }
    for (const CTxOut& out : tx.vout) {
        if (spent_scripts.count(out.scriptPubKey)) {
            m_self_change_outputs += 1;
            m_self_change_value += out.nValue;
        }
    }

    if (spent.size() != 1 || tx.vout.size() != 2) return;
    const SpentCoin& coin{spent.front()};
    if (coin.out.nValue < FORWARD_MIN_VALUE || height - coin.created_height >= FORWARD_MAX_AGE_BLOCKS) return;
    const CTxOut& kept{tx.vout[0].nValue >= tx.vout[1].nValue ? tx.vout[0] : tx.vout[1]};
    // Values stay below 2.1e15 satoshis, so the products fit in 64 bits.
    if (kept.nValue * 1000 >= coin.out.nValue * FORWARD_KEEP_PER_MILLE && kept.scriptPubKey != coin.out.scriptPubKey) {
        m_forward_outputs += 1;
        m_forward_value += kept.nValue;
    }
}

} // namespace enterprise
