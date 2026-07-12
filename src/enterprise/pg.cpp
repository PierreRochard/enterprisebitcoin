#include <enterprise/pg.h>

#include <common/args.h>
#include <enterprise/denomination_classifier.h>
#include <enterprise/network.h>
#include <enterprise/pg_config.h>
#include <enterprise/pqxx_compat.h>
#include <tinyformat.h>
#include <util/check.h>
#include <util/fs.h>
#include <util/log.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <exception>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace {

constexpr const char* REQUIRED_MIGRATION{"20260711_pg17_reuse_v1"};

enum class ColumnDefault {
    NONE,
    ZERO,
    NOW,
    SESSION_USER,
};

struct ColumnSpec {
    const char* name;
    const char* type;
    bool not_null;
    ColumnDefault default_value{ColumnDefault::NONE};
};

struct InsertColumnSpec {
    const char* name;
    const char* type;
    bool text_compatible{false};
};

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

std::string NormalizeSqlExpression(std::string expression)
{
    expression.erase(std::remove_if(expression.begin(), expression.end(), [](unsigned char ch) { return std::isspace(ch); }), expression.end());
    std::transform(expression.begin(), expression.end(), expression.begin(), [](unsigned char ch) { return std::tolower(ch); });
    return expression;
}

bool DefaultMatches(ColumnDefault expected, const std::string& expression)
{
    const std::string normalized{NormalizeSqlExpression(expression)};
    switch (expected) {
    case ColumnDefault::NONE:
        return normalized.empty();
    case ColumnDefault::ZERO:
        return normalized == "0";
    case ColumnDefault::NOW:
        return normalized == "now()";
    case ColumnDefault::SESSION_USER:
        return normalized == "session_user";
    }
    Assume(false);
}

void RequireRelationKind(pqxx::work& w, const std::string& relation, std::string_view allowed_kinds)
{
    const std::string qualified_relation{"public." + relation};
    const auto result{w.exec(
        "SELECT relkind::text FROM pg_class WHERE oid = to_regclass(" + w.quote(qualified_relation) + ")")};
    if (result.size() != 1) {
        throw std::runtime_error("required relation public." + relation + " is missing");
    }
    const std::string kind{result.front()[0].as<std::string>()};
    if (kind.size() != 1 || allowed_kinds.find(kind.front()) == std::string_view::npos) {
        throw std::runtime_error(strprintf("required relation public.%s has incompatible relkind '%s'", relation, kind));
    }
}

