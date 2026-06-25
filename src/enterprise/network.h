#ifndef BITCOIN_ENTERPRISE_NETWORK_H
#define BITCOIN_ENTERPRISE_NETWORK_H

#pragma once

#include <util/chaintype.h>

#include <cassert>
#include <string>

inline std::string EnterpriseChainToString(ChainType chain)
{
    switch (chain) {
    case ChainType::MAIN:
        return "mainnet";
    case ChainType::TESTNET:
        return "testnet";
    case ChainType::TESTNET4:
        return "testnet4";
    case ChainType::SIGNET:
        return "signet";
    case ChainType::REGTEST:
        return "regtest";
    }
    assert(false);
    return {};
}

#endif // BITCOIN_ENTERPRISE_NETWORK_H
