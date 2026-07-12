// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <enterprise/block_spool.h>
#include <enterprise/options.h>
#include <node/blockmanager_args.h>
#include <node/blockstorage.h>
#include <node/kernel_notifications.h>
#include <test/util/setup_common.h>
#include <uint256.h>
#include <util/byte_units.h>
#include <util/fs.h>

#include <boost/test/unit_test.hpp>

#include <chainparams.h>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace {
EnterpriseBlockDelta MakeDelta(EnterpriseSpoolEventType event_type, int32_t height, uint8_t hash, std::string source)
{
    EnterpriseBlockDelta delta;
    delta.event_type = event_type;
    delta.block_hash = uint256{hash};
    delta.height = height;
    delta.script_flags = 1000 + height;
    delta.source = std::move(source);
    delta.block.nVersion = height;
    return delta;
}

void CheckDelta(const EnterpriseBlockDelta& actual, const EnterpriseBlockDelta& expected)
{
    BOOST_CHECK(actual.event_type == expected.event_type);
    BOOST_CHECK(actual.block_hash == expected.block_hash);
    BOOST_CHECK_EQUAL(actual.height, expected.height);
    BOOST_CHECK_EQUAL(actual.script_flags, expected.script_flags);
    BOOST_CHECK_EQUAL(actual.source, expected.source);
    BOOST_CHECK_EQUAL(actual.block.nVersion, expected.block.nVersion);
}
} // namespace

BOOST_FIXTURE_TEST_SUITE(enterprise_block_spool_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(capture_order_restarts)
{
    // Keep this path short enough for the temporary suffix and spool filename
    // to stay below MAX_PATH on Windows.
    const fs::path spool_dir{m_args.GetDataDirBase() / "spool"};

    const std::vector<EnterpriseBlockDelta> expected{
        MakeDelta(EnterpriseSpoolEventType::CONNECT, 20, 2, "index"),
        MakeDelta(EnterpriseSpoolEventType::DISCONNECT, 5, 1, "reorg"),
        MakeDelta(EnterpriseSpoolEventType::CONNECT, 20, 3, "local"),
    };

    {
        EnterpriseBlockSpool spool{spool_dir};
        BOOST_REQUIRE(spool.Write(expected.at(0)));
        BOOST_REQUIRE(spool.Write(expected.at(1)));
    }
    {
        EnterpriseBlockSpool spool{spool_dir};
        BOOST_REQUIRE(spool.Write(expected.at(2)));

        const std::vector<fs::path> pending{spool.Pending()};
        BOOST_REQUIRE_EQUAL(pending.size(), expected.size());
        BOOST_CHECK_GT(spool.UsageBytes(), uint64_t{0});
        BOOST_CHECK_EQUAL(fs::PathToString(pending.at(0).filename()).substr(0, 21), "00000000000000000000-");
        BOOST_CHECK_EQUAL(fs::PathToString(pending.at(1).filename()).substr(0, 21), "00000000000000000001-");
        BOOST_CHECK_EQUAL(fs::PathToString(pending.at(2).filename()).substr(0, 21), "00000000000000000002-");

        for (std::size_t i{0}; i < expected.size(); ++i) {
            EnterpriseBlockDelta actual;
            BOOST_REQUIRE(spool.Read(pending.at(i), actual));
            CheckDelta(actual, expected.at(i));
        }

        BOOST_REQUIRE(spool.RemoveMany(pending));
        BOOST_CHECK_EQUAL(spool.UsageBytes(), uint64_t{0});
    }
}

BOOST_AUTO_TEST_CASE(block_spool_parses_file_metadata)
{
    const uint256 block_hash{uint256::FromHex("00000000000000000004240dd237ccbdfb7aa4a4670af8f537fb9bd12ae1590a").value()};
    const fs::path path{EnterpriseBlockSpool::FileNameFor(42, EnterpriseSpoolEventType::CONNECT, 201234, block_hash)};

    const auto info{EnterpriseBlockSpool::InfoFromFileName(path)};
    BOOST_REQUIRE(info);
    BOOST_CHECK_EQUAL(info->sequence, uint64_t{42});
    BOOST_CHECK(info->event_type == EnterpriseSpoolEventType::CONNECT);
    BOOST_CHECK_EQUAL(info->height, 201234);
    BOOST_CHECK(info->block_hash == block_hash);
    BOOST_CHECK(!EnterpriseBlockSpool::InfoFromFileName(fs::u8path("not-a-spool-file.tmp")));
}

BOOST_AUTO_TEST_CASE(enterprise_index_defaults_to_20gb_prune_target)
{
    ArgsManager args;
    args.ForceSetArg("-enterpriseindex", "1");

    const auto params{CreateChainParams(ArgsManager{}, ChainType::MAIN)};
    node::KernelNotifications notifications{Assert(m_node.shutdown_request), m_node.exit_status, *Assert(m_node.warnings)};
    auto make_options = [&]() {
        return node::BlockManager::Options{
            .chainparams = *params,
            .blocks_dir = m_args.GetBlocksDirPath(),
            .notifications = notifications,
            .block_tree_db_params = DBParams{
                .path = m_args.GetDataDirNet() / "blocks" / "index",
                .cache_bytes = 0,
            },
        };
    };

    auto enterprise_default_options{make_options()};
    BOOST_REQUIRE(node::ApplyArgsManOptions(args, enterprise_default_options));
    BOOST_CHECK_EQUAL(enterprise_default_options.prune_target, uint64_t{DEFAULT_ENTERPRISE_PRUNE_TARGET_MIB} * 1_MiB);

    args.ForceSetArg("-prune", "550");
    auto explicit_prune_options{make_options()};
    BOOST_REQUIRE(node::ApplyArgsManOptions(args, explicit_prune_options));
    BOOST_CHECK_EQUAL(explicit_prune_options.prune_target, uint64_t{550} * 1_MiB);
}

BOOST_AUTO_TEST_SUITE_END()