void RequireColumns(pqxx::work& w, const std::string& relation, const std::vector<ColumnSpec>& expected)
{
    const std::string qualified_relation{"public." + relation};
    const auto result{w.exec(R"sql(
        SELECT attribute.attname,
               format_type(attribute.atttypid, attribute.atttypmod),
               attribute.attnotnull,
               COALESCE(pg_get_expr(default_value.adbin, default_value.adrelid), ''),
               attribute.attgenerated::text,
               attribute.attidentity::text
        FROM pg_attribute attribute
        LEFT JOIN pg_attrdef default_value
          ON default_value.adrelid = attribute.attrelid
         AND default_value.adnum = attribute.attnum
        WHERE attribute.attrelid = to_regclass()sql" + w.quote(qualified_relation) + R"sql()
          AND attribute.attnum > 0
          AND NOT attribute.attisdropped
    )sql")};

    std::map<std::string, std::tuple<std::string, bool, std::string, std::string, std::string>> actual;
    for (const auto& row : result) {
        actual.emplace(
            row[0].as<std::string>(),
            std::make_tuple(
                row[1].as<std::string>(),
                row[2].as<bool>(),
                row[3].as<std::string>(),
                row[4].as<std::string>(),
                row[5].as<std::string>()));
    }

    for (const ColumnSpec& column : expected) {
        const auto it{actual.find(column.name)};
        if (it == actual.end()) {
            throw std::runtime_error(strprintf("required column public.%s.%s is missing", relation, column.name));
        }
        const auto& [type, not_null, default_value, generated, identity]{it->second};
        if (type != column.type ||
            not_null != column.not_null ||
            !DefaultMatches(column.default_value, default_value) ||
            !generated.empty() ||
            !identity.empty()) {
            throw std::runtime_error(strprintf(
                "required column public.%s.%s is incompatible (type=%s, not_null=%s, default=%s, generated=%s, identity=%s)",
                relation,
                column.name,
                type,
                not_null ? "true" : "false",
                default_value.empty() ? "<none>" : default_value,
                generated.empty() ? "false" : generated,
                identity.empty() ? "false" : identity));
        }
    }
}

void RequireInsertColumns(pqxx::work& w, const std::string& relation, const std::vector<InsertColumnSpec>& expected)
{
    const std::string qualified_relation{"public." + relation};
    const auto result{w.exec(R"sql(
        SELECT attribute.attname,
               format_type(attribute.atttypid, attribute.atttypmod),
               attribute.attgenerated::text,
               attribute.attidentity::text
        FROM pg_attribute attribute
        WHERE attribute.attrelid = to_regclass()sql" + w.quote(qualified_relation) + R"sql()
          AND attribute.attnum > 0
          AND NOT attribute.attisdropped
    )sql")};
    std::map<std::string, std::tuple<std::string, std::string, std::string>> actual;
    for (const auto& row : result) {
        actual.emplace(
            row[0].as<std::string>(),
            std::make_tuple(row[1].as<std::string>(), row[2].as<std::string>(), row[3].as<std::string>()));
    }
    for (const InsertColumnSpec& column : expected) {
        const auto it{actual.find(column.name)};
        if (it == actual.end()) {
            throw std::runtime_error(strprintf("required insert column public.%s.%s is missing", relation, column.name));
        }
        const auto& [type, generated, identity]{it->second};
        const bool compatible_text{
            column.text_compatible &&
            (type == "text" || type == "character varying")};
        if ((type != column.type && !compatible_text) || !generated.empty() || !identity.empty()) {
            throw std::runtime_error(strprintf(
                "required insert column public.%s.%s is incompatible (type=%s, generated=%s, identity=%s)",
                relation,
                column.name,
                type,
                generated.empty() ? "false" : generated,
                identity.empty() ? "false" : identity));
        }
    }
}

void RejectBlockingExtraColumns(
    pqxx::work& w,
    const std::string& relation,
    const std::vector<std::string_view>& managed_columns)
{
    const std::string qualified_relation{"public." + relation};
    std::string managed_sql;
    for (const std::string_view column : managed_columns) {
        if (!managed_sql.empty()) managed_sql += ",";
        managed_sql += w.quote(std::string{column});
    }
    const auto result{w.exec(R"sql(
        SELECT attribute.attname
        FROM pg_attribute attribute
        LEFT JOIN pg_attrdef default_value
          ON default_value.adrelid = attribute.attrelid
         AND default_value.adnum = attribute.attnum
        WHERE attribute.attrelid = to_regclass()sql" + w.quote(qualified_relation) + R"sql()
          AND attribute.attnum > 0
          AND NOT attribute.attisdropped
          AND attribute.attnotnull
          AND default_value.oid IS NULL
          AND attribute.attgenerated = ''
          AND attribute.attidentity = ''
          AND attribute.attname NOT IN ()sql" + managed_sql + R"sql()
        ORDER BY attribute.attnum
    )sql")};
    if (!result.empty()) {
        throw std::runtime_error(strprintf(
            "public.%s has unmanaged NOT NULL column %s without a default; enterprise inserts would fail",
            relation,
            result.front()[0].as<std::string>()));
    }
}

std::vector<std::string_view> ColumnNames(const std::vector<InsertColumnSpec>& columns)
{
    std::vector<std::string_view> names;
    names.reserve(columns.size());
    for (const InsertColumnSpec& column : columns) names.emplace_back(column.name);
    return names;
}

void RequirePrimaryKey(pqxx::work& w, const std::string& relation, const std::string& expected_columns)
{
    const std::string qualified_relation{"public." + relation};
    const auto result{w.exec(R"sql(
        SELECT string_agg(attribute.attname, ',' ORDER BY key_column.ordinality)
        FROM pg_constraint constraint_row
        CROSS JOIN LATERAL unnest(constraint_row.conkey) WITH ORDINALITY key_column(attnum, ordinality)
        JOIN pg_attribute attribute
          ON attribute.attrelid = constraint_row.conrelid
         AND attribute.attnum = key_column.attnum
        WHERE constraint_row.conrelid = to_regclass()sql" + w.quote(qualified_relation) + R"sql()
          AND constraint_row.contype = 'p'
        GROUP BY constraint_row.oid
    )sql")};
    if (result.size() != 1 || result.front()[0].as<std::string>() != expected_columns) {
        throw std::runtime_error(strprintf("public.%s must have primary key (%s)", relation, expected_columns));
    }
}

void RequireIndex(
    pqxx::work& w,
    const std::string& index,
    const std::string& relation,
    const std::string& expected_columns,
    bool unique)
{
    const std::string qualified_index{"public." + index};
    const std::string qualified_relation{"public." + relation};
    const auto result{w.exec(R"sql(
        SELECT index_meta.indisunique,
               index_meta.indisvalid,
               index_meta.indisready,
               index_meta.indpred IS NULL,
               access_method.amname,
               (
                   SELECT string_agg(attribute.attname, ',' ORDER BY key_column.ordinality)
                   FROM unnest(index_meta.indkey) WITH ORDINALITY key_column(attnum, ordinality)
                   JOIN pg_attribute attribute
                     ON attribute.attrelid = index_meta.indrelid
                    AND attribute.attnum = key_column.attnum
               )
        FROM pg_index index_meta
        JOIN pg_class index_relation ON index_relation.oid = index_meta.indexrelid
        JOIN pg_am access_method ON access_method.oid = index_relation.relam
        WHERE index_meta.indexrelid = to_regclass()sql" + w.quote(qualified_index) + R"sql()
          AND index_meta.indrelid = to_regclass()sql" + w.quote(qualified_relation) + R"sql()
          AND index_meta.indexprs IS NULL
    )sql")};
    if (result.size() != 1 ||
        result.front()[0].as<bool>() != unique ||
        !result.front()[1].as<bool>() ||
        !result.front()[2].as<bool>() ||
        !result.front()[3].as<bool>() ||
        result.front()[4].as<std::string>() != "btree" ||
        result.front()[5].as<std::string>() != expected_columns) {
        throw std::runtime_error(strprintf(
            "required index public.%s on public.%s (%s) is missing, invalid, or incompatible",
            index,
            relation,
            expected_columns));
    }
}

void RequireUniqueIndexOnColumns(pqxx::work& w, const std::string& relation, const std::string& expected_columns)
{
    const std::string qualified_relation{"public." + relation};
    const auto result{w.exec(R"sql(
        SELECT EXISTS (
            SELECT 1
            FROM pg_index index_meta
            JOIN pg_class index_relation ON index_relation.oid = index_meta.indexrelid
            JOIN pg_am access_method ON access_method.oid = index_relation.relam
            WHERE index_meta.indrelid = to_regclass()sql" + w.quote(qualified_relation) + R"sql()
              AND index_meta.indisunique
              AND index_meta.indisvalid
              AND index_meta.indisready
              AND index_meta.indpred IS NULL
              AND index_meta.indexprs IS NULL
              AND access_method.amname = 'btree'
              AND (
                  SELECT string_agg(attribute.attname, ',' ORDER BY key_column.ordinality)
                  FROM unnest(index_meta.indkey) WITH ORDINALITY key_column(attnum, ordinality)
                  JOIN pg_attribute attribute
                    ON attribute.attrelid = index_meta.indrelid
                   AND attribute.attnum = key_column.attnum
              ) = )sql" + w.quote(expected_columns) + R"sql(
        )
    )sql")};
    if (!result.front()[0].as<bool>()) {
        throw std::runtime_error(strprintf(
            "public.%s requires a valid unique btree index on (%s)", relation, expected_columns));
    }
}

void RequireCompatiblePrices(pqxx::work& w)
{
    RequireRelationKind(w, "prices", "rm");
    const auto result{w.exec(R"sql(
        SELECT
            (SELECT format_type(atttypid, atttypmod) FROM pg_attribute
             WHERE attrelid = 'public.prices'::regclass AND attname = 'day' AND attnum > 0 AND NOT attisdropped),
            (SELECT format_type(atttypid, atttypmod) FROM pg_attribute
             WHERE attrelid = 'public.prices'::regclass AND attname = 'price' AND attnum > 0 AND NOT attisdropped),
            EXISTS (SELECT 1 FROM public.prices WHERE price IS NULL)
    )sql")};
    const std::string day_type{result.front()[0].is_null() ? "" : result.front()[0].as<std::string>()};
    const std::string price_type{result.front()[1].is_null() ? "" : result.front()[1].as<std::string>()};
    static constexpr std::array<std::string_view, 3> DAY_TYPES{
        "date", "timestamp with time zone", "timestamp without time zone"};
    static constexpr std::array<std::string_view, 6> PRICE_TYPES{
        "smallint", "integer", "bigint", "numeric", "double precision", "real"};
    if (std::find(DAY_TYPES.begin(), DAY_TYPES.end(), day_type) == DAY_TYPES.end() ||
        std::find(PRICE_TYPES.begin(), PRICE_TYPES.end(), price_type) == PRICE_TYPES.end() ||
        result.front()[2].as<bool>()) {
        throw std::runtime_error(strprintf(
            "public.prices requires a date/timestamp day column, numeric price column, and no NULL prices (found day=%s, price=%s)",
            day_type,
            price_type));
    }
    RequireUniqueIndexOnColumns(w, "prices", "day");
}

void MarkIngest(
    enterprise::PgSession& session,
    pqxx::work& w,
    const EnterpriseBlockDelta& delta,
    const fs::path& spool_path,
    const std::string& source,
    const std::string& status,
    const std::string& error)
{
    session.Prepare("EnterpriseMarkIngest", R"sql(
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
    enterprise::ExecPrepared(
        w,
        "EnterpriseMarkIngest",
        enterprise::Network(),
        delta.block_hash.GetHex(),
        delta.height,
        EventTypeString(delta.event_type),
        status,
        source,
        fs::PathToString(spool_path),
        error);
}

void MarkIngest(const EnterpriseBlockDelta& delta, const fs::path& spool_path, const std::string& source, const std::string& status, const std::string& error)
{
    enterprise::PgSession session;
    pqxx::work w{session.Connection()};
    MarkIngest(session, w, delta, spool_path, source, status, error);
    w.commit();
}

void ResolveGap(
    enterprise::PgSession& session,
    pqxx::work& w,
    int height,
    const uint256& expected_hash,
    const std::string& source)
{
    session.Prepare("EnterpriseVerifyGapResolution", R"sql(
        SELECT
            EXISTS (
                SELECT 1 FROM blocks
                WHERE network = $1 AND height = $2 AND hash = $3
            ),
            EXISTS (
                SELECT 1 FROM enterprise_block_gaps
                WHERE network = $1 AND height = $2
            )
    )sql");
    const auto state{enterprise::ExecPrepared(
        w,
        "EnterpriseVerifyGapResolution",
        enterprise::Network(),
        height,
        expected_hash.GetHex())};
    if (state.empty() || !state.front()[0].as<bool>()) {
        throw std::runtime_error(strprintf(
            "cannot resolve enterprise block gap at height %d: matching block row is absent",
            height));
    }
    if (!state.front()[1].as<bool>()) return;

    session.Prepare("EnterpriseResolveGap", R"sql(
        UPDATE enterprise_block_gaps SET
            expected_hash = $3,
            status = 'resolved',
            source = $4,
            last_error = NULL,
            updated_at = now()
        WHERE network = $1 AND height = $2
        RETURNING 1
    )sql");
    const auto updated{enterprise::ExecPrepared(
        w,
        "EnterpriseResolveGap",
        enterprise::Network(),
        height,
        expected_hash.GetHex(),
        source)};
    if (updated.size() != 1) {
        throw std::runtime_error(strprintf("failed to resolve enterprise block gap at height %d", height));
    }
}

} // namespace

namespace enterprise {

pqxx::connection& PgSession::Connection()
{
    if (!m_connection) {
        m_connection = std::make_unique<pqxx::connection>(PgConnectionString());
        m_prepared.clear();
    }
    return *m_connection;
}

void PgSession::Prepare(std::string_view name, std::string_view definition)
{
    const auto existing{m_prepared.find(name)};
    if (existing != m_prepared.end()) {
        if (existing->second != definition) {
            throw std::logic_error("enterprise PostgreSQL prepared statement redefined: " + std::string{name});
        }
        return;
    }

    const std::string statement_name{name};
    const std::string statement_definition{definition};
    Connection().prepare(statement_name.c_str(), statement_definition.c_str());
    m_prepared.emplace(statement_name, statement_definition);
}

void PgSession::Reset() noexcept
{
    m_prepared.clear();
    m_connection.reset();
}

std::string Network()
{
    return EnterpriseChainToString(gArgs.GetChainType());
}

void ValidateEnterpriseSchema()
{
    pqxx::connection c{Connect()};
    pqxx::work w{c};

    const auto settings{w.exec(R"sql(
        SELECT current_setting('server_version_num')::integer,
               current_setting('application_name'),
               current_setting('synchronous_commit'),
               current_setting('search_path'),
               current_setting('lock_timeout'),
               current_setting('statement_timeout'),
               current_setting('idle_in_transaction_session_timeout')
    )sql")};
    const auto settings_row{settings.front()};
    const int server_version{settings_row[0].as<int>()};
    if (server_version < 170000 || server_version >= 180000) {
        throw std::runtime_error(strprintf("enterprise SQL requires PostgreSQL 17.x (server_version_num=%d)", server_version));
    }
    if (settings_row[1].as<std::string>() != "enterprise-bitcoind" ||
        settings_row[2].as<std::string>() != "on" ||
        NormalizeSqlExpression(settings_row[3].as<std::string>()) != "pg_catalog,public" ||
        settings_row[4].as<std::string>() == "0" ||
        settings_row[5].as<std::string>() == "0" ||
        settings_row[6].as<std::string>() == "0") {
        throw std::runtime_error("enterprise PostgreSQL connection safety settings were not applied");
    }

    RequireRelationKind(w, "enterprise_schema_migrations", "r");
    RequireColumns(w, "enterprise_schema_migrations", {
        {"version", "text", true},
        {"description", "text", true},
        {"applied_at", "timestamp with time zone", true, ColumnDefault::NOW},
        {"applied_by", "text", true, ColumnDefault::SESSION_USER},
    });
    RequirePrimaryKey(w, "enterprise_schema_migrations", "version");
    const auto migration{w.exec(
        "SELECT EXISTS (SELECT 1 FROM public.enterprise_schema_migrations WHERE version = " +
        w.quote(std::string{REQUIRED_MIGRATION}) +
        " AND description = " +
        w.quote("Additive PostgreSQL 17 reuse schema for enterprise denomination backfill") +
        ")")};
    if (!migration.front()[0].as<bool>()) {
        throw std::runtime_error(strprintf(
            "required enterprise migration %s is not recorded; apply and verify the administrator migration before starting the node",
            REQUIRED_MIGRATION));
    }

    RequireRelationKind(w, "blocks", "r");
    const std::vector<InsertColumnSpec> block_insert_columns{
        {"hash", "text", true},
        {"merkle_root", "text", true},
        {"time", "timestamp with time zone"},
        {"median_time", "timestamp with time zone"},
        {"height", "bigint"},
        {"subsidy", "bigint"},
        {"transactions_count", "bigint"},
        {"version", "bigint"},
        {"status", "bigint"},
        {"bits", "bigint"},
        {"nonce", "bigint"},
        {"difficulty", "double precision"},
        {"chain_work", "text", true},
        {"outputs_count", "bigint"},
        {"inputs_count", "bigint"},
        {"total_output_value", "bigint"},
        {"total_input_value", "bigint"},
        {"total_fees", "bigint"},
        {"total_size", "bigint"},
        {"total_vsize", "bigint"},
        {"total_weight", "bigint"},
        {"fee_rates", "jsonb"},
        {"output_data", "jsonb"},
        {"input_data", "jsonb"},
        {"transaction_data", "jsonb"},
        {"output_script_types", "jsonb"},
        {"input_script_types", "jsonb"},
        {"output_legacy_signature_operations", "bigint"},
        {"input_legacy_signature_operations", "bigint"},
        {"input_p2sh_signature_operations", "bigint"},
        {"input_witness_signature_operations", "bigint"},
        {"outputs_total_size", "bigint"},
        {"inputs_total_size", "bigint"},
        {"net_utxo_size_impact", "bigint"},
        {"hash_prev_block", "text", true},
        {"network", "text", true},
        {"nonstandard_create_count", "bigint"},
        {"pubkey_create_count", "bigint"},
        {"pubkeyhash_create_count", "bigint"},
        {"scripthash_create_count", "bigint"},
        {"multisig_create_count", "bigint"},
        {"null_data_create_count", "bigint"},
        {"witness_v0_keyhash_create_count", "bigint"},
        {"witness_v0_scripthash_create_count", "bigint"},
        {"witness_v1_taproot_create_count", "bigint"},
        {"witness_unknown_create_count", "bigint"},
        {"nonstandard_spend_count", "bigint"},
        {"pubkey_spend_count", "bigint"},
        {"pubkeyhash_spend_count", "bigint"},
        {"scripthash_spend_count", "bigint"},
        {"multisig_spend_count", "bigint"},
        {"null_data_spend_count", "bigint"},
        {"witness_v0_keyhash_spend_count", "bigint"},
        {"witness_v0_scripthash_spend_count", "bigint"},
        {"witness_v1_taproot_spend_count", "bigint"},
        {"witness_unknown_spend_count", "bigint"},
        {"coinbase", "bigint"},
        {"ordinals_weight", "bigint"},
        {"ordinals_count", "bigint"},
        {"ordinals_size", "bigint"},
        {"ordinals_vsize", "bigint"},
        {"ordinals_fees", "bigint"},
        {"non_ordinals_weight", "bigint"},
        {"non_ordinals_count", "bigint"},
        {"non_ordinals_size", "bigint"},
        {"non_ordinals_vsize", "bigint"},
        {"non_ordinals_fees", "bigint"},
        {"btc_usd_price", "double precision"},
        {"btc_usd_price_low", "double precision"},
        {"btc_usd_price_high", "double precision"},
        {"btc_usd_price_source", "text"},
        {"denomination_eligible_outputs_count", "bigint"},
        {"denomination_eligible_value_sats", "bigint"},
        {"usd_denom_outputs_count", "bigint"},
        {"usd_denom_value_sats", "bigint"},
        {"usd_denom_confidence_sum", "double precision"},
        {"sats_denom_outputs_count", "bigint"},
        {"sats_denom_value_sats", "bigint"},
        {"sats_denom_confidence_sum", "double precision"},
        {"unknown_denom_outputs_count", "bigint"},
        {"unknown_denom_value_sats", "bigint"},
        {"ambiguous_denom_outputs_count", "bigint"},
        {"ambiguous_denom_value_sats", "bigint"},
        {"likely_change_outputs_count", "bigint"},
        {"likely_change_value_sats", "bigint"},
        {"denomination_classifier_version", "text"},
    };
    RequireInsertColumns(w, "blocks", block_insert_columns);
    RejectBlockingExtraColumns(w, "blocks", ColumnNames(block_insert_columns));
    RequirePrimaryKey(w, "blocks", "hash");
    RequireColumns(w, "blocks", {
        {"height", "bigint", false},
        {"btc_usd_price", "double precision", false},
        {"btc_usd_price_low", "double precision", false},
        {"btc_usd_price_high", "double precision", false},
        {"btc_usd_price_source", "text", false},
        {"denomination_eligible_outputs_count", "bigint", false},
        {"denomination_eligible_value_sats", "bigint", false},
        {"usd_denom_outputs_count", "bigint", false},
        {"usd_denom_value_sats", "bigint", false},
        {"usd_denom_confidence_sum", "double precision", false},
        {"sats_denom_outputs_count", "bigint", false},
        {"sats_denom_value_sats", "bigint", false},
        {"sats_denom_confidence_sum", "double precision", false},
        {"unknown_denom_outputs_count", "bigint", false},
        {"unknown_denom_value_sats", "bigint", false},
        {"ambiguous_denom_outputs_count", "bigint", false},
        {"ambiguous_denom_value_sats", "bigint", false},
        {"likely_change_outputs_count", "bigint", false},
        {"likely_change_value_sats", "bigint", false},
        {"denomination_classifier_version", "text", false},
    });
    RequireIndex(w, "blocks_network_height_idx", "blocks", "network,height", true);

    RequireRelationKind(w, "enterprise_block_ingest", "r");
    RequireColumns(w, "enterprise_block_ingest", {
        {"network", "text", true},
        {"hash", "text", true},
        {"height", "bigint", true},
        {"event_type", "text", true},
        {"status", "text", true},
        {"source", "text", true},
        {"spool_path", "text", false},
        {"attempts", "bigint", true, ColumnDefault::ZERO},
        {"last_error", "text", false},
        {"queued_at", "timestamp with time zone", true, ColumnDefault::NOW},
        {"updated_at", "timestamp with time zone", true, ColumnDefault::NOW},
        {"completed_at", "timestamp with time zone", false},
    });
    RejectBlockingExtraColumns(w, "enterprise_block_ingest", {
        "network", "hash", "height", "event_type", "status", "source", "spool_path",
        "attempts", "last_error", "queued_at", "updated_at", "completed_at"});
    RequirePrimaryKey(w, "enterprise_block_ingest", "network,hash,event_type");
    RequireIndex(
        w,
        "enterprise_block_ingest_monitor_idx",
        "enterprise_block_ingest",
        "network,source,status,height",
        false);

    RequireRelationKind(w, "enterprise_block_gaps", "r");
    RequireColumns(w, "enterprise_block_gaps", {
        {"network", "text", true},
        {"height", "bigint", true},
        {"expected_hash", "text", false},
        {"status", "text", true},
        {"source", "text", false},
        {"last_error", "text", false},
        {"first_seen_at", "timestamp with time zone", true, ColumnDefault::NOW},
        {"updated_at", "timestamp with time zone", true, ColumnDefault::NOW},
    });
    RejectBlockingExtraColumns(w, "enterprise_block_gaps", {
        "network", "height", "expected_hash", "status", "source", "last_error",
        "first_seen_at", "updated_at"});
    RequirePrimaryKey(w, "enterprise_block_gaps", "network,height");

    RequireRelationKind(w, "mempool_entries", "r");
    RequireColumns(w, "mempool_entries", {
        {"txid", "text", true},
        {"network", "text", false},
        {"wtxid", "text", false},
        {"fee", "bigint", false},
        {"weight", "bigint", false},
        {"memory_usage", "bigint", false},
        {"entry_time", "timestamp with time zone", false},
        {"entry_height", "bigint", false},
        {"spends_coinbase", "boolean", false},
        {"sigop_cost", "bigint", false},
        {"height_lockpoint", "bigint", false},
        {"time_lockpoint", "timestamp with time zone", false},
        {"descendants_count", "bigint", false},
        {"descendants_size", "bigint", false},
        {"descendants_fees", "bigint", false},
        {"ancestors_count", "bigint", false},
        {"ancestors_size", "bigint", false},
        {"ancestors_fees", "bigint", false},
        {"ancestors_sigop_cost", "bigint", false},
        {"removal_reason", "text", false},
        {"removal_time", "timestamp with time zone", false},
    });
    RejectBlockingExtraColumns(w, "mempool_entries", {
        "txid", "network", "wtxid", "fee", "weight", "memory_usage", "entry_time",
        "entry_height", "spends_coinbase", "sigop_cost", "height_lockpoint", "time_lockpoint",
        "descendants_count", "descendants_size", "descendants_fees", "ancestors_count",
        "ancestors_size", "ancestors_fees", "ancestors_sigop_cost", "removal_reason", "removal_time"});
    RequirePrimaryKey(w, "mempool_entries", "txid");
    RequireCompatiblePrices(w);

    w.commit();
}

void MarkIngestStarted(const EnterpriseBlockDelta& delta, const fs::path& spool_path, const std::string& source)
{
    MarkIngest(delta, spool_path, source, "started", "");
}

void MarkIngestStarted(PgSession& session, pqxx::work& work, const EnterpriseBlockDelta& delta, const fs::path& spool_path, const std::string& source)
{
    MarkIngest(session, work, delta, spool_path, source, "started", "");
}

void MarkIngestSucceeded(const EnterpriseBlockDelta& delta, const fs::path& spool_path, const std::string& source)
{
    PgSession session;
    pqxx::work w{session.Connection()};
    MarkIngestSucceeded(session, w, delta, spool_path, source);
    w.commit();
}

void MarkIngestSucceeded(PgSession& session, pqxx::work& work, const EnterpriseBlockDelta& delta, const fs::path& spool_path, const std::string& source)
{
    if (delta.event_type == EnterpriseSpoolEventType::CONNECT) {
        ResolveGap(session, work, delta.height, delta.block_hash, source);
    }
    MarkIngest(session, work, delta, spool_path, source, "succeeded", "");
}

void MarkIngestFailed(const EnterpriseBlockDelta& delta, const fs::path& spool_path, const std::string& source, const std::string& error)
{
    try {
        MarkIngest(delta, spool_path, source, "failed", error);
    } catch (const std::exception& e) {
        LogWarning("enterprise: failed to record ingest failure for %s: %s", delta.block_hash.ToString(), e.what());
    }
}

void ReconcileCoveredIngest(const EnterpriseBlockDelta& delta, const fs::path& spool_path, const std::string& source)
{
    PgSession session;
    pqxx::work w{session.Connection()};
    ReconcileCoveredIngest(session, w, delta, spool_path, source);
    w.commit();
}

void ReconcileCoveredIngest(PgSession& session, pqxx::work& work, const EnterpriseBlockDelta& delta, const fs::path& spool_path, const std::string& source)
{
    ResolveGap(session, work, delta.height, delta.block_hash, source);
    MarkIngest(session, work, delta, spool_path, source, "succeeded", "");
}

std::vector<int> FindBlockTableGaps(int chain_tip_height, std::size_t limit)
{
    std::vector<int> gaps;
    if (chain_tip_height < 0 || limit == 0) return gaps;

    pqxx::connection c{Connect()};
    pqxx::work w{c};
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
    for (const auto& row : enterprise::ExecPrepared(
             w,
             "EnterpriseFindBlockGaps",
             chain_tip_height,
             Network(),
             static_cast<int64_t>(limit))) {
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
    const auto result{enterprise::ExecPrepared(w, "EnterpriseBlockTableCoverage", chain_tip_height, Network())};
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

bool BlockRowCovered(int height, const uint256& expected_hash, int backfill_height)
{
    pqxx::connection c{Connect()};
    pqxx::work w{c};
    c.prepare("EnterpriseBlockRowCovered", R"sql(
        SELECT EXISTS (
            SELECT 1
            FROM blocks
            WHERE network = $1
              AND height = $2
              AND hash = $3
              AND ($4 < 0 OR height > $4 OR denomination_classifier_version = $5)
        )
    )sql");
    const auto result{enterprise::ExecPrepared(
        w,
        "EnterpriseBlockRowCovered",
        Network(),
        height,
        expected_hash.GetHex(),
        backfill_height,
        DENOMINATION_CLASSIFIER_VERSION)};
    const bool covered{!result.empty() && result.front()[0].as<bool>()};
    w.commit();
    return covered;
}

std::vector<BlockRowKey> FindCoveredBlockRows(const std::vector<BlockRowKey>& rows, int backfill_height)
{
    PgSession session;
    pqxx::work work{session.Connection()};
    std::vector<BlockRowKey> covered{FindCoveredBlockRows(work, rows, backfill_height)};
    work.commit();
    return covered;
}

std::vector<BlockRowKey> FindCoveredBlockRows(pqxx::work& w, const std::vector<BlockRowKey>& rows, int backfill_height)
{
    std::vector<BlockRowKey> covered;
    if (rows.empty()) return covered;

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
            AND ()sql";
    sql += std::to_string(backfill_height);
    sql += " < 0 OR requested.height > ";
    sql += std::to_string(backfill_height);
    sql += " OR blocks.denomination_classifier_version = ";
    sql += w.quote(DENOMINATION_CLASSIFIER_VERSION);
    sql += ")";

    for (const auto& result_row : w.exec(sql)) {
        const auto hash{uint256::FromHex(result_row[1].as<std::string>())};
        if (!hash) continue;
        covered.push_back(BlockRowKey{result_row[0].as<int>(), *hash});
    }
    return covered;
}

void MarkGapQueued(int height, const uint256& expected_hash, const std::string& source)
{
    pqxx::connection c{Connect()};
    pqxx::work w{c};
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
    enterprise::ExecPrepared(w, "EnterpriseMarkGapQueued", Network(), height, expected_hash.GetHex(), source);
    w.commit();
}

void MarkGapResolved(int height, const uint256& expected_hash, const std::string& source)
{
    PgSession session;
    pqxx::work w{session.Connection()};
    ResolveGap(session, w, height, expected_hash, source);
    w.commit();
}

void MarkGapUnavailable(int height, const uint256& expected_hash, const std::string& source, const std::string& error)
{
    try {
        pqxx::connection c{Connect()};
        pqxx::work w{c};
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
        enterprise::ExecPrepared(w, "EnterpriseMarkGapUnavailable", Network(), height, expected_hash.GetHex(), source, error);
        w.commit();
    } catch (const std::exception& e) {
        LogWarning("enterprise: failed to record unavailable gap at height %d: %s", height, e.what());
    }
}

} // namespace enterprise
