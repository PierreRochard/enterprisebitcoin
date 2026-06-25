#ifndef BITCOIN_ENTERPRISE_PG_H
#define BITCOIN_ENTERPRISE_PG_H

#include <enterprise/block_delta.h>
#include <util/fs.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace enterprise {

struct BlockTableCoverage
{
    int chain_tip_height{-1};
    int64_t expected_blocks{0};
    int64_t populated_heights{0};
    int64_t missing_heights{0};
    int64_t duplicate_rows{0};
    int64_t unavailable_heights{0};

    bool Complete() const
    {
        return expected_blocks == populated_heights && missing_heights == 0 && duplicate_rows == 0 && unavailable_heights == 0;
    }
};

struct BlockRowKey {
    int height{-1};
    uint256 hash{};
};

[[nodiscard]] std::string Network();

void MarkIngestStarted(const EnterpriseBlockDelta& delta, const fs::path& spool_path, const std::string& source);
void MarkIngestSucceeded(const EnterpriseBlockDelta& delta, const fs::path& spool_path, const std::string& source);
void MarkIngestFailed(const EnterpriseBlockDelta& delta, const fs::path& spool_path, const std::string& source, const std::string& error);

[[nodiscard]] std::vector<int> FindBlockTableGaps(int chain_tip_height, std::size_t limit);
[[nodiscard]] BlockTableCoverage GetBlockTableCoverage(int chain_tip_height);
[[nodiscard]] bool BlockRowMatches(int height, const uint256& expected_hash);
[[nodiscard]] std::vector<BlockRowKey> FindMatchingBlockRows(const std::vector<BlockRowKey>& rows);
void MarkGapQueued(int height, const uint256& expected_hash, const std::string& source);
void MarkGapResolved(int height, const uint256& expected_hash, const std::string& source);
void MarkGapUnavailable(int height, const uint256& expected_hash, const std::string& source, const std::string& error);

} // namespace enterprise

#endif // BITCOIN_ENTERPRISE_PG_H
