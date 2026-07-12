\set ON_ERROR_STOP on

-- Additive migration for the existing bitcoin_enterprise PostgreSQL 17 cluster.
-- This file is intentionally additive and never changes prices or its source
-- relations.

\if :{?expected_database}
\else
    \echo 'expected_database must be supplied with -v expected_database=...'
    \quit 3
\endif

SELECT current_database() = :'expected_database' AS expected_database_matches \gset
\if :expected_database_matches
\else
    \echo 'Refusing migration: connected database does not match expected_database.'
    \quit 3
\endif

SELECT current_setting('server_version_num')::integer >= 170000
       AND current_setting('server_version_num')::integer < 180000 AS server_is_pg17 \gset
\if :server_is_pg17
\else
    \echo 'Refusing migration: PostgreSQL major version 17 is required.'
    \quit 3
\endif

SET lock_timeout = '5s';
SET statement_timeout = '30min';
SET idle_in_transaction_session_timeout = '5min';
SET search_path = pg_catalog, public;

DO $migration_preflight$
DECLARE
    object_kind "char";
    primary_key_columns text[];
BEGIN
    IF session_user <> 'postgres' OR NOT EXISTS (
        SELECT 1 FROM pg_catalog.pg_roles WHERE rolname = session_user AND rolsuper
    ) THEN
        RAISE EXCEPTION 'migration must run as the postgres superuser';
    END IF;

    SELECT c.relkind
      INTO object_kind
      FROM pg_catalog.pg_class AS c
      JOIN pg_catalog.pg_namespace AS n ON n.oid = c.relnamespace
     WHERE n.nspname = 'public' AND c.relname = 'blocks';
    IF object_kind IS DISTINCT FROM 'r'::"char" THEN
        RAISE EXCEPTION 'public.blocks must be an ordinary table, found relkind %', object_kind;
    END IF;

    SELECT c.relkind
      INTO object_kind
      FROM pg_catalog.pg_class AS c
      JOIN pg_catalog.pg_namespace AS n ON n.oid = c.relnamespace
     WHERE n.nspname = 'public' AND c.relname = 'prices';
    IF object_kind IS DISTINCT FROM 'm'::"char" THEN
        RAISE EXCEPTION 'public.prices must remain the existing materialized view, found relkind %', object_kind;
    END IF;

    IF NOT EXISTS (
        SELECT 1 FROM pg_catalog.pg_attribute
         WHERE attrelid = 'public.blocks'::pg_catalog.regclass
           AND attname = 'hash' AND NOT attisdropped
           AND pg_catalog.format_type(atttypid, atttypmod) IN ('text', 'character varying')
    ) OR NOT EXISTS (
        SELECT 1 FROM pg_catalog.pg_attribute
         WHERE attrelid = 'public.blocks'::pg_catalog.regclass
           AND attname = 'network' AND NOT attisdropped
           AND pg_catalog.format_type(atttypid, atttypmod) IN ('text', 'character varying')
    ) OR NOT EXISTS (
        SELECT 1 FROM pg_catalog.pg_attribute
         WHERE attrelid = 'public.blocks'::pg_catalog.regclass
           AND attname = 'height' AND NOT attisdropped
           AND pg_catalog.format_type(atttypid, atttypmod) = 'bigint'
    ) THEN
        RAISE EXCEPTION 'public.blocks lacks compatible hash/network/height columns';
    END IF;

    SELECT array_agg(a.attname::text ORDER BY keys.ordinality)
      INTO primary_key_columns
      FROM pg_catalog.pg_constraint AS constraint_row
      CROSS JOIN LATERAL unnest(constraint_row.conkey) WITH ORDINALITY AS keys(attnum, ordinality)
      JOIN pg_catalog.pg_attribute AS a
        ON a.attrelid = constraint_row.conrelid AND a.attnum = keys.attnum
     WHERE constraint_row.conrelid = 'public.blocks'::pg_catalog.regclass
       AND constraint_row.contype = 'p';
    IF primary_key_columns IS DISTINCT FROM ARRAY['hash']::text[] THEN
        RAISE EXCEPTION 'public.blocks must retain its primary key on (hash)';
    END IF;

    IF NOT EXISTS (
        SELECT 1 FROM pg_catalog.pg_attribute
         WHERE attrelid = 'public.prices'::pg_catalog.regclass
           AND attname = 'day' AND NOT attisdropped
           AND pg_catalog.format_type(atttypid, atttypmod) IN
               ('date', 'timestamp without time zone', 'timestamp with time zone')
    ) OR NOT EXISTS (
        SELECT 1 FROM pg_catalog.pg_attribute
         WHERE attrelid = 'public.prices'::pg_catalog.regclass
           AND attname = 'price' AND NOT attisdropped
           AND pg_catalog.format_type(atttypid, atttypmod) IN
               ('smallint', 'integer', 'bigint', 'real', 'double precision', 'numeric')
    ) OR NOT EXISTS (
        SELECT 1
          FROM pg_catalog.pg_index AS i
          JOIN pg_catalog.pg_class AS index_relation ON index_relation.oid = i.indexrelid
          JOIN pg_catalog.pg_am AS access_method ON access_method.oid = index_relation.relam
         WHERE i.indrelid = 'public.prices'::pg_catalog.regclass
           AND i.indisunique AND i.indisvalid AND i.indisready
           AND i.indpred IS NULL AND i.indexprs IS NULL
           AND access_method.amname = 'btree'
           AND (SELECT array_agg(a.attname::text ORDER BY keys.ordinality)
                  FROM unnest(i.indkey) WITH ORDINALITY AS keys(attnum, ordinality)
                  JOIN pg_catalog.pg_attribute AS a
                    ON a.attrelid = i.indrelid AND a.attnum = keys.attnum)
               = ARRAY['day']::text[]
    ) THEN
        RAISE EXCEPTION 'public.prices lacks its compatible day/price shape or unique day index';
    END IF;
    IF EXISTS (SELECT 1 FROM public.prices WHERE price IS NULL LIMIT 1) THEN
        RAISE EXCEPTION 'public.prices contains NULL price rows';
    END IF;

    IF EXISTS (
        SELECT 1
          FROM public.blocks
         WHERE network IS NULL OR height IS NULL
         LIMIT 1
    ) THEN
        RAISE EXCEPTION 'public.blocks contains NULL network or height values';
    END IF;

    IF EXISTS (
        SELECT 1
          FROM public.blocks
         GROUP BY network, height
        HAVING count(*) > 1
         LIMIT 1
    ) THEN
        RAISE EXCEPTION 'public.blocks contains duplicate (network, height) rows';
    END IF;
