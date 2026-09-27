#ifndef BITCOIN_ENTERPRISE_UTXO_SET_TO_SQL_H
#define BITCOIN_ENTERPRISE_UTXO_SET_TO_SQL_H

#include <consensus/amount.h>
#include <enterprise/options.h>
#include <uint256.h>

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

class CBlockIndex;
class CCoinsViewCursor;
class CScript;

namespace enterprise {

struct UtxoSetExportStats {
    int height{-1};
    uint256 block_hash{};
    std::string median_time;
    uint64_t utxo_count{0};
    CAmount total_value{0};
    int64_t total_size{0};
    size_t age_buckets{0};
    uint64_t funded_scripts{0}; //!< scripts with a positive balance
    size_t address_bins{0};
    bool skipped{false};
    std::string skip_reason;
    bool wrote_usd{false};
    bool wrote_optional{false};
};

struct UtxoAgeHistogramRow {
    unsigned int weeks_old{0};
    CAmount utxo_value{0};
    uint64_t utxo_count{0};
    int64_t utxo_size{0};
};

//! Funded scripts (addresses) whose balance falls in [lower_bound, upper_bound).
struct AddressBalanceBin {
    CAmount lower_bound{0};
    CAmount upper_bound{0};
    uint64_t address_count{0};
    CAmount balance{0};
};

//! One point-in-time scan of the UTXO set: the utxo_age rows plus the
//! address-balance histogram, written to PostgreSQL in one transaction.
struct UtxoAgeHistogram {
    UtxoSetExportStats stats;
    std::vector<UtxoAgeHistogramRow> rows;
    std::vector<AddressBalanceBin> address_bins;
};

//! Running balance per scriptPubKey, keyed by a 64-bit SipHash of the script.
//! Open addressing with linear probing: 16 bytes per slot, so the full
//! mainnet set (tens of millions of funded scripts) fits in a few GB.
class ScriptBalanceMap
{
public:
    static uint64_t Key(const CScript& script);

    void Add(uint64_t key, CAmount value);
    size_t size() const { return m_size; }

    template <typename F>
    void ForEach(F&& f) const
    {
        for (const auto& slot : m_slots) {
            if (slot.key != EMPTY) f(slot.key, slot.balance);
        }
    }

private:
    static constexpr uint64_t EMPTY{0};
    struct Slot {
        uint64_t key{EMPTY};
        CAmount balance{0};
    };

    void Grow();

    std::vector<Slot> m_slots;
    size_t m_size{0};
};

//! Lower bounds (satoshis) of the address-balance histogram bins: 100
//! log-spaced bins per power of ten, rounded up to whole satoshis and
//! deduplicated. Every power of ten is an exact edge, so any bracket that
//! starts at a power of ten is an exact sum of bins.
const std::vector<CAmount>& AddressBalanceBinEdges();

//! Index into AddressBalanceBinEdges() of the bin holding a positive balance.
size_t AddressBalanceBinIndex(CAmount balance);

//! Collapse per-script balances into the non-empty histogram bins, ascending.
std::vector<AddressBalanceBin> BuildAddressBalanceHistogram(const ScriptBalanceMap& balances);

//! Funded scripts (positive balance) across the bins. Scripts holding only
//! zero-value outputs are in the map but not in any bin.
uint64_t CountAddresses(const std::vector<AddressBalanceBin>& bins);

//! Age in whole weeks, matching the original exporter: round((tip-coin)/1008).
unsigned int CoinAgeWeeks(int snapshot_height, int coin_height);

//! Log10 satoshi buckets used by utxo_balances. Zero and negative map to [0, 1).
std::pair<CAmount, CAmount> ValueMagnitudeBounds(CAmount value);

double PercentRounded(double part, double total);

//! Decide whether the current chain tip should be snapshotted.
//! force: RPC one-shot, ignoring cadence.
//! height_already_exported: skip unless force is set, and even then the writer
//! is idempotent on (block_height, weeks_old).
bool ShouldExportUtxoSet(int height, int64_t db_max_height, bool height_already_exported, bool force);

int64_t MaxExportedUtxoHeight();
bool UtxoHeightExported(int height);
void LoadExportedUtxoHeights(std::unordered_set<int>& heights, int64_t& max_height);

//! Scan the live UTXO set into an in-memory age histogram. Does not open PostgreSQL.
UtxoAgeHistogram ScanUtxoAgeFromCursor(
    const CBlockIndex& block_index,
    CCoinsViewCursor& cursor,
    const std::function<void()>& interruption_point);

//! INSERT the histogram on the calling thread. Must not run on the validation thread.
void WriteUtxoAgeHistogram(const UtxoAgeHistogram& histogram);

UtxoSetExportStats ExportUtxoSetFromCursor(
    const CBlockIndex& block_index,
    CCoinsViewCursor& cursor,
    const std::function<void()>& interruption_point,
    bool include_optional_tables);

} // namespace enterprise

#endif // BITCOIN_ENTERPRISE_UTXO_SET_TO_SQL_H
