#include <enterprise/pg.h>

#include <common/args.h>
#include <enterprise/pg_config.h>
#include <enterprise/network.h>
#include <pqxx/pqxx>
#include <tinyformat.h>
#include <util/check.h>
#include <util/fs.h>
#include <util/log.h>

#include <cstddef>
#include <exception>
#include <string>
#include <vector>

namespace {

std::string EventTypeString(EnterpriseSpoolEventType event_type)
{
    switch (event_type) {
    case EnterpriseSpoolEventType::CONNECT:
        return "connect";
    case EnterpriseSpoolEventType::DISCONNECT:
        return "disconnect";
    }
    Assume(false);
    return "unknown";
}

pqxx::connection Connect()
{
    return pqxx::connection{enterprise::PgConnectionString()};
}

void MarkIngest(const EnterpriseBlockDelta& delta, const fs::path& spool_path, const std::string& source, const std::string& status, const std::string& error)
{
    pqxx::connection c{Connect()};
    pqxx::work w{c};
    enterprise::EnsureEnterpriseTables(w);
    c.prepare("EnterpriseMarkIngest", R"sql(
        INSERT INTO enterprise_block_ingest
            (network, hash, height, event_type, status, source, spool_path, attempts, last_error, completed_at)
        VALUES
            ($1, $2, $3, $4, $5, $6, $7, 1, NULLIF($8, ''), CASE WHEN $5 = 'succeeded' THEN now() ELSE NULL END)
        ON CONFLICT (network, hash, event_type) DO UPDATE SET
            height = EXCLUDED.height,
            status = EXCLUDED.status,
            source = EXCLUDED.source,
            spool_path = EXCLUDED.spool_path,
            attempts = enterprise_block_ingest.attempts + CASE WHEN EXCLUDED.status = 'started' THEN 1 ELSE 0 END,
            last_error = EXCLUDED.last_error,
            updated_at = now(),
            completed_at = CASE WHEN EXCLUDED.status = 'succeeded' THEN now() ELSE enterprise_block_ingest.completed_at END
    )sql");
    w.exec_prepared(
        "EnterpriseMarkIngest",
        enterprise::Network(),
        delta.block_hash.GetHex(),
        delta.height,
        EventTypeString(delta.event_type),
        status,
        source,
        fs::PathToString(spool_path),
        error);
    w.commit();
}

} // namespace

