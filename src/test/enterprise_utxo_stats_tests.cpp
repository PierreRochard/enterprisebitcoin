// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/amount.h>
#include <enterprise/options.h>
#include <enterprise/utxo_set_to_sql.h>
#include <script/script.h>

#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <cstdint>

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
    BOOST_CHECK(!enterprise::ShouldExportUtxoSet(0, -1, false, false));
    BOOST_CHECK(enterprise::ShouldExportUtxoSet(964012, -1, false, false));
    BOOST_CHECK(enterprise::ShouldExportUtxoSet(964012, 354816, false, false));
    BOOST_CHECK(!enterprise::ShouldExportUtxoSet(964012, 963648, false, false));
    BOOST_CHECK_EQUAL(358848 % UTXO_EXPORT_INTERVAL, 0);
    BOOST_CHECK(enterprise::ShouldExportUtxoSet(967680, 964012, false, false));
    BOOST_CHECK(enterprise::ShouldExportUtxoSet(967681, 964012, false, false));
    BOOST_CHECK(!enterprise::ShouldExportUtxoSet(967679, 964012, false, false));
    BOOST_CHECK(!enterprise::ShouldExportUtxoSet(964012, 354816, true, false));
    BOOST_CHECK(enterprise::ShouldExportUtxoSet(964012, 964012, true, true));
    // An IBD node must still fill missing aligned heights behind a newer catch-up row.
    BOOST_CHECK(enterprise::ShouldExportUtxoSet(358848, 964014, false, false));
    BOOST_CHECK(!enterprise::ShouldExportUtxoSet(358848, 964014, true, false));
}

BOOST_AUTO_TEST_CASE(export_interval_matches_historical_series)
{
    BOOST_CHECK_EQUAL(UTXO_EXPORT_INTERVAL, 4032);
    BOOST_CHECK_EQUAL(354816 % UTXO_EXPORT_INTERVAL, 0);
    BOOST_CHECK_EQUAL(4032 * 239, 963648);
    BOOST_CHECK_EQUAL(4032 * 240, 967680);
}

BOOST_AUTO_TEST_CASE(address_bin_edges_are_increasing_with_exact_powers_of_ten)
{
    const auto& edges{enterprise::AddressBalanceBinEdges()};
    BOOST_CHECK_EQUAL(edges.front(), 1);
    BOOST_CHECK(std::adjacent_find(edges.begin(), edges.end(), std::greater_equal<CAmount>{}) == edges.end());
    CAmount power{1};
    for (int decade{0}; decade <= 16; ++decade, power *= 10) {
        BOOST_CHECK_MESSAGE(std::binary_search(edges.begin(), edges.end(), power), "missing edge " << power);
    }
    BOOST_CHECK(edges.back() > MAX_MONEY);
    // Above 100 satoshis every decade keeps all 100 bins.
    const auto count_between{[&](CAmount lo, CAmount hi) {
        return std::count_if(edges.begin(), edges.end(), [&](CAmount e) { return e >= lo && e < hi; });
    }};
    BOOST_CHECK_EQUAL(count_between(COIN, 10 * COIN), 100);
    BOOST_CHECK_EQUAL(count_between(1'000, 10'000), 100);
}

BOOST_AUTO_TEST_CASE(address_bin_index_brackets_balances)
{
    const auto& edges{enterprise::AddressBalanceBinEdges()};
    for (const CAmount balance : {CAmount{1}, CAmount{9}, CAmount{10}, CAmount{999'999}, CAmount{1'000'000}, COIN, COIN + 1, MAX_MONEY}) {
        const size_t i{enterprise::AddressBalanceBinIndex(balance)};
        BOOST_CHECK_LE(edges[i], balance);
        BOOST_CHECK_LT(balance, edges[i + 1]);
    }
    BOOST_CHECK_EQUAL(edges[enterprise::AddressBalanceBinIndex(1'000'000)], 1'000'000);
    BOOST_CHECK_EQUAL(edges[enterprise::AddressBalanceBinIndex(COIN)], COIN);
}

BOOST_AUTO_TEST_CASE(script_balance_map_sums_per_script_across_growth)
{
    enterprise::ScriptBalanceMap balances;
    const CScript a{CScript() << OP_TRUE};
    const CScript b{CScript() << OP_2};
    BOOST_CHECK(enterprise::ScriptBalanceMap::Key(a) != enterprise::ScriptBalanceMap::Key(b));
    BOOST_CHECK_EQUAL(enterprise::ScriptBalanceMap::Key(a), enterprise::ScriptBalanceMap::Key(CScript() << OP_TRUE));

    balances.Add(enterprise::ScriptBalanceMap::Key(a), 5 * COIN);
    // Enough distinct keys to force several rehashes.
    for (uint64_t k{1}; k <= 10'000; ++k) {
        balances.Add(k * 0x9E3779B97F4A7C15ULL, 1'000);
    }
    balances.Add(enterprise::ScriptBalanceMap::Key(a), 3 * COIN);
    balances.Add(enterprise::ScriptBalanceMap::Key(b), 2'000'000);
    BOOST_CHECK_EQUAL(balances.size(), 10'002U);

    const auto bins{enterprise::BuildAddressBalanceHistogram(balances)};
    uint64_t addresses{0};
    CAmount total{0};
    for (size_t i{0}; i < bins.size(); ++i) {
        BOOST_CHECK(bins[i].address_count > 0);
        if (i > 0) BOOST_CHECK(bins[i - 1].upper_bound <= bins[i].lower_bound);
        addresses += bins[i].address_count;
        total += bins[i].balance;
    }
    BOOST_CHECK_EQUAL(addresses, 10'002U);
    BOOST_CHECK_EQUAL(enterprise::CountAddresses(bins), 10'002U);

    // A script holding only zero-value outputs is tracked but is not an address with a balance.
    balances.Add(enterprise::ScriptBalanceMap::Key(CScript() << OP_3), 0);
    BOOST_CHECK_EQUAL(balances.size(), 10'003U);
    BOOST_CHECK_EQUAL(enterprise::CountAddresses(enterprise::BuildAddressBalanceHistogram(balances)), 10'002U);
    BOOST_CHECK_EQUAL(total, 8 * COIN + 2'000'000 + 10'000 * 1'000);

    const auto holding{[&](CAmount balance) {
        return std::find_if(bins.begin(), bins.end(), [&](const auto& bin) {
            return bin.lower_bound <= balance && balance < bin.upper_bound;
        });
    }};
    BOOST_CHECK_EQUAL(holding(8 * COIN)->address_count, 1U);
    BOOST_CHECK_EQUAL(holding(8 * COIN)->balance, 8 * COIN);
    BOOST_CHECK_EQUAL(holding(1'000)->address_count, 10'000U);
}

BOOST_AUTO_TEST_SUITE_END()
