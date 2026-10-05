// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <enterprise/spent_age.h>

#include <boost/test/unit_test.hpp>

#include <cstdint>

namespace {

constexpr int64_t DAY{24 * 60 * 60};

} // namespace

BOOST_AUTO_TEST_SUITE(enterprise_spent_age_tests)

BOOST_AUTO_TEST_CASE(band_lower_edges_are_inclusive)
{
    BOOST_CHECK_EQUAL(enterprise::SpentAgeBand(0), 0U);
    BOOST_CHECK_EQUAL(enterprise::SpentAgeBand(DAY - 1), 0U);
    BOOST_CHECK_EQUAL(enterprise::SpentAgeBand(DAY), 1U);
    BOOST_CHECK_EQUAL(enterprise::SpentAgeBand(155 * DAY - 1), 4U);
    BOOST_CHECK_EQUAL(enterprise::SpentAgeBand(155 * DAY), 5U);
    BOOST_CHECK_EQUAL(enterprise::SpentAgeBand(365 * DAY), 7U);
    BOOST_CHECK_EQUAL(enterprise::SpentAgeBand(3650 * DAY - 1), 11U);
    BOOST_CHECK_EQUAL(enterprise::SpentAgeBand(3650 * DAY), 12U);
    BOOST_CHECK_EQUAL(enterprise::SpentAgeBand(17 * 365 * DAY), 13U);
}

BOOST_AUTO_TEST_CASE(negative_age_counts_as_zero)
{
    BOOST_CHECK_EQUAL(enterprise::SpentAgeBand(-DAY), 0U);

    enterprise::SpentAgeStats stats;
    stats.Add(COIN, 100, -DAY);
    BOOST_CHECK_EQUAL(stats.CoinDaysDestroyed(), 0.0);
    BOOST_CHECK_EQUAL(stats.BandsJson(), "[[0,1,100000000]]");
}

BOOST_AUTO_TEST_CASE(empty_block_spends_nothing)
{
    const enterprise::SpentAgeStats stats;
    BOOST_CHECK_EQUAL(stats.CoinDaysDestroyed(), 0.0);
    BOOST_CHECK(!stats.OldestSpentHeight());
    BOOST_CHECK_EQUAL(stats.BandsJson(), "[]");
}

BOOST_AUTO_TEST_CASE(accumulates_coin_days_bands_and_oldest_height)
{
    enterprise::SpentAgeStats stats;
    stats.Add(2 * COIN, 900'000, 10 * DAY);      // 20 coin-days, 7-day band
    stats.Add(COIN / 2, 52'000, 6000 * DAY);     // 3,000 coin-days, 5475-day band
    stats.Add(COIN / 4, 899'000, 12 * DAY);      // 3 coin-days, 7-day band
    stats.Add(1'000, 960'000, 0);                // same-block spend

    BOOST_CHECK_CLOSE(stats.CoinDaysDestroyed(), 3023.0, 1e-9);
    BOOST_REQUIRE(stats.OldestSpentHeight());
    BOOST_CHECK_EQUAL(*stats.OldestSpentHeight(), 52'000);
    BOOST_CHECK_EQUAL(stats.BandsJson(), "[[0,1,1000],[7,2,225000000],[5475,1,50000000]]");
}

BOOST_AUTO_TEST_SUITE_END()
