\set ON_ERROR_STOP on

\if :{?expected_database}
\else
    \echo 'expected_database must be supplied with -v expected_database=...'
    \quit 3
\endif

SELECT current_database() = :'expected_database' AS expected_database_matches \gset
\if :expected_database_matches
\else
    \echo 'Refusing verification: connected database does not match expected_database.'
    \quit 3
\endif

SELECT current_setting('server_version_num')::integer >= 170000
       AND current_setting('server_version_num')::integer < 180000 AS server_is_pg17 \gset
\if :server_is_pg17
\else
    \echo 'Refusing verification: PostgreSQL major version 17 is required.'
    \quit 3
\endif

BEGIN TRANSACTION READ ONLY;
SET LOCAL statement_timeout = '5min';
SET LOCAL idle_in_transaction_session_timeout = '1min';
SET LOCAL search_path = pg_catalog, public;

\ir 20260711_pg17_reuse_v1_checks.sql

SELECT 'migration' AS check_name,
       version AS value,
       applied_at,
       applied_by
  FROM public.enterprise_schema_migrations
 WHERE version = '20260711_pg17_reuse_v1';

SELECT 'blocks' AS check_name,
       count(*) AS row_count,
       count(DISTINCT (network, height)) AS distinct_network_heights,
       min(height) FILTER (WHERE network = 'mainnet') AS mainnet_min_height,
       max(height) FILTER (WHERE network = 'mainnet') AS mainnet_max_height
  FROM public.blocks;

SELECT 'prices' AS check_name,
       count(*) AS row_count,
       min(day) AS first_day,
       max(day) AS last_day,
       count(*) FILTER (WHERE price IS NULL) AS null_prices
  FROM public.prices;

COMMIT;
\echo 'Verified migration 20260711_pg17_reuse_v1.'