namespace enterprise {

std::string Network()
{
    return EnterpriseChainToString(gArgs.GetChainType());
}

void EnsureEnterpriseTables(pqxx::work& w)
{
    w.exec(R"sql(
        CREATE UNIQUE INDEX IF NOT EXISTS blocks_network_height_idx ON blocks(network, height)
    )sql");
    w.exec(R"sql(
        ALTER TABLE blocks
            ADD COLUMN IF NOT EXISTS btc_usd_price DOUBLE PRECISION,
            ADD COLUMN IF NOT EXISTS btc_usd_price_low DOUBLE PRECISION,
            ADD COLUMN IF NOT EXISTS btc_usd_price_high DOUBLE PRECISION,
            ADD COLUMN IF NOT EXISTS btc_usd_price_source TEXT,
            ADD COLUMN IF NOT EXISTS denomination_eligible_outputs_count BIGINT,
            ADD COLUMN IF NOT EXISTS denomination_eligible_value_sats BIGINT,
            ADD COLUMN IF NOT EXISTS usd_denom_outputs_count BIGINT,
            ADD COLUMN IF NOT EXISTS usd_denom_value_sats BIGINT,
            ADD COLUMN IF NOT EXISTS usd_denom_confidence_sum DOUBLE PRECISION,
            ADD COLUMN IF NOT EXISTS sats_denom_outputs_count BIGINT,
            ADD COLUMN IF NOT EXISTS sats_denom_value_sats BIGINT,
            ADD COLUMN IF NOT EXISTS sats_denom_confidence_sum DOUBLE PRECISION,
            ADD COLUMN IF NOT EXISTS unknown_denom_outputs_count BIGINT,
            ADD COLUMN IF NOT EXISTS unknown_denom_value_sats BIGINT,
            ADD COLUMN IF NOT EXISTS ambiguous_denom_outputs_count BIGINT,
            ADD COLUMN IF NOT EXISTS ambiguous_denom_value_sats BIGINT,
            ADD COLUMN IF NOT EXISTS likely_change_outputs_count BIGINT,
            ADD COLUMN IF NOT EXISTS likely_change_value_sats BIGINT,
            ADD COLUMN IF NOT EXISTS denomination_classifier_version TEXT
    )sql");
    w.exec(R"sql(
        CREATE TABLE IF NOT EXISTS prices
        (
            day timestamp with time zone PRIMARY KEY,
            price DOUBLE PRECISION NOT NULL
        )
    )sql");
    w.exec(R"sql(
        DO $$
        BEGIN
            IF EXISTS (
                SELECT 1
                FROM pg_class
                WHERE oid = 'prices'::regclass
                  AND relkind IN ('r', 'p')
            ) THEN
                ALTER TABLE prices
                    ADD COLUMN IF NOT EXISTS price_low DOUBLE PRECISION,
                    ADD COLUMN IF NOT EXISTS price_high DOUBLE PRECISION,
                    ADD COLUMN IF NOT EXISTS price_source TEXT;
            END IF;
        END
        $$
    )sql");
    w.exec(R"sql(
        CREATE TABLE IF NOT EXISTS enterprise_block_ingest (
            network text NOT NULL,
            hash text NOT NULL,
            height bigint NOT NULL,
            event_type text NOT NULL,
            status text NOT NULL,
            source text NOT NULL,
            spool_path text,
            attempts bigint NOT NULL DEFAULT 0,
            last_error text,
            queued_at timestamp with time zone NOT NULL DEFAULT now(),
            updated_at timestamp with time zone NOT NULL DEFAULT now(),
            completed_at timestamp with time zone,
            PRIMARY KEY (network, hash, event_type)
        )
    )sql");
    w.exec(R"sql(
        CREATE TABLE IF NOT EXISTS enterprise_block_gaps (
            network text NOT NULL,
            height bigint NOT NULL,
            expected_hash text,
            status text NOT NULL,
            source text,
            last_error text,
            first_seen_at timestamp with time zone NOT NULL DEFAULT now(),
            updated_at timestamp with time zone NOT NULL DEFAULT now(),
            PRIMARY KEY (network, height)
        )
    )sql");
}

void MarkIngestStarted(const EnterpriseBlockDelta& delta, const fs::path& spool_path, const std::string& source)
{
    MarkIngest(delta, spool_path, source, "started", "");
}

void MarkIngestSucceeded(const EnterpriseBlockDelta& delta, const fs::path& spool_path, const std::string& source)
{
    MarkIngest(delta, spool_path, source, "succeeded", "");
}

void MarkIngestFailed(const EnterpriseBlockDelta& delta, const fs::path& spool_path, const std::string& source, const std::string& error)
{
    try {
        MarkIngest(delta, spool_path, source, "failed", error);
    } catch (const std::exception& e) {
        LogWarning("enterprise: failed to record ingest failure for %s: %s", delta.block_hash.ToString(), e.what());
    }
}

std::vector<int> FindBlockTableGaps(int chain_tip_height, std::size_t limit)
{
    std::vector<int> gaps;
    if (chain_tip_height < 0 || limit == 0) return gaps;

    pqxx::connection c{Connect()};
    pqxx::work w{c};
    EnsureEnterpriseTables(w);
    c.prepare("EnterpriseFindBlockGaps", R"sql(
        WITH expected(height) AS (
            SELECT generate_series(0, $1::bigint)
        ), missing AS (
            SELECT expected.height
            FROM expected
            LEFT JOIN enterprise_block_gaps gaps ON gaps.height = expected.height AND gaps.network = $2
            WHERE NOT EXISTS (
                SELECT 1
                FROM blocks
                WHERE network = $2
                  AND height = expected.height
            )
              AND COALESCE(gaps.status, '') NOT IN ('queued', 'unavailable')
            ORDER BY expected.height
            LIMIT $3
        )
        INSERT INTO enterprise_block_gaps (network, height, status)
        SELECT $2, missing.height, 'missing'
        FROM missing
        ON CONFLICT (network, height) DO UPDATE SET
            status = 'missing',
            updated_at = now()
        RETURNING height
    )sql");
    for (const auto& row : w.exec_prepared("EnterpriseFindBlockGaps", chain_tip_height, Network(), static_cast<int64_t>(limit))) {
        gaps.push_back(row[0].as<int>());
    }
    w.commit();
    return gaps;
}

BlockTableCoverage GetBlockTableCoverage(int chain_tip_height)
{
    BlockTableCoverage coverage;
    coverage.chain_tip_height = chain_tip_height;
    if (chain_tip_height < 0) return coverage;

    pqxx::connection c{Connect()};
    pqxx::work w{c};
    EnsureEnterpriseTables(w);
    c.prepare("EnterpriseBlockTableCoverage", R"sql(
        WITH block_counts AS (
            SELECT count(*) AS populated_heights
            FROM blocks
            WHERE network = $2
              AND height BETWEEN 0 AND $1::bigint
        ), unavailable AS (
            SELECT count(*) AS unavailable_heights
            FROM enterprise_block_gaps
            WHERE network = $2
              AND height BETWEEN 0 AND $1::bigint
              AND status = 'unavailable'
        )
        SELECT
            $1::bigint + 1 AS expected_blocks,
            block_counts.populated_heights,
            ($1::bigint + 1) - block_counts.populated_heights AS missing_heights,
            0 AS duplicate_rows,
            unavailable.unavailable_heights
        FROM block_counts
        CROSS JOIN unavailable
    )sql");
    const auto result{w.exec_prepared("EnterpriseBlockTableCoverage", chain_tip_height, Network())};
    if (!result.empty()) {
        const auto row{result.front()};
        coverage.expected_blocks = row[0].as<int64_t>();
        coverage.populated_heights = row[1].as<int64_t>();
        coverage.missing_heights = row[2].as<int64_t>();
        coverage.duplicate_rows = row[3].as<int64_t>();
        coverage.unavailable_heights = row[4].as<int64_t>();
    }
    w.commit();
    return coverage;
}

bool BlockRowMatches(int height, const uint256& expected_hash)
{
    pqxx::connection c{Connect()};
    pqxx::work w{c};
    c.prepare("EnterpriseBlockRowMatches", R"sql(
        SELECT EXISTS (
            SELECT 1
            FROM blocks
            WHERE network = $1
              AND height = $2
              AND hash = $3
        )
    )sql");
    const auto result{w.exec_prepared("EnterpriseBlockRowMatches", Network(), height, expected_hash.GetHex())};
    const bool matches{!result.empty() && result.front()[0].as<bool>()};
    w.commit();
    return matches;
}

std::vector<BlockRowKey> FindMatchingBlockRows(const std::vector<BlockRowKey>& rows)
{
    std::vector<BlockRowKey> matches;
    if (rows.empty()) return matches;

    pqxx::connection c{Connect()};
    pqxx::work w{c};

    std::string sql{"WITH requested(height, hash) AS (VALUES "};
    bool first{true};
    for (const BlockRowKey& row : rows) {
        if (!first) sql += ",";
        first = false;
        sql += "(";
        sql += std::to_string(row.height);
        sql += ",";
        sql += w.quote(row.hash.GetHex());
        sql += ")";
    }
    sql += R"sql(
        )
        SELECT requested.height, requested.hash
        FROM requested
        JOIN blocks ON blocks.network = )sql";
    sql += w.quote(Network());
    sql += R"sql(
            AND blocks.height = requested.height
            AND blocks.hash = requested.hash
    )sql";

    for (const auto& result_row : w.exec(sql)) {
        const auto hash{uint256::FromHex(result_row[1].as<std::string>())};
        if (!hash) continue;
        matches.push_back(BlockRowKey{result_row[0].as<int>(), *hash});
    }
    w.commit();
    return matches;
}

