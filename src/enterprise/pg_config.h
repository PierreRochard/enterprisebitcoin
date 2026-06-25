#ifndef BITCOIN_ENTERPRISE_PG_CONFIG_H
#define BITCOIN_ENTERPRISE_PG_CONFIG_H

#pragma once

#include <common/args.h>
#include <enterprise/dotenv.h>
#include <util/fs.h>

#include <sstream>
#include <string>

namespace enterprise {

inline auto& ConfigurePgEnv()
{
    auto& dotenv = env;
    if (gArgs.IsArgSet("-enterpriseconfig")) {
        dotenv.config(fs::PathToString(AbsPathForConfigVal(gArgs, gArgs.GetPathArg("-enterpriseconfig"))), /*reset=*/true);
    } else {
        dotenv.config();
        dotenv.config(fs::PathToString(gArgs.GetDataDirNet() / "enterprise.env"));
    }
    return dotenv;
}

inline std::string PgConnectionString()
{
    auto& dotenv = ConfigurePgEnv();

    std::stringstream conn_stream;
    conn_stream << "dbname = "
                << dotenv["PGDB"]
                << " user = "
                << dotenv["PGUSER"]
                << " password = "
                << dotenv["PGPASSWORD"]
                << " hostaddr = "
                << dotenv["PGHOST"]
                << " port = "
                << dotenv["PGPORT"];
    return conn_stream.str();
}

} // namespace enterprise

#endif // BITCOIN_ENTERPRISE_PG_CONFIG_H
