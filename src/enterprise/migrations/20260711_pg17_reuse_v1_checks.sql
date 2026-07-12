-- Shared shape checks. This fragment is included by the migration inside its
-- write transaction and by the standalone verifier inside a read-only one.

DO $schema_checks$
DECLARE
    actual_default text;
    actual_generated text;
    actual_identity text;
    actual_not_null boolean;
    actual_type text;
    blocking_columns text;
    expected record;
    index_columns text[];
    index_expressions text;
    index_is_ready boolean;
    index_is_unique boolean;
    index_is_valid boolean;
    index_method text;
    index_predicate text;
    object_kind "char";
    primary_key_columns text[];
    prices_unique_day_index boolean;
    marker_count bigint;
    marker_description text;
    managed_block_columns text[] := ARRAY[]::text[];
BEGIN
    SELECT c.relkind INTO object_kind
      FROM pg_catalog.pg_class AS c
      JOIN pg_catalog.pg_namespace AS n ON n.oid = c.relnamespace
     WHERE n.nspname = 'public' AND c.relname = 'blocks';
    IF object_kind IS DISTINCT FROM 'r'::"char" THEN
        RAISE EXCEPTION 'public.blocks must be an ordinary table';
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

    FOR expected IN
        SELECT * FROM (VALUES
            ('hash', 'text', true),
            ('merkle_root', 'text', true),
            ('time', 'timestamp with time zone', false),
            ('median_time', 'timestamp with time zone', false),
            ('height', 'bigint', false),
            ('subsidy', 'bigint', false),
            ('transactions_count', 'bigint', false),
            ('version', 'bigint', false),
            ('status', 'bigint', false),
            ('bits', 'bigint', false),
            ('nonce', 'bigint', false),
            ('difficulty', 'double precision', false),
            ('chain_work', 'text', true),
            ('outputs_count', 'bigint', false),
            ('inputs_count', 'bigint', false),
            ('total_output_value', 'bigint', false),
            ('total_input_value', 'bigint', false),
            ('total_fees', 'bigint', false),
            ('total_size', 'bigint', false),
            ('total_vsize', 'bigint', false),
            ('total_weight', 'bigint', false),
            ('fee_rates', 'jsonb', false),
            ('output_data', 'jsonb', false),
            ('input_data', 'jsonb', false),
            ('transaction_data', 'jsonb', false),
            ('output_script_types', 'jsonb', false),
            ('input_script_types', 'jsonb', false),
            ('output_legacy_signature_operations', 'bigint', false),
            ('input_legacy_signature_operations', 'bigint', false),
            ('input_p2sh_signature_operations', 'bigint', false),
            ('input_witness_signature_operations', 'bigint', false),
            ('outputs_total_size', 'bigint', false),
            ('inputs_total_size', 'bigint', false),
            ('net_utxo_size_impact', 'bigint', false),
            ('hash_prev_block', 'text', true),
            ('network', 'text', true),
            ('nonstandard_create_count', 'bigint', false),
            ('pubkey_create_count', 'bigint', false),
            ('pubkeyhash_create_count', 'bigint', false),
            ('scripthash_create_count', 'bigint', false),
            ('multisig_create_count', 'bigint', false),
            ('null_data_create_count', 'bigint', false),
            ('witness_v0_keyhash_create_count', 'bigint', false),
            ('witness_v0_scripthash_create_count', 'bigint', false),
            ('witness_v1_taproot_create_count', 'bigint', false),
            ('witness_unknown_create_count', 'bigint', false),
            ('nonstandard_spend_count', 'bigint', false),
            ('pubkey_spend_count', 'bigint', false),
            ('pubkeyhash_spend_count', 'bigint', false),
            ('scripthash_spend_count', 'bigint', false),
            ('multisig_spend_count', 'bigint', false),
            ('null_data_spend_count', 'bigint', false),
            ('witness_v0_keyhash_spend_count', 'bigint', false),
            ('witness_v0_scripthash_spend_count', 'bigint', false),
            ('witness_v1_taproot_spend_count', 'bigint', false),
            ('witness_unknown_spend_count', 'bigint', false),
            ('coinbase', 'bigint', false),
            ('ordinals_weight', 'bigint', false),
            ('ordinals_count', 'bigint', false),
            ('ordinals_size', 'bigint', false),
            ('ordinals_vsize', 'bigint', false),
            ('ordinals_fees', 'bigint', false),
            ('non_ordinals_weight', 'bigint', false),
            ('non_ordinals_count', 'bigint', false),
            ('non_ordinals_size', 'bigint', false),
            ('non_ordinals_vsize', 'bigint', false),
            ('non_ordinals_fees', 'bigint', false),
            ('btc_usd_price', 'double precision', false),
            ('btc_usd_price_low', 'double precision', false),
            ('btc_usd_price_high', 'double precision', false),
            ('btc_usd_price_source', 'text', false),
            ('denomination_eligible_outputs_count', 'bigint', false),
            ('denomination_eligible_value_sats', 'bigint', false),
            ('usd_denom_outputs_count', 'bigint', false),
            ('usd_denom_value_sats', 'bigint', false),
            ('usd_denom_confidence_sum', 'double precision', false),
            ('sats_denom_outputs_count', 'bigint', false),
            ('sats_denom_value_sats', 'bigint', false),
            ('sats_denom_confidence_sum', 'double precision', false),
            ('unknown_denom_outputs_count', 'bigint', false),
            ('unknown_denom_value_sats', 'bigint', false),
            ('ambiguous_denom_outputs_count', 'bigint', false),
            ('ambiguous_denom_value_sats', 'bigint', false),
            ('likely_change_outputs_count', 'bigint', false),
            ('likely_change_value_sats', 'bigint', false),
            ('denomination_classifier_version', 'text', false)
        ) AS managed_insert_columns(column_name, data_type, text_compatible)
    LOOP
        managed_block_columns := array_append(managed_block_columns, expected.column_name);
        SELECT pg_catalog.format_type(a.atttypid, a.atttypmod),
               a.attgenerated::text,
               a.attidentity::text
          INTO actual_type, actual_generated, actual_identity
          FROM pg_catalog.pg_attribute AS a
         WHERE a.attrelid = 'public.blocks'::pg_catalog.regclass
           AND a.attname = expected.column_name
           AND a.attnum > 0
           AND NOT a.attisdropped;
        IF NOT FOUND
           OR (actual_type <> expected.data_type AND NOT (
               expected.text_compatible AND actual_type IN ('text', 'character varying')))
           OR actual_generated <> '' OR actual_identity <> '' THEN
            RAISE EXCEPTION 'invalid managed INSERT column public.blocks.% (type=%, generated=%, identity=%)',
                expected.column_name, actual_type, actual_generated, actual_identity;
        END IF;
    END LOOP;

    SELECT string_agg(a.attname::text, ', ' ORDER BY a.attnum)
      INTO blocking_columns
      FROM pg_catalog.pg_attribute AS a
      LEFT JOIN pg_catalog.pg_attrdef AS d
        ON d.adrelid = a.attrelid AND d.adnum = a.attnum
     WHERE a.attrelid = 'public.blocks'::pg_catalog.regclass
       AND a.attnum > 0 AND NOT a.attisdropped
       AND a.attnotnull AND d.oid IS NULL
       AND a.attgenerated = '' AND a.attidentity = ''
       AND NOT (a.attname::text = ANY(managed_block_columns));
    IF blocking_columns IS NOT NULL THEN
        RAISE EXCEPTION 'public.blocks has unmanaged NOT NULL columns without defaults that block INSERT: %',
            blocking_columns;
    END IF;

    FOR expected IN
        SELECT * FROM (VALUES
            ('btc_usd_price', 'double precision'),
            ('btc_usd_price_low', 'double precision'),
            ('btc_usd_price_high', 'double precision'),
            ('btc_usd_price_source', 'text'),
            ('denomination_eligible_outputs_count', 'bigint'),
            ('denomination_eligible_value_sats', 'bigint'),
            ('usd_denom_outputs_count', 'bigint'),
            ('usd_denom_value_sats', 'bigint'),
            ('usd_denom_confidence_sum', 'double precision'),
            ('sats_denom_outputs_count', 'bigint'),
            ('sats_denom_value_sats', 'bigint'),
            ('sats_denom_confidence_sum', 'double precision'),
            ('unknown_denom_outputs_count', 'bigint'),
            ('unknown_denom_value_sats', 'bigint'),
            ('ambiguous_denom_outputs_count', 'bigint'),
            ('ambiguous_denom_value_sats', 'bigint'),
            ('likely_change_outputs_count', 'bigint'),
            ('likely_change_value_sats', 'bigint'),
            ('denomination_classifier_version', 'text')
        ) AS required_columns(column_name, data_type)
    LOOP
        SELECT pg_catalog.format_type(a.atttypid, a.atttypmod),
               a.attnotnull,
               pg_catalog.pg_get_expr(d.adbin, d.adrelid),
               a.attgenerated::text,
               a.attidentity::text
          INTO actual_type, actual_not_null, actual_default, actual_generated, actual_identity
          FROM pg_catalog.pg_attribute AS a
          LEFT JOIN pg_catalog.pg_attrdef AS d
            ON d.adrelid = a.attrelid AND d.adnum = a.attnum
         WHERE a.attrelid = 'public.blocks'::pg_catalog.regclass
           AND a.attname = expected.column_name
           AND a.attnum > 0
           AND NOT a.attisdropped;
        IF NOT FOUND OR actual_type <> expected.data_type OR actual_not_null OR actual_default IS NOT NULL
           OR actual_generated <> '' OR actual_identity <> '' THEN
            RAISE EXCEPTION 'invalid public.blocks column % (type=%, not_null=%, default=%, generated=%, identity=%)',
                expected.column_name, actual_type, actual_not_null, actual_default, actual_generated, actual_identity;
        END IF;
    END LOOP;

    FOR expected IN
        SELECT * FROM (VALUES
            ('enterprise_block_ingest', ARRAY[
                'network', 'hash', 'height', 'event_type', 'status', 'source', 'spool_path',
                'attempts', 'last_error', 'queued_at', 'updated_at', 'completed_at']::text[]),
            ('enterprise_block_gaps', ARRAY[
                'network', 'height', 'expected_hash', 'status', 'source', 'last_error',
                'first_seen_at', 'updated_at']::text[]),
            ('mempool_entries', ARRAY[
                'txid', 'network', 'wtxid', 'fee', 'weight', 'memory_usage', 'entry_time',
                'entry_height', 'spends_coinbase', 'sigop_cost', 'height_lockpoint', 'time_lockpoint',
                'descendants_count', 'descendants_size', 'descendants_fees', 'ancestors_count',
                'ancestors_size', 'ancestors_fees', 'ancestors_sigop_cost', 'removal_reason',
                'removal_time']::text[])
        ) AS managed_tables(table_name, column_names)
    LOOP
        SELECT string_agg(a.attname::text, ', ' ORDER BY a.attnum)
          INTO blocking_columns
          FROM pg_catalog.pg_attribute AS a
          JOIN pg_catalog.pg_class AS c ON c.oid = a.attrelid
          JOIN pg_catalog.pg_namespace AS n ON n.oid = c.relnamespace
          LEFT JOIN pg_catalog.pg_attrdef AS d
            ON d.adrelid = a.attrelid AND d.adnum = a.attnum
         WHERE n.nspname = 'public' AND c.relname = expected.table_name
           AND a.attnum > 0 AND NOT a.attisdropped
           AND a.attnotnull AND d.oid IS NULL
           AND a.attgenerated = '' AND a.attidentity = ''
           AND NOT (a.attname::text = ANY(expected.column_names));
        IF blocking_columns IS NOT NULL THEN
            RAISE EXCEPTION 'public.% has unmanaged NOT NULL columns without defaults that block INSERT: %',
                expected.table_name, blocking_columns;
        END IF;
    END LOOP;

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
      INTO index_is_unique, index_is_valid, index_is_ready, index_predicate,
           index_expressions, index_method, index_columns
      FROM pg_catalog.pg_class AS idx
      JOIN pg_catalog.pg_namespace AS ns ON ns.oid = idx.relnamespace
      JOIN pg_catalog.pg_index AS i ON i.indexrelid = idx.oid
      JOIN pg_catalog.pg_am AS am ON am.oid = idx.relam
     WHERE ns.nspname = 'public' AND idx.relname = 'blocks_network_height_idx'
       AND i.indrelid = 'public.blocks'::pg_catalog.regclass;
    IF NOT FOUND OR NOT index_is_unique OR NOT index_is_valid OR NOT index_is_ready
       OR index_predicate IS NOT NULL OR index_expressions IS NOT NULL
       OR index_method <> 'btree'
       OR index_columns <> ARRAY['network', 'height']::text[] THEN
        RAISE EXCEPTION 'public.blocks_network_height_idx has the wrong definition or is invalid';
    END IF;

    FOR expected IN
        SELECT * FROM (VALUES
            ('enterprise_block_ingest', 'network', 'text', true, NULL::text),
            ('enterprise_block_ingest', 'hash', 'text', true, NULL::text),
            ('enterprise_block_ingest', 'height', 'bigint', true, NULL::text),
            ('enterprise_block_ingest', 'event_type', 'text', true, NULL::text),
            ('enterprise_block_ingest', 'status', 'text', true, NULL::text),
            ('enterprise_block_ingest', 'source', 'text', true, NULL::text),
            ('enterprise_block_ingest', 'spool_path', 'text', false, NULL::text),
            ('enterprise_block_ingest', 'attempts', 'bigint', true, '0'),
            ('enterprise_block_ingest', 'last_error', 'text', false, NULL::text),
            ('enterprise_block_ingest', 'queued_at', 'timestamp with time zone', true, 'now()'),
            ('enterprise_block_ingest', 'updated_at', 'timestamp with time zone', true, 'now()'),
            ('enterprise_block_ingest', 'completed_at', 'timestamp with time zone', false, NULL::text),
            ('enterprise_block_gaps', 'network', 'text', true, NULL::text),
            ('enterprise_block_gaps', 'height', 'bigint', true, NULL::text),
            ('enterprise_block_gaps', 'expected_hash', 'text', false, NULL::text),
            ('enterprise_block_gaps', 'status', 'text', true, NULL::text),
            ('enterprise_block_gaps', 'source', 'text', false, NULL::text),
            ('enterprise_block_gaps', 'last_error', 'text', false, NULL::text),
            ('enterprise_block_gaps', 'first_seen_at', 'timestamp with time zone', true, 'now()'),
            ('enterprise_block_gaps', 'updated_at', 'timestamp with time zone', true, 'now()'),
            ('mempool_entries', 'txid', 'text', true, NULL::text),
            ('mempool_entries', 'network', 'text', false, NULL::text),
            ('mempool_entries', 'wtxid', 'text', false, NULL::text),
            ('mempool_entries', 'fee', 'bigint', false, NULL::text),
            ('mempool_entries', 'weight', 'bigint', false, NULL::text),
            ('mempool_entries', 'memory_usage', 'bigint', false, NULL::text),
            ('mempool_entries', 'entry_time', 'timestamp with time zone', false, NULL::text),
            ('mempool_entries', 'entry_height', 'bigint', false, NULL::text),
            ('mempool_entries', 'spends_coinbase', 'boolean', false, NULL::text),
            ('mempool_entries', 'sigop_cost', 'bigint', false, NULL::text),
            ('mempool_entries', 'height_lockpoint', 'bigint', false, NULL::text),
            ('mempool_entries', 'time_lockpoint', 'timestamp with time zone', false, NULL::text),
            ('mempool_entries', 'descendants_count', 'bigint', false, NULL::text),
            ('mempool_entries', 'descendants_size', 'bigint', false, NULL::text),
            ('mempool_entries', 'descendants_fees', 'bigint', false, NULL::text),
            ('mempool_entries', 'ancestors_count', 'bigint', false, NULL::text),
            ('mempool_entries', 'ancestors_size', 'bigint', false, NULL::text),
            ('mempool_entries', 'ancestors_fees', 'bigint', false, NULL::text),
            ('mempool_entries', 'ancestors_sigop_cost', 'bigint', false, NULL::text),
            ('mempool_entries', 'removal_reason', 'text', false, NULL::text),
            ('mempool_entries', 'removal_time', 'timestamp with time zone', false, NULL::text),
            ('enterprise_schema_migrations', 'version', 'text', true, NULL::text),
            ('enterprise_schema_migrations', 'description', 'text', true, NULL::text),
            ('enterprise_schema_migrations', 'applied_at', 'timestamp with time zone', true, 'now()'),
            ('enterprise_schema_migrations', 'applied_by', 'text', true, 'SESSION_USER')
        ) AS required_columns(table_name, column_name, data_type, not_null, default_expression)
    LOOP
        SELECT pg_catalog.format_type(a.atttypid, a.atttypmod),
               a.attnotnull,
               pg_catalog.pg_get_expr(d.adbin, d.adrelid),
               a.attgenerated::text,
               a.attidentity::text
          INTO actual_type, actual_not_null, actual_default, actual_generated, actual_identity
          FROM pg_catalog.pg_attribute AS a
          JOIN pg_catalog.pg_class AS c ON c.oid = a.attrelid
          JOIN pg_catalog.pg_namespace AS n ON n.oid = c.relnamespace
          LEFT JOIN pg_catalog.pg_attrdef AS d
            ON d.adrelid = a.attrelid AND d.adnum = a.attnum
         WHERE n.nspname = 'public'
           AND c.relname = expected.table_name
           AND c.relkind = 'r'
           AND a.attname = expected.column_name
           AND a.attnum > 0
           AND NOT a.attisdropped;
        IF NOT FOUND
           OR actual_type <> expected.data_type
           OR actual_not_null <> expected.not_null
           OR actual_default IS DISTINCT FROM expected.default_expression
           OR actual_generated <> '' OR actual_identity <> '' THEN
            RAISE EXCEPTION 'invalid %.% (type=%, not_null=%, default=%, generated=%, identity=%)',
                expected.table_name, expected.column_name, actual_type, actual_not_null,
                actual_default, actual_generated, actual_identity;
        END IF;
    END LOOP;

    FOR expected IN
        SELECT * FROM (VALUES
            ('blocks', ARRAY['hash']::text[]),
            ('enterprise_block_ingest', ARRAY['network', 'hash', 'event_type']::text[]),
            ('enterprise_block_gaps', ARRAY['network', 'height']::text[]),
            ('mempool_entries', ARRAY['txid']::text[]),
            ('enterprise_schema_migrations', ARRAY['version']::text[])
        ) AS required_keys(table_name, key_columns)
    LOOP
        SELECT array_agg(a.attname::text ORDER BY keys.ordinality)
          INTO primary_key_columns
          FROM pg_catalog.pg_constraint AS constraint_row
          CROSS JOIN LATERAL unnest(constraint_row.conkey) WITH ORDINALITY AS keys(attnum, ordinality)
          JOIN pg_catalog.pg_attribute AS a
            ON a.attrelid = constraint_row.conrelid AND a.attnum = keys.attnum
         WHERE constraint_row.conrelid = pg_catalog.to_regclass('public.' || expected.table_name)
           AND constraint_row.contype = 'p';
        IF primary_key_columns IS DISTINCT FROM expected.key_columns THEN
            RAISE EXCEPTION 'invalid primary key on public.%: found %, expected %',
                expected.table_name, primary_key_columns, expected.key_columns;
        END IF;
    END LOOP;

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
      INTO index_is_unique, index_is_valid, index_is_ready, index_predicate,
           index_expressions, index_method, index_columns
      FROM pg_catalog.pg_class AS idx
      JOIN pg_catalog.pg_namespace AS ns ON ns.oid = idx.relnamespace
      JOIN pg_catalog.pg_index AS i ON i.indexrelid = idx.oid
      JOIN pg_catalog.pg_am AS am ON am.oid = idx.relam
     WHERE ns.nspname = 'public' AND idx.relname = 'enterprise_block_ingest_monitor_idx'
       AND i.indrelid = 'public.enterprise_block_ingest'::pg_catalog.regclass;
    IF NOT FOUND OR index_is_unique OR NOT index_is_valid OR NOT index_is_ready
       OR index_predicate IS NOT NULL OR index_expressions IS NOT NULL
       OR index_method <> 'btree'
       OR index_columns <> ARRAY['network', 'source', 'status', 'height']::text[] THEN
        RAISE EXCEPTION 'public.enterprise_block_ingest_monitor_idx has the wrong definition or is invalid';
    END IF;

    SELECT c.relkind INTO object_kind
      FROM pg_catalog.pg_class AS c
      JOIN pg_catalog.pg_namespace AS n ON n.oid = c.relnamespace
     WHERE n.nspname = 'public' AND c.relname = 'prices';
    IF object_kind IS DISTINCT FROM 'm'::"char" THEN
        RAISE EXCEPTION 'public.prices is not the expected materialized view';
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
    ) THEN
        RAISE EXCEPTION 'public.prices lacks a compatible day/price shape';
    END IF;
    SELECT EXISTS (
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
    ) INTO prices_unique_day_index;
    IF NOT prices_unique_day_index THEN
        RAISE EXCEPTION 'public.prices must retain a valid unique index on (day)';
    END IF;
    IF EXISTS (SELECT 1 FROM public.prices WHERE price IS NULL LIMIT 1) THEN
        RAISE EXCEPTION 'public.prices contains NULL price rows';
    END IF;

    EXECUTE $marker_query$
        SELECT count(*), min(description)
          FROM public.enterprise_schema_migrations
         WHERE version = '20260711_pg17_reuse_v1'
    $marker_query$ INTO marker_count, marker_description;
    IF marker_count <> 1 OR marker_description IS DISTINCT FROM
       'Additive PostgreSQL 17 reuse schema for enterprise denomination backfill' THEN
        RAISE EXCEPTION 'migration marker 20260711_pg17_reuse_v1 is missing or malformed';
    END IF;
END
$schema_checks$;