void MarkGapQueued(int height, const uint256& expected_hash, const std::string& source)
{
    pqxx::connection c{Connect()};
    pqxx::work w{c};
    EnsureEnterpriseTables(w);
    c.prepare("EnterpriseMarkGapQueued", R"sql(
        INSERT INTO enterprise_block_gaps (network, height, expected_hash, status, source, last_error)
        VALUES ($1, $2, $3, 'queued', $4, NULL)
        ON CONFLICT (network, height) DO UPDATE SET
            expected_hash = EXCLUDED.expected_hash,
            status = 'queued',
            source = EXCLUDED.source,
            last_error = NULL,
            updated_at = now()
    )sql");
    w.exec_prepared("EnterpriseMarkGapQueued", Network(), height, expected_hash.GetHex(), source);
    w.commit();
}

void MarkGapResolved(int height, const uint256& expected_hash, const std::string& source)
{
    try {
        pqxx::connection c{Connect()};
        pqxx::work w{c};
        EnsureEnterpriseTables(w);
        c.prepare("EnterpriseMarkGapResolved", R"sql(
            UPDATE enterprise_block_gaps SET
                expected_hash = $3,
                status = 'resolved',
                source = $4,
                last_error = NULL,
                updated_at = now()
            WHERE network = $1
              AND height = $2
              AND (expected_hash IS NULL OR expected_hash = $3)
        )sql");
        w.exec_prepared("EnterpriseMarkGapResolved", Network(), height, expected_hash.GetHex(), source);
        w.commit();
    } catch (const std::exception& e) {
        LogWarning("enterprise: failed to record resolved gap at height %d: %s", height, e.what());
    }
}

void MarkGapUnavailable(int height, const uint256& expected_hash, const std::string& source, const std::string& error)
{
    try {
        pqxx::connection c{Connect()};
        pqxx::work w{c};
        EnsureEnterpriseTables(w);
        c.prepare("EnterpriseMarkGapUnavailable", R"sql(
            INSERT INTO enterprise_block_gaps (network, height, expected_hash, status, source, last_error)
            VALUES ($1, $2, $3, 'unavailable', $4, $5)
            ON CONFLICT (network, height) DO UPDATE SET
                expected_hash = EXCLUDED.expected_hash,
                status = 'unavailable',
                source = EXCLUDED.source,
                last_error = EXCLUDED.last_error,
                updated_at = now()
        )sql");
        w.exec_prepared("EnterpriseMarkGapUnavailable", Network(), height, expected_hash.GetHex(), source, error);
        w.commit();
    } catch (const std::exception& e) {
        LogWarning("enterprise: failed to record unavailable gap at height %d: %s", height, e.what());
    }
}

} // namespace enterprise
