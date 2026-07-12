// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <enterprise/denomination_classifier.h>

#include <boost/test/unit_test.hpp>

#include <optional>

namespace {

enterprise::DenominationPriceWindow ExactWindow(double price)
{
    return enterprise::DenominationPriceWindow{.price = price, .low = price, .high = price, .source = "test"};
}

enterprise::DenominationPriceWindow WindowForTarget(CAmount sats, double target_usd)
{
    const double price{target_usd * static_cast<double>(COIN) / static_cast<double>(sats)};
    return ExactWindow(price);
}

} // namespace

BOOST_AUTO_TEST_SUITE(enterprise_denomination_classifier_tests)

BOOST_AUTO_TEST_CASE(classifies_exact_usd_bill)
{
    const auto result{enterprise::ClassifyOutputDenomination(54'321, WindowForTarget(54'321, 20.00))};
    BOOST_CHECK(result.category == enterprise::DenominationCategory::USD);
    BOOST_CHECK_GE(result.usd_score, 0.99);
    BOOST_CHECK_EQUAL(result.confidence, result.usd_score);
}

BOOST_AUTO_TEST_CASE(classifies_retail_cent_amount)
{
    const auto result{enterprise::ClassifyOutputDenomination(47'035, WindowForTarget(47'035, 19.99))};
    BOOST_CHECK(result.category == enterprise::DenominationCategory::USD);
    BOOST_CHECK_GE(result.usd_score, 0.99);
}

BOOST_AUTO_TEST_CASE(classifies_clean_sats_amount)
{
    const auto result{enterprise::ClassifyOutputDenomination(1'000'000, ExactWindow(37'123.0))};
    BOOST_CHECK(result.category == enterprise::DenominationCategory::SATS);
    BOOST_CHECK_GE(result.sats_score, 0.99);
    BOOST_CHECK_EQUAL(result.confidence, result.sats_score);
}

BOOST_AUTO_TEST_CASE(classifies_ambiguous_usd_and_sats_signal)
{
    const auto result{enterprise::ClassifyOutputDenomination(100'000, ExactWindow(100'000.0))};
    BOOST_CHECK(result.category == enterprise::DenominationCategory::AMBIGUOUS);
    BOOST_CHECK_GE(result.usd_score, 0.99);
    BOOST_CHECK_GE(result.sats_score, 0.99);
    BOOST_CHECK_EQUAL(result.confidence, 0.0);
}

BOOST_AUTO_TEST_CASE(missing_price_window_is_unknown)
{
    const auto result{enterprise::ClassifyOutputDenomination(1'000'000, std::nullopt)};
    BOOST_CHECK(result.category == enterprise::DenominationCategory::UNKNOWN);
    BOOST_CHECK_EQUAL(result.usd_score, 0.0);
    BOOST_CHECK_EQUAL(result.sats_score, 0.0);
}

BOOST_AUTO_TEST_CASE(zero_price_window_is_unknown)
{
    const enterprise::DenominationPriceWindow zero_window{
        .price = 0.0,
        .low = 0.0,
        .high = 0.0,
        .source = "test-zero",
    };
    BOOST_CHECK(!zero_window.Valid());
    const auto result{enterprise::ClassifyOutputDenomination(1'000'000, zero_window)};
    BOOST_CHECK(result.category == enterprise::DenominationCategory::UNKNOWN);
    BOOST_CHECK_EQUAL(result.usd_score, 0.0);
    BOOST_CHECK_EQUAL(result.sats_score, 0.0);
}

BOOST_AUTO_TEST_CASE(applies_high_score_threshold)
{
    const auto result{enterprise::ClassifyOutputDenomination(123'000, ExactWindow(37'123.0))};
    BOOST_CHECK(result.category == enterprise::DenominationCategory::SATS);
    BOOST_CHECK_GE(result.sats_score, 0.70);
}

BOOST_AUTO_TEST_CASE(near_miss_remains_unknown)
{
    const auto result{enterprise::ClassifyOutputDenomination(54'321, ExactWindow(37'123.0))};
    BOOST_CHECK(result.category == enterprise::DenominationCategory::UNKNOWN);
    BOOST_CHECK_LT(result.usd_score, 0.70);
    BOOST_CHECK_LT(result.sats_score, 0.70);
}

BOOST_AUTO_TEST_SUITE_END()
