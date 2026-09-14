// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <enterprise/pg_config.h>

#include <boost/test/unit_test.hpp>

#include <map>
#include <stdexcept>
#include <string>

BOOST_AUTO_TEST_SUITE(enterprise_pg_config_tests)

BOOST_AUTO_TEST_CASE(connection_string_pins_libpq_routing)
{
    const std::map<std::string, std::string> values{
        {"PGDB", "enterprisebitcoin_test_config"},
        {"PGUSER", "postgres"},
        {"PGPASSWORD", "test-password"},
        {"PGHOST", "127.0.0.1"},
        {"PGPORT", "55418"},
    };

    const std::string connection{enterprise::BuildPgConnectionString(values)};
    BOOST_CHECK_NE(connection.find(" host='/tmp'"), std::string::npos);
    BOOST_CHECK_EQUAL(connection.find(" hostaddr="), std::string::npos);
    BOOST_CHECK_EQUAL(connection.find(" service="), std::string::npos);
    BOOST_CHECK_NE(connection.find(" gssencmode='disable'"), std::string::npos);
    BOOST_CHECK_NE(connection.find(" sslmode='disable'"), std::string::npos);
    BOOST_CHECK_NE(connection.find(" options='-c synchronous_commit=on"), std::string::npos);

    auto localhost_values{values};
    localhost_values["PGHOST"] = "localhost";
    const std::string localhost_connection{enterprise::BuildPgConnectionString(localhost_values)};
    BOOST_CHECK_NE(localhost_connection.find(" host='/tmp'"), std::string::npos);
    BOOST_CHECK_EQUAL(localhost_connection.find(" hostaddr="), std::string::npos);

    auto socket_values{values};
    socket_values["PGHOST"] = "/var/run/postgresql";
    const std::string socket_connection{enterprise::BuildPgConnectionString(socket_values)};
    BOOST_CHECK_NE(socket_connection.find(" host='/var/run/postgresql'"), std::string::npos);

    auto remote_values{values};
    remote_values["PGHOST"] = "database.example.com";
    BOOST_CHECK_THROW(enterprise::BuildPgConnectionString(remote_values), std::runtime_error);
}

BOOST_AUTO_TEST_SUITE_END()
