#ifndef BITCOIN_ENTERPRISE_PG_CONFIG_H
#define BITCOIN_ENTERPRISE_PG_CONFIG_H

#pragma once

#include <common/args.h>
#include <util/fs.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace enterprise {

inline std::string QuotePgConnectionValue(std::string_view value)
{
    std::string quoted{"'"};
    quoted.reserve(value.size() + 2);
    for (const char ch : value) {
        // libpq conninfo values use a backslash to quote both backslashes and
        // single quotes inside a single-quoted value.
        if (ch == '\\' || ch == '\'') quoted.push_back('\\');
        quoted.push_back(ch);
    }
    quoted.push_back('\'');
    return quoted;
}

inline void SanitizePgRoutingEnvironment()
{
    // These libpq fallbacks can redirect or supplement an otherwise explicit
    // conninfo string. Clear them once during early node initialization, before
    // any enterprise connection or enterprise worker thread exists.
    static constexpr std::array<const char*, 3> ROUTING_VARIABLES{
        "PGHOSTADDR", "PGSERVICE", "PGSERVICEFILE"};
    for (const char* name : ROUTING_VARIABLES) {
#ifdef _WIN32
        if (_putenv_s(name, "") != 0) {
#else
        if (unsetenv(name) != 0) {
#endif
            throw std::runtime_error("failed to clear ambient libpq routing variable " + std::string{name});
        }
    }
}

inline std::string PgHostAddress(std::string_view host)
{
    if (host == "127.0.0.1" || host == "::1") return std::string{host};
    if (host == "localhost") return "127.0.0.1";
    throw std::runtime_error("enterprise PostgreSQL host must be loopback (127.0.0.1, ::1, or localhost)");
}

inline std::map<std::string, std::string> ParsePgEnvFile(const fs::path& path)
{
    static constexpr std::array<std::string_view, 5> ALLOWED_KEYS{
        "PGDB", "PGUSER", "PGPASSWORD", "PGHOST", "PGPORT"};
    std::ifstream file{path.std_path()};
    if (!file.is_open()) return {};

    std::map<std::string, std::string> values;
    std::string line;
    std::size_t line_number{0};
    while (std::getline(file, line)) {
        ++line_number;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line.starts_with('#')) continue;

        const std::size_t equals{line.find('=')};
        if (equals == std::string::npos || equals == 0) {
            throw std::runtime_error(
                "malformed PostgreSQL environment line " + std::to_string(line_number) + " in " + fs::PathToString(path));
        }
        const std::string key{line.substr(0, equals)};
        const std::string value{line.substr(equals + 1)};
        if (std::find(ALLOWED_KEYS.begin(), ALLOWED_KEYS.end(), key) == ALLOWED_KEYS.end()) {
            throw std::runtime_error("unknown PostgreSQL environment key " + key + " in " + fs::PathToString(path));
        }
        if (value.empty() || std::any_of(value.begin(), value.end(), [](unsigned char ch) {
                return std::isspace(ch) || ch == '#' || ch == '\'' || ch == '"';
            })) {
            throw std::runtime_error(
                "PostgreSQL environment value for " + key + " must be nonempty and contain no whitespace, comments, or quotes");
        }
        if (!values.emplace(key, value).second) {
            throw std::runtime_error("duplicate PostgreSQL environment key " + key + " in " + fs::PathToString(path));
        }
    }
    return values;
}

inline std::string BuildPgConnectionString(const std::map<std::string, std::string>& values)
{
    std::stringstream conn_stream;
    conn_stream << "dbname=" << QuotePgConnectionValue(values.at("PGDB"))
                << " user=" << QuotePgConnectionValue(values.at("PGUSER"))
                << " password=" << QuotePgConnectionValue(values.at("PGPASSWORD"))
                << " host=" << QuotePgConnectionValue(values.at("PGHOST"))
                // Pin the socket destination independently of DNS and of any
                // ambient libpq hostaddr default.
                << " hostaddr=" << QuotePgConnectionValue(PgHostAddress(values.at("PGHOST")))
                << " port=" << QuotePgConnectionValue(values.at("PGPORT"))
                << " application_name=" << QuotePgConnectionValue("enterprise-bitcoind")
                << " connect_timeout=" << QuotePgConnectionValue("10")
                << " options=" << QuotePgConnectionValue(
                       "-c synchronous_commit=on"
                       " -c search_path=pg_catalog,public"
                       " -c lock_timeout=5s"
                       " -c statement_timeout=5min"
                       " -c idle_in_transaction_session_timeout=60s");
    return conn_stream.str();
}

inline std::string LoadPgConnectionString()
{
    std::map<std::string, std::string> values;
    const bool explicit_config{gArgs.IsArgSet("-enterpriseconfig")};
    if (explicit_config) {
        values = ParsePgEnvFile(AbsPathForConfigVal(gArgs, gArgs.GetPathArg("-enterpriseconfig")));
    } else {
        values = ParsePgEnvFile(fs::path{".env"});
        for (auto& [key, value] : ParsePgEnvFile(gArgs.GetDataDirNet() / "enterprise.env")) {
            values[key] = std::move(value);
        }
    }
    for (const std::string_view key : {"PGDB", "PGUSER", "PGPASSWORD", "PGHOST", "PGPORT"}) {
        if (!explicit_config) {
            const char* environment_value{std::getenv(std::string{key}.c_str())};
            if (environment_value != nullptr) {
                values[std::string{key}] = environment_value;
            }
        }
        if (!values.contains(std::string{key}) || values.at(std::string{key}).empty()) {
            throw std::runtime_error("required PostgreSQL environment key " + std::string{key} + " is missing or empty");
        }
    }
    return BuildPgConnectionString(values);
}

inline std::string PgConnectionString()
{
    // PostgreSQL calls originate from both the async block writer and mempool
    // notification threads. Resolve file/environment settings once, after
    // argument parsing, then publish only the immutable connection string.
    // std::call_once retries if loading throws, without exposing a partial
    // value to another thread.
    static std::once_flag load_once;
    static std::string connection_string;
    std::call_once(load_once, [] { connection_string = LoadPgConnectionString(); });
    return connection_string;
}

} // namespace enterprise

#endif // BITCOIN_ENTERPRISE_PG_CONFIG_H
