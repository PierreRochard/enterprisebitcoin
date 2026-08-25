#ifndef BITCOIN_ENTERPRISE_UTXO_SET_TO_SQL_H
#define BITCOIN_ENTERPRISE_UTXO_SET_TO_SQL_H

#include <consensus/amount.h>
#include <enterprise/options.h>
#include <uint256.h>

#include <cstdint>
#include <functional>
#include <string>
#include <utility>

class CBlockIndex;
class CCoinsViewCursor;

namespace enterprise {

struct UtxoSetExportStats {
    int height{-1};
    uint256 block_hash{};
    std::string median_time;
    uint64_t utxo_count{0};
    CAmount total_value{0};
    int64_t total_size{0};
    size_t age_buckets{0};
    bool skipped{false};
    std::string skip_reason;
    bool wrote_usd{false};
    bool wrote_optional{false};
};

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

UtxoSetExportStats ExportUtxoSetFromCursor(
    const CBlockIndex& block_index,
    CCoinsViewCursor& cursor,
    const std::function<void()>& interruption_point,
    bool include_optional_tables);

} // namespace enterprise

#endif // BITCOIN_ENTERPRISE_UTXO_SET_TO_SQL_H
