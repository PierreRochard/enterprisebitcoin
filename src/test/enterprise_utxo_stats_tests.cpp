// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/amount.h>
#include <enterprise/options.h>
#include <enterprise/utxo_set_to_sql.h>

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(enterprise_utxo_stats_tests)

BOOST_AUTO_TEST_CASE(coin_age_weeks_matches_original_rounding)
{
    BOOST_CHECK_EQUAL(enterprise::CoinAgeWeeks(1008, 1008), 0);
    BOOST_CHECK_EQUAL(enterprise::CoinAgeWeeks(1008, 0), 1);
    BOOST_CHECK_EQUAL(enterprise::CoinAgeWeeks(504, 0), 1); // round(0.5) == 1
    BOOST_CHECK_EQUAL(enterprise::CoinAgeWeeks(4032, 0), 4);
    BOOST_CHECK_EQUAL(enterprise::CoinAgeWeeks(10, 20), 0);
}

BOOST_AUTO_TEST_CASE(value_magnitude_bounds_are_log10_buckets)
{
    const auto zero{enterprise::ValueMagnitudeBounds(0)};
    BOOST_CHECK_EQUAL(zero.first, 0);
    BOOST_CHECK_EQUAL(zero.second, 1);

    const auto one{enterprise::ValueMagnitudeBounds(1)};
    BOOST_CHECK_EQUAL(one.first, 1);
    BOOST_CHECK_EQUAL(one.second, 10);

    const auto ten{enterprise::ValueMagnitudeBounds(10)};
    BOOST_CHECK_EQUAL(ten.first, 10);
    BOOST_CHECK_EQUAL(ten.second, 100);

    const auto coin{enterprise::ValueMagnitudeBounds(COIN)};
    BOOST_CHECK_EQUAL(coin.first, 100'000'000);
    BOOST_CHECK_EQUAL(coin.second, 1'000'000'000);
}

BOOST_AUTO_TEST_CASE(percent_rounded_to_hundredths)
{
    BOOST_CHECK_EQUAL(enterprise::PercentRounded(0, 0), 0.0);
    BOOST_CHECK_EQUAL(enterprise::PercentRounded(1, 3), 33.33);
    BOOST_CHECK_EQUAL(enterprise::PercentRounded(50, 100), 50.0);
}

BOOST_AUTO_TEST_CASE(should_export_uses_4032_block_intervals)
{
    BOOST_CHECK(!enterprise::ShouldExportUtxoSet(-1, -1, false, false));
    BOOST_CHECK(enterprise::ShouldExportUtxoSet(964012, -1, false, false));
    BOOST_CHECK(enterprise::ShouldExportUtxoSet(964012, 354816, false, false));
    BOOST_CHECK(!enterprise::ShouldExportUtxoSet(964012, 963648, false, false));
    BOOST_CHECK(enterprise::ShouldExportUtxoSet(967680, 964012, false, false));
    BOOST_CHECK(enterprise::ShouldExportUtxoSet(967681, 964012, false, false));
    BOOST_CHECK(!enterprise::ShouldExportUtxoSet(967679, 964012, false, false));
    BOOST_CHECK(!enterprise::ShouldExportUtxoSet(964012, 354816, true, false));
    BOOST_CHECK(enterprise::ShouldExportUtxoSet(964012, 964012, true, true));
}

BOOST_AUTO_TEST_CASE(export_interval_matches_historical_series)
{
    BOOST_CHECK_EQUAL(UTXO_EXPORT_INTERVAL, 4032);
    BOOST_CHECK_EQUAL(354816 % UTXO_EXPORT_INTERVAL, 0);
    BOOST_CHECK_EQUAL(4032 * 239, 963648);
    BOOST_CHECK_EQUAL(4032 * 240, 967680);
}

BOOST_AUTO_TEST_SUITE_END()
