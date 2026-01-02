SELECT b.daydate,
       count(height) as heights,
       output_script_type,
       total_outputs,
       sum(output_script_type_count) as output_script_type_count,
       (sum(output_script_type_count)::float / total_outputs::float * 100)::int as percentage
FROM (
         SELECT height,
                outputs_count,
                median_time::date                                         as daydate,
                total_fees,
                total_weight,
                (jsonb_array_elements(output_script_types) ->> 0)::bigint as output_script_type,
                (jsonb_array_elements(output_script_types) ->> 1)::bigint as output_script_type_count
         FROM bitcoin.blocks
         ORDER BY median_time DESC) as b
LEFT JOIN (
    SELECT
        median_time::date                                         as daydate,
           sum(outputs_count) as total_outputs
    FROM bitcoin.blocks
    GROUP BY median_time::date

    ) c ON b.daydate = c.daydate
GROUP BY b.daydate, total_outputs, output_script_type
ORDER BY b.daydate desc;