#ifndef BITCOIN_ENTERPRISE_PG_H
#define BITCOIN_ENTERPRISE_PG_H

#include <enterprise/block_delta.h>
#include <enterprise/pqxx_compat.h>
#include <util/fs.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace enterprise {

class PgSession
{
private:
    std::unique_ptr<pqxx::connection> m_connection;
    std::map<std::string, std::string, std::less<>> m_prepared;

public:
    PgSession() = default;
    PgSession(const PgSession&) = delete;
    PgSession& operator=(const PgSession&) = delete;

    [[nodiscard]] pqxx::connection& Connection();
    void Prepare(std::string_view name, std::string_view definition);
    void Reset() noexcept;
};

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

/**
 * Validate the administrator-managed enterprise schema. This function never
 * creates or alters database objects and throws with a diagnostic when the
 * required migration has not been applied exactly.
 */
void ValidateEnterpriseSchema();

void MarkIngestStarted(const EnterpriseBlockDelta& delta, const fs::path& spool_path, const std::string& source);
void MarkIngestStarted(PgSession& session, pqxx::work& work, const EnterpriseBlockDelta& delta, const fs::path& spool_path, const std::string& source);
void MarkIngestSucceeded(const EnterpriseBlockDelta& delta, const fs::path& spool_path, const std::string& source);
void MarkIngestSucceeded(PgSession& session, pqxx::work& work, const EnterpriseBlockDelta& delta, const fs::path& spool_path, const std::string& source);
void MarkIngestFailed(const EnterpriseBlockDelta& delta, const fs::path& spool_path, const std::string& source, const std::string& error);
void ReconcileCoveredIngest(const EnterpriseBlockDelta& delta, const fs::path& spool_path, const std::string& source);
void ReconcileCoveredIngest(PgSession& session, pqxx::work& work, const EnterpriseBlockDelta& delta, const fs::path& spool_path, const std::string& source);

[[nodiscard]] std::vector<int> FindBlockTableGaps(int chain_tip_height, std::size_t limit);
[[nodiscard]] BlockTableCoverage GetBlockTableCoverage(int chain_tip_height);
[[nodiscard]] bool BlockRowCovered(int height, const uint256& expected_hash, int backfill_height);
[[nodiscard]] std::vector<BlockRowKey> FindCoveredBlockRows(const std::vector<BlockRowKey>& rows, int backfill_height);
[[nodiscard]] std::vector<BlockRowKey> FindCoveredBlockRows(pqxx::work& work, const std::vector<BlockRowKey>& rows, int backfill_height);
[[nodiscard]] std::vector<BlockRowKey> FindPriceFinalizationCandidates(
    PgSession& session,
    pqxx::work& work,
    int min_height,
    int max_height,
    std::size_t limit);
void MarkPriceFinalizationSucceeded(
    PgSession& session,
    pqxx::work& work,
    int height,
    const uint256& hash);
void MarkGapQueued(int height, const uint256& expected_hash, const std::string& source);
void MarkGapResolved(int height, const uint256& expected_hash, const std::string& source);
void MarkGapUnavailable(int height, const uint256& expected_hash, const std::string& source, const std::string& error);

} // namespace enterprise

#endif // BITCOIN_ENTERPRISE_PG_H
