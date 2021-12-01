SELECT b.daydate,
       heights,
       total_size,
       side,
       script_type,
       sum(script_type_size)                                         as script_type_size,
       (sum(script_type_size)::float / total_size::float * 100)::int as percentage
FROM (
         SELECT median_time::date                                         as daydate,
                height,
                (jsonb_array_elements(output_script_types) ->> 0)::bigint as script_type,
                (jsonb_array_elements(output_script_types) ->> 2)::bigint as script_type_size,
                quote_literal('output')                                   as side
         FROM bitcoin.blocks
         UNION ALL
         SELECT median_time::date                                        as daydate,
                height,
                (jsonb_array_elements(input_script_types) ->> 0)::bigint as script_type,
                (jsonb_array_elements(input_script_types) ->> 3)::bigint as script_type_size,
                quote_literal('input')                                   as side
         FROM bitcoin.blocks
         ORDER BY height desc
     ) as b
         LEFT JOIN (
    SELECT median_time::date as daydate,
           count(height)     as heights,
           sum(total_size)   as total_size
    FROM bitcoin.blocks
    GROUP BY median_time::date
) c ON b.daydate = c.daydate
GROUP BY b.daydate, heights, total_size, side, script_type
ORDER BY b.daydate desc;