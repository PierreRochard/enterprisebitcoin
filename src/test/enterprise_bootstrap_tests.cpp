// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <enterprise/pg.h>
#include <tinyformat.h>

#include <boost/test/unit_test.hpp>

#include <stdexcept>
#include <vector>

BOOST_AUTO_TEST_SUITE(enterprise_bootstrap_tests)

namespace {
uint256 Hash(unsigned int value)
{
    return *uint256::FromHex(strprintf("%064x", value));
}
} // namespace

BOOST_AUTO_TEST_CASE(verifies_contiguous_active_chain_rows)
{
    const std::vector<uint256> active_chain{Hash(1), Hash(2), Hash(3)};
    const std::vector<enterprise::BlockRowKey> rows{
        {0, active_chain[0]},
        {1, active_chain[1]},
        {2, active_chain[2]},
    };

    const enterprise::BlockRowKey tip{enterprise::VerifyContiguousBlockRows(
        rows,
        /*chain_tip_height=*/2,
        [&active_chain](int height) { return active_chain.at(height); })};

    BOOST_CHECK_EQUAL(tip.height, 2);
    BOOST_CHECK(tip.hash == active_chain[2]);
}

BOOST_AUTO_TEST_CASE(rejects_incomplete_or_mismatched_coverage)
{
    const std::vector<uint256> active_chain{Hash(1), Hash(2), Hash(3)};
    const auto active_hash{[&active_chain](int height) {
        return active_chain.at(height);
    }};
    const auto verify{[&active_hash](
                          const std::vector<enterprise::BlockRowKey>& rows,
                          int chain_tip_height) {
        (void)enterprise::VerifyContiguousBlockRows(
            rows,
            chain_tip_height,
            active_hash);
    }};

    BOOST_CHECK_THROW(
        verify({}, 2),
        std::runtime_error);
    BOOST_CHECK_THROW(
        verify(
            {{0, active_chain[0]}, {2, active_chain[2]}},
            2),
        std::runtime_error);
    BOOST_CHECK_THROW(
        verify(
            {{0, active_chain[0]}, {1, Hash(99)}},
            2),
        std::runtime_error);
    BOOST_CHECK_THROW(
        verify(
            {{0, active_chain[0]}, {1, active_chain[1]}, {1, active_chain[1]}},
            2),
        std::runtime_error);
    BOOST_CHECK_THROW(
        verify(
            {{1, active_chain[1]}},
            2),
        std::runtime_error);
    BOOST_CHECK_THROW(
        verify(
            {{0, active_chain[0]}, {1, active_chain[1]}, {2, active_chain[2]}},
            1),
        std::runtime_error);
}

BOOST_AUTO_TEST_SUITE_END()
