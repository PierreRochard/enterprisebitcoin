// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <enterprise/change_flows.h>

#include <primitives/transaction.h>
#include <script/script.h>

#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <vector>

namespace {

constexpr int HEIGHT{900'000};

CScript Script(unsigned char fill)
{
    return CScript() << OP_DUP << OP_HASH160 << std::vector<unsigned char>(20, fill) << OP_EQUALVERIFY << OP_CHECKSIG;
}

enterprise::SpentCoin Coin(CAmount value, unsigned char fill, int created_height = HEIGHT - 10)
{
    return {CTxOut{value, Script(fill)}, created_height};
}

CTransaction Spend(const std::vector<enterprise::SpentCoin>& spent, const std::vector<std::pair<CAmount, unsigned char>>& outputs)
{
    CMutableTransaction tx;
    for (std::size_t i = 0; i < spent.size(); ++i) {
        tx.vin.emplace_back(COutPoint{Txid::FromUint256(uint256::ONE), static_cast<uint32_t>(i)});
    }
    for (const auto& [value, fill] : outputs) {
        tx.vout.emplace_back(value, Script(fill));
    }
    return CTransaction{tx};
}

enterprise::ChangeFlowStats Stats(const std::vector<enterprise::SpentCoin>& spent,
                                  const std::vector<std::pair<CAmount, unsigned char>>& outputs)
{
    enterprise::ChangeFlowStats stats;
    stats.AddTransaction(Spend(spent, outputs), spent, HEIGHT);
    return stats;
}

} // namespace

BOOST_AUTO_TEST_SUITE(enterprise_change_flow_tests)

BOOST_AUTO_TEST_CASE(self_change_counts_outputs_paying_a_spent_script)
{
    const auto stats{Stats({Coin(5 * COIN, 'a'), Coin(4 * COIN, 'b')}, {{5 * COIN, 'a'}, {3 * COIN, 'c'}, {COIN, 'b'}})};
    BOOST_CHECK_EQUAL(stats.SelfChangeOutputs(), 2);
    BOOST_CHECK_EQUAL(stats.SelfChangeValue(), 6 * COIN);
    BOOST_CHECK_EQUAL(stats.ForwardOutputs(), 0);
}

BOOST_AUTO_TEST_CASE(coinbase_counts_nothing)
{
    CMutableTransaction coinbase;
    coinbase.vin.emplace_back(COutPoint{});
    coinbase.vout.emplace_back(50 * COIN, Script('a'));
    enterprise::ChangeFlowStats stats;
    stats.AddTransaction(CTransaction{coinbase}, {Coin(50 * COIN, 'a')}, HEIGHT);
    BOOST_CHECK_EQUAL(stats.SelfChangeOutputs(), 0);
    BOOST_CHECK_EQUAL(stats.ForwardOutputs(), 0);
}

BOOST_AUTO_TEST_CASE(forward_keeps_a_fresh_coin_moving_to_a_new_script)
{
    const auto stats{Stats({Coin(200 * COIN, 'a')}, {{COIN, 'c'}, {199 * COIN, 'b'}})};
    BOOST_CHECK_EQUAL(stats.ForwardOutputs(), 1);
    BOOST_CHECK_EQUAL(stats.ForwardValue(), 199 * COIN);
    BOOST_CHECK_EQUAL(stats.SelfChangeOutputs(), 0);
}

BOOST_AUTO_TEST_CASE(forward_thresholds_are_inclusive_of_the_edges)
{
    // 99% kept, 100 BTC, 143 blocks old: all at the edge, all counted.
    BOOST_CHECK_EQUAL(Stats({Coin(200 * COIN, 'a')}, {{198 * COIN, 'b'}, {2 * COIN, 'c'}}).ForwardOutputs(), 1);
    BOOST_CHECK_EQUAL(Stats({Coin(100 * COIN, 'a')}, {{100 * COIN - 1000, 'b'}, {1000, 'c'}}).ForwardOutputs(), 1);
    BOOST_CHECK_EQUAL(Stats({Coin(200 * COIN, 'a', HEIGHT - 143)}, {{199 * COIN, 'b'}, {COIN, 'c'}}).ForwardOutputs(), 1);

    // Just past each edge.
    BOOST_CHECK_EQUAL(Stats({Coin(200 * COIN, 'a')}, {{198 * COIN - 1, 'b'}, {2 * COIN + 1, 'c'}}).ForwardOutputs(), 0);
    BOOST_CHECK_EQUAL(Stats({Coin(100 * COIN - 1, 'a')}, {{100 * COIN - 1001, 'b'}, {1000, 'c'}}).ForwardOutputs(), 0);
    BOOST_CHECK_EQUAL(Stats({Coin(200 * COIN, 'a', HEIGHT - 144)}, {{199 * COIN, 'b'}, {COIN, 'c'}}).ForwardOutputs(), 0);
}

BOOST_AUTO_TEST_CASE(forward_needs_one_input_and_two_outputs)
{
    BOOST_CHECK_EQUAL(Stats({Coin(200 * COIN, 'a')}, {{200 * COIN, 'b'}}).ForwardOutputs(), 0);
    BOOST_CHECK_EQUAL(Stats({Coin(200 * COIN, 'a')}, {{199 * COIN, 'b'}, {COIN / 2, 'c'}, {COIN / 2, 'd'}}).ForwardOutputs(), 0);
    BOOST_CHECK_EQUAL(Stats({Coin(200 * COIN, 'a'), Coin(COIN, 'e')}, {{200 * COIN, 'b'}, {COIN, 'c'}}).ForwardOutputs(), 0);
}

BOOST_AUTO_TEST_CASE(a_kept_output_to_the_same_script_is_self_change_not_a_forward)
{
    const auto stats{Stats({Coin(200 * COIN, 'a')}, {{199 * COIN, 'a'}, {COIN, 'c'}})};
    BOOST_CHECK_EQUAL(stats.ForwardOutputs(), 0);
    BOOST_CHECK_EQUAL(stats.SelfChangeOutputs(), 1);
    BOOST_CHECK_EQUAL(stats.SelfChangeValue(), 199 * COIN);
}

BOOST_AUTO_TEST_CASE(accumulates_across_transactions)
{
    enterprise::ChangeFlowStats stats;
    const std::vector<enterprise::SpentCoin> first{Coin(200 * COIN, 'a')};
    const std::vector<enterprise::SpentCoin> second{Coin(5 * COIN, 'b')};
    stats.AddTransaction(Spend(first, {{199 * COIN, 'b'}, {COIN, 'c'}}), first, HEIGHT);
    stats.AddTransaction(Spend(second, {{4 * COIN, 'b'}, {COIN, 'd'}}), second, HEIGHT);
    BOOST_CHECK_EQUAL(stats.ForwardOutputs(), 1);
    BOOST_CHECK_EQUAL(stats.ForwardValue(), 199 * COIN);
    BOOST_CHECK_EQUAL(stats.SelfChangeOutputs(), 1);
    BOOST_CHECK_EQUAL(stats.SelfChangeValue(), 4 * COIN);
}

BOOST_AUTO_TEST_SUITE_END()
