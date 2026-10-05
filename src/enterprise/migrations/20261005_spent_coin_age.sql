\set ON_ERROR_STOP on

-- Additive migration: spent-coin age columns on public.blocks.
--
--   coin_days_destroyed  value (BTC) times age (days) summed over the coins a
--                        block spends; age is the spending block's median time
--                        past minus that of the block that created the coin
--   spent_age_bands      [[min_days, inputs, value_sats], ...] for the
--                        non-empty age bands 0, 1, 7, 30, 90, 155, 180, 365,
--                        730, 1095, 1825, 2555, 3650 and 5475 days
--   oldest_spent_height  height the oldest spent coin was created at
--
-- Nullable columns without defaults change only the catalog, so this is
-- instant on the full blocks table. Nodes built before this migration keep
-- inserting rows with the columns NULL; the enterprise writer fills NULL rows
-- from local block and undo data (-enterprisespentagebackfill).
--
-- Run as the owner of public.blocks:
--   psql -X -v expected_database=<db> -f 20261005_spent_coin_age.sql

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
       AND current_setting('server_version_num')::integer < 190000 AS server_is_supported \gset
\if :server_is_supported
\else
    \echo 'Refusing migration: PostgreSQL major version 17 or 18 is required.'
    \quit 3
\endif

-- ADD COLUMN needs a brief ACCESS EXCLUSIVE lock. Fail fast rather than queue
-- behind a long query and block every reader of the table meanwhile.
SET lock_timeout = '5s';
SET statement_timeout = '1min';
SET search_path = pg_catalog, public;

BEGIN;

DO $migration_preflight$
BEGIN
    IF to_regclass('public.enterprise_schema_migrations') IS NULL OR NOT EXISTS (
        SELECT 1 FROM public.enterprise_schema_migrations WHERE version = '20260711_pg17_reuse_v1'
    ) THEN
        RAISE EXCEPTION 'apply migration 20260711_pg17_reuse_v1 first';
    END IF;
END
$migration_preflight$;

ALTER TABLE public.blocks
    ADD COLUMN IF NOT EXISTS coin_days_destroyed double precision,
    ADD COLUMN IF NOT EXISTS spent_age_bands jsonb,
    ADD COLUMN IF NOT EXISTS oldest_spent_height bigint;

COMMENT ON COLUMN public.blocks.coin_days_destroyed IS
    'Sum over spent coins of value (BTC) times age (days); age uses median time past. NULL until computed.';
COMMENT ON COLUMN public.blocks.spent_age_bands IS
    'Spent coins by age band: [[min_days, inputs, value_sats], ...], non-empty bands only.';
COMMENT ON COLUMN public.blocks.oldest_spent_height IS
    'Height the oldest coin spent in this block was created at; NULL when the block spends nothing.';

INSERT INTO public.enterprise_schema_migrations (version, description)
VALUES ('20261005_spent_coin_age', 'Spent-coin age columns on blocks')
ON CONFLICT (version) DO NOTHING;

DO $migration_verify$
DECLARE
    expected record;
    actual_type text;
    actual_not_null boolean;
BEGIN
    FOR expected IN
        SELECT * FROM (VALUES
            ('coin_days_destroyed', 'double precision'),
            ('spent_age_bands', 'jsonb'),
            ('oldest_spent_height', 'bigint')
        ) AS columns(name, type)
    LOOP
        SELECT pg_catalog.format_type(a.atttypid, a.atttypmod), a.attnotnull
          INTO actual_type, actual_not_null
          FROM pg_catalog.pg_attribute AS a
         WHERE a.attrelid = 'public.blocks'::regclass
           AND a.attname = expected.name
           AND a.attnum > 0
           AND NOT a.attisdropped;
        IF actual_type IS DISTINCT FROM expected.type OR actual_not_null THEN
            RAISE EXCEPTION 'public.blocks.% must be a nullable %, found % (not null: %)',
                expected.name, expected.type, actual_type, actual_not_null;
        END IF;
    END LOOP;
END
$migration_verify$;

COMMIT;

\echo 'Applied and verified migration 20261005_spent_coin_age.'