END
$migration_preflight$;

-- Build and commit the only scanning index before taking the short metadata
-- transaction's ACCESS EXCLUSIVE lock. Refuse an existing same-name object
-- unless it is already the exact, usable index required by this migration.
DO $index_name_guard$
DECLARE
    columns text[];
    expressions text;
    is_ready boolean;
    is_unique boolean;
    is_valid boolean;
    method text;
    predicate text;
BEGIN
    IF pg_catalog.to_regclass('public.blocks_network_height_idx') IS NOT NULL THEN
        SELECT i.indisunique,
               i.indisvalid,
               i.indisready,
               i.indpred::text,
               i.indexprs::text,
               am.amname,
               (SELECT array_agg(a.attname::text ORDER BY keys.ordinality)
                  FROM unnest(i.indkey) WITH ORDINALITY AS keys(attnum, ordinality)
                  JOIN pg_catalog.pg_attribute AS a
                    ON a.attrelid = i.indrelid AND a.attnum = keys.attnum)
          INTO is_unique, is_valid, is_ready, predicate, expressions, method, columns
          FROM pg_catalog.pg_class AS idx
          JOIN pg_catalog.pg_namespace AS ns ON ns.oid = idx.relnamespace
          JOIN pg_catalog.pg_index AS i ON i.indexrelid = idx.oid
          JOIN pg_catalog.pg_am AS am ON am.oid = idx.relam
         WHERE ns.nspname = 'public'
           AND idx.relname = 'blocks_network_height_idx'
           AND i.indrelid = 'public.blocks'::pg_catalog.regclass;
        IF NOT FOUND OR NOT is_unique OR NOT is_valid OR NOT is_ready
           OR predicate IS NOT NULL OR expressions IS NOT NULL
           OR method <> 'btree'
           OR columns <> ARRAY['network', 'height']::text[] THEN
            RAISE EXCEPTION 'existing public.blocks_network_height_idx has the wrong shape or is invalid';
        END IF;
    END IF;
END
$index_name_guard$;

CREATE UNIQUE INDEX IF NOT EXISTS blocks_network_height_idx
    ON public.blocks (network, height);

BEGIN;
SET LOCAL lock_timeout = '5s';
SET LOCAL statement_timeout = '5min';
SET LOCAL idle_in_transaction_session_timeout = '1min';
SET LOCAL search_path = pg_catalog, public;

-- Acquire the lock up front so a busy writer causes a bounded, clean abort
-- before any metadata changes occur.
LOCK TABLE public.blocks IN ACCESS EXCLUSIVE MODE;

CREATE TEMPORARY TABLE enterprise_pg17_protected_prices_snapshot ON COMMIT DROP AS
SELECT c.oid AS relation_oid,
       c.relkind,
       c.relowner,
       c.relacl::text AS relacl,
       pg_catalog.pg_get_viewdef(c.oid, true) AS view_definition,
       (SELECT pg_catalog.jsonb_agg(
                   pg_catalog.jsonb_build_object(
                       'name', a.attname,
                       'type', pg_catalog.format_type(a.atttypid, a.atttypmod),
                       'not_null', a.attnotnull)
                   ORDER BY a.attnum)
          FROM pg_catalog.pg_attribute AS a
         WHERE a.attrelid = c.oid AND a.attnum > 0 AND NOT a.attisdropped) AS columns,
       (SELECT pg_catalog.jsonb_agg(pg_catalog.pg_get_indexdef(i.indexrelid) ORDER BY i.indexrelid)
          FROM pg_catalog.pg_index AS i
         WHERE i.indrelid = c.oid) AS indexes
  FROM pg_catalog.pg_class AS c
  JOIN pg_catalog.pg_namespace AS n ON n.oid = c.relnamespace
 WHERE n.nspname = 'public' AND c.relname = 'prices';

