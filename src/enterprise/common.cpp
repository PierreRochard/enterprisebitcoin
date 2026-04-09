#include <enterprise/common.h>

#include <chainparams.h>
#include <common/args.h>

namespace enterprise {

std::string ChainName()
{
    switch (gArgs.GetChainType()) {
    case ChainType::TESTNET:
        return "testnet";
    case ChainType::TESTNET4:
        return "testnet4";
    case ChainType::SIGNET:
        return "signet";
    case ChainType::REGTEST:
        return "regtest";
    case ChainType::MAIN:
        return "mainnet";
    }
    return "unknown";
}

} // namespace enterprise
