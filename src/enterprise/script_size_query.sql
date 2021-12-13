SELECT b.daydate,
       heights,
       round(total_size / 1000000 / heights, 2),
       side,
       script_type,
       round(sum(script_type_size) / 1000000 / heights, 2)           as script_type_size,
       (sum(script_type_size)::float / total_size::float * 100)::int as percentage
FROM (
         SELECT median_time::date                                         as daydate,
                height,
                (jsonb_array_elements(output_script_types) ->> 0)::bigint as script_type,
                (jsonb_array_elements(output_script_types) ->> 2)::bigint as script_type_size,
                'output'::text                                            as side
         FROM bitcoin.blocks
         UNION ALL
         SELECT median_time::date                                        as daydate,
                height,
                (jsonb_array_elements(input_script_types) ->> 0)::bigint as script_type,
                (jsonb_array_elements(input_script_types) ->> 3)::bigint as script_type_size,
                'input'::text                                            as side
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
WHERE script_type in (2, 3)
GROUP BY b.daydate, heights, total_size, side, script_type
ORDER BY b.daydate desc;