ALTER TABLE public.blocks
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
    ADD COLUMN IF NOT EXISTS denomination_classifier_version TEXT;

CREATE TABLE IF NOT EXISTS public.enterprise_block_ingest
(
    network      TEXT NOT NULL,
    hash         TEXT NOT NULL,
    height       BIGINT NOT NULL,
    event_type   TEXT NOT NULL,
    status       TEXT NOT NULL,
    source       TEXT NOT NULL,
    spool_path   TEXT,
    attempts     BIGINT NOT NULL DEFAULT 0,
    last_error   TEXT,
    queued_at    TIMESTAMP WITH TIME ZONE NOT NULL DEFAULT now(),
    updated_at   TIMESTAMP WITH TIME ZONE NOT NULL DEFAULT now(),
    completed_at TIMESTAMP WITH TIME ZONE,
    PRIMARY KEY (network, hash, event_type)
);

CREATE TABLE IF NOT EXISTS public.enterprise_block_gaps
(
    network       TEXT NOT NULL,
    height        BIGINT NOT NULL,
    expected_hash TEXT,
    status        TEXT NOT NULL,
    source        TEXT,
    last_error    TEXT,
    first_seen_at TIMESTAMP WITH TIME ZONE NOT NULL DEFAULT now(),
    updated_at    TIMESTAMP WITH TIME ZONE NOT NULL DEFAULT now(),
    PRIMARY KEY (network, height)
);

CREATE TABLE IF NOT EXISTS public.mempool_entries
(
    txid                 TEXT PRIMARY KEY,
    network              TEXT,
    wtxid                TEXT,
    fee                  BIGINT,
    weight               BIGINT,
    memory_usage         BIGINT,
    entry_time           TIMESTAMP WITH TIME ZONE,
    entry_height         BIGINT,
    spends_coinbase      BOOLEAN,
    sigop_cost           BIGINT,
    height_lockpoint     BIGINT,
    time_lockpoint       TIMESTAMP WITH TIME ZONE,
    descendants_count    BIGINT,
    descendants_size     BIGINT,
    descendants_fees     BIGINT,
    ancestors_count      BIGINT,
    ancestors_size       BIGINT,
    ancestors_fees       BIGINT,
    ancestors_sigop_cost BIGINT,
    removal_reason       TEXT,
    removal_time         TIMESTAMP WITH TIME ZONE
);

CREATE TABLE IF NOT EXISTS public.enterprise_schema_migrations
(
    version     TEXT PRIMARY KEY,
    description TEXT NOT NULL,
    applied_at  TIMESTAMP WITH TIME ZONE NOT NULL DEFAULT now(),
    applied_by  TEXT NOT NULL DEFAULT SESSION_USER
);

CREATE INDEX IF NOT EXISTS enterprise_block_ingest_monitor_idx
    ON public.enterprise_block_ingest (network, source, status, height);

INSERT INTO public.enterprise_schema_migrations (version, description)
VALUES (
    '20260711_pg17_reuse_v1',
    'Additive PostgreSQL 17 reuse schema for enterprise denomination backfill'
)
ON CONFLICT (version) DO NOTHING;

\ir 20260711_pg17_reuse_v1_checks.sql

DO $protected_prices_unchanged$
DECLARE
    changed boolean;
BEGIN
    SELECT EXISTS (
        SELECT relation_oid, relkind, relowner, relacl, view_definition, columns,
               indexes
          FROM enterprise_pg17_protected_prices_snapshot
        EXCEPT
        SELECT c.oid,
               c.relkind,
               c.relowner,
               c.relacl::text,
               pg_catalog.pg_get_viewdef(c.oid, true),
               (SELECT pg_catalog.jsonb_agg(
                           pg_catalog.jsonb_build_object(
                               'name', a.attname,
                               'type', pg_catalog.format_type(a.atttypid, a.atttypmod),
                               'not_null', a.attnotnull)
                           ORDER BY a.attnum)
                  FROM pg_catalog.pg_attribute AS a
                 WHERE a.attrelid = c.oid AND a.attnum > 0 AND NOT a.attisdropped),
               (SELECT pg_catalog.jsonb_agg(pg_catalog.pg_get_indexdef(i.indexrelid) ORDER BY i.indexrelid)
                  FROM pg_catalog.pg_index AS i
                 WHERE i.indrelid = c.oid)
          FROM pg_catalog.pg_class AS c
          JOIN pg_catalog.pg_namespace AS n ON n.oid = c.relnamespace
         WHERE n.nspname = 'public' AND c.relname = 'prices'
    ) INTO changed;

    IF changed OR (SELECT count(*) FROM enterprise_pg17_protected_prices_snapshot) <> 1 THEN
        RAISE EXCEPTION 'public.prices changed during additive migration';
    END IF;
END
$protected_prices_unchanged$;

COMMIT;

\echo 'Applied and verified migration 20260711_pg17_reuse_v1.'
