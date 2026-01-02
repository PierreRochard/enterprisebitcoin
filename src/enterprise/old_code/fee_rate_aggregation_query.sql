SELECT daydate,
       count(height)        as heights,
       sum(total_fees)      as total_fees,
       sum(total_weight)    as total_weight,
       fee_rate             as fee_rate,
       sum(fee_rate_weight) as fee_rate_weight

FROM (
         SELECT height,
                outputs_count,
                median_time::date                               as daydate,
                total_fees,
                total_weight,
                (jsonb_array_elements(fee_rates) ->> 0)::bigint as fee_rate,
                (jsonb_array_elements(fee_rates) ->> 1)::bigint as fee_rate_weight
         FROM bitcoin.blocks
         WHERE outputs_count > 1
           and total_fees > 0
         ORDER BY median_time DESC
     ) as b
WHERE fee_rate > 0
GROUP BY daydate, fee_rate;