\set ON_ERROR_STOP on

-- Additive migration: change-flow columns on public.blocks, the output value a
-- block's transactions send back to their own wallets.
--
--   self_change_outputs_count, self_change_value_sats
--       outputs paying a script that one of the same transaction's inputs
--       spends: change returned to a reused address
--   forward_outputs_count, forward_value_sats
--       the kept output of a same-day forward: a transaction that spends one
--       coin of at least 100 BTC created fewer than 144 blocks earlier and
--       passes at least 99% of it to a different script in one of exactly two
--       outputs
--
-- Nullable columns without defaults change only the catalog, so this is
-- instant on the full blocks table. Nodes built before this migration keep
-- inserting rows with the columns NULL; the enterprise writer fills NULL rows
-- from local block and undo data (-enterprisespentagebackfill).
--
-- Run as the owner of public.blocks:
--   psql -X -v expected_database=<db> -f 20261008_change_flows.sql

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
        SELECT 1 FROM public.enterprise_schema_migrations WHERE version = '20261005_spent_coin_age'
    ) THEN
        RAISE EXCEPTION 'apply migration 20261005_spent_coin_age first';
    END IF;
END
$migration_preflight$;

ALTER TABLE public.blocks
    ADD COLUMN IF NOT EXISTS self_change_outputs_count bigint,
    ADD COLUMN IF NOT EXISTS self_change_value_sats bigint,
    ADD COLUMN IF NOT EXISTS forward_outputs_count bigint,
    ADD COLUMN IF NOT EXISTS forward_value_sats bigint;

COMMENT ON COLUMN public.blocks.self_change_outputs_count IS
    'Outputs paying a script that one of the same transaction''s inputs spends. NULL until computed.';
COMMENT ON COLUMN public.blocks.self_change_value_sats IS
    'Value of the outputs counted in self_change_outputs_count.';
COMMENT ON COLUMN public.blocks.forward_outputs_count IS
    'Same-day forwards: one-input, two-output transactions spending a coin of at least 100 BTC created fewer than 144 blocks earlier and passing at least 99% of it to a different script. NULL until computed.';
COMMENT ON COLUMN public.blocks.forward_value_sats IS
    'Value of the kept outputs counted in forward_outputs_count.';

INSERT INTO public.enterprise_schema_migrations (version, description)
VALUES ('20261008_change_flows', 'Change-flow columns on blocks')
ON CONFLICT (version) DO NOTHING;

DO $migration_verify$
DECLARE
    expected record;
    actual_type text;
    actual_not_null boolean;
BEGIN
    FOR expected IN
        SELECT * FROM (VALUES
            ('self_change_outputs_count', 'bigint'),
            ('self_change_value_sats', 'bigint'),
            ('forward_outputs_count', 'bigint'),
            ('forward_value_sats', 'bigint')
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

\echo 'Applied and verified migration 20261008_change_flows.'
