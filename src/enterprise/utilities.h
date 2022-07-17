#ifndef ENTERPRISE_BITCOIN_UTILITIES_H
#define ENTERPRISE_BITCOIN_UTILITIES_H

#include <base58.h>

#include <bech32.h>
#include <hash.h>
#include <txmempool.h>
#include <script/script.h>
#include <uint256.h>
#include <util/strencodings.h>

#include <boost/variant/apply_visitor.hpp>
#include <boost/variant/static_visitor.hpp>

#include <algorithm>
#include <assert.h>
#include <string.h>

unsigned int GetTxnOutputTypeEnum(TxoutType t) {
    switch (t) {
        case TxoutType::NONSTANDARD:
            return 1;
        case TxoutType::PUBKEY:
            return 2;
        case TxoutType::PUBKEYHASH:
            return 3;
        case TxoutType::SCRIPTHASH:
            return 4;
        case TxoutType::MULTISIG:
            return 5;
        case TxoutType::NULL_DATA:
            return 6;
        case TxoutType::WITNESS_V0_KEYHASH:
            return 7;
        case TxoutType::WITNESS_V0_SCRIPTHASH:
            return 8;
        case TxoutType::WITNESS_V1_TAPROOT:
            return 9;
        case TxoutType::WITNESS_UNKNOWN:
            return 10;
    } // no default case, so the compiler can warn about missing cases
    assert(false);
}

unsigned int GetMemPoolRemovalReasonEnum(MemPoolRemovalReason r) {
    switch (r) {
        case MemPoolRemovalReason::EXPIRY:
            return 1;
        case MemPoolRemovalReason::SIZELIMIT:
            return 2;
        case MemPoolRemovalReason::REORG:
            return 3;
        case MemPoolRemovalReason::BLOCK:
            return 4;
        case MemPoolRemovalReason::CONFLICT:
            return 5;
        case MemPoolRemovalReason::REPLACED:
            return 6;
    } // no default case, so the compiler can warn about missing cases
    assert(false);
}

struct EnterpriseDestination {
    std::string address;
    std::string source_type;
    std::string hex;
    unsigned int version;
    unsigned int length;
    std::string program;
};

class EnterpriseDestinationEncoder : public boost::static_visitor<EnterpriseDestination> {
private:
    const CChainParams &m_params;

public:
    EnterpriseDestinationEncoder(const CChainParams &params) : m_params(params) {}

    EnterpriseDestination operator()(const CKeyID &id) const {
        EnterpriseDestination destination;
        destination.source_type = "CKeyID";
        destination.hex = id.ToString();

        std::vector<unsigned char> data = m_params.Base58Prefix(CChainParams::PUBKEY_ADDRESS);
        data.insert(data.end(), id.begin(), id.end());

        destination.address = EncodeBase58Check(data);
        return destination;
    }

    EnterpriseDestination operator()(const CScriptID &id) const {
        EnterpriseDestination destination;
        destination.source_type = "CScriptID";
        destination.hex = id.ToString();

        std::vector<unsigned char> data = m_params.Base58Prefix(CChainParams::SCRIPT_ADDRESS);
        data.insert(data.end(), id.begin(), id.end());
        destination.address = EncodeBase58Check(data);

        return destination;;
    }

    EnterpriseDestination operator()(const WitnessV0KeyHash &id) const {
        EnterpriseDestination destination;
        destination.source_type = "WitnessV0KeyHash";
        destination.hex = id.ToString();

        std::vector<unsigned char> data = {0};
        ConvertBits<8, 5, true>([&](unsigned char c) { data.push_back(c); }, id.begin(), id.end());
        destination.address = bech32::Encode(bech32::Encoding::BECH32, m_params.Bech32HRP(), data);

        return destination;
    }

    EnterpriseDestination operator()(const WitnessV0ScriptHash &id) const {
        EnterpriseDestination destination;
        destination.source_type = "WitnessV0ScriptHash";
        destination.hex = id.ToString();

        std::vector<unsigned char> data = {0};
        ConvertBits<8, 5, true>([&](unsigned char c) { data.push_back(c); }, id.begin(), id.end());
        destination.address = bech32::Encode(bech32::Encoding::BECH32, m_params.Bech32HRP(), data);

        return destination;
    }

    EnterpriseDestination operator()(const WitnessUnknown &id) const {
        EnterpriseDestination destination;
        destination.source_type = "WitnessUnknown";
        destination.version = id.version;
        destination.length = id.length;
        std::string str_program(reinterpret_cast<char *>(const_cast<unsigned char *>(id.program)));
        destination.program = str_program;

        if (id.version < 1 || id.version > 16 || id.length < 2 || id.length > 40) {
            return destination;
        }

        std::vector<unsigned char> data = {(unsigned char) id.version};
        ConvertBits<8, 5, true>([&](unsigned char c) { data.push_back(c); }, id.program, id.program + id.length);
        destination.address = bech32::Encode(bech32::Encoding::BECH32, m_params.Bech32HRP(), data);

        return destination;
    }

    EnterpriseDestination operator()(const CNoDestination &no) const {
        EnterpriseDestination destination;
        destination.source_type = "CNoDestination";
        return destination;
    }
};

#endif // ENTERPRISE_BITCOIN_UTILITIES_H