#ifndef BITCOIN_ENTERPRISE_BLOCK_DELTA_H
#define BITCOIN_ENTERPRISE_BLOCK_DELTA_H

#include <primitives/block.h>
#include <script/verify_flags.h>
#include <serialize.h>
#include <uint256.h>
#include <undo.h>

#include <cstdint>
#include <string>

enum class EnterpriseSpoolEventType : uint8_t {
    CONNECT = 1,
    DISCONNECT = 2,
};

struct EnterpriseBlockDelta {
    static constexpr uint32_t CURRENT_VERSION{2};

    uint32_t version{CURRENT_VERSION};
    EnterpriseSpoolEventType event_type{EnterpriseSpoolEventType::CONNECT};
    uint256 block_hash{};
    int32_t height{-1};
    uint64_t script_flags{0};
    std::string source{"index"};
    CBlock block{};
    CBlockUndo undo{};

    SERIALIZE_METHODS(EnterpriseBlockDelta, obj)
    {
        uint8_t event_type{static_cast<uint8_t>(obj.event_type)};
        READWRITE(obj.version);
        READWRITE(event_type);
        READWRITE(obj.block_hash);
        READWRITE(obj.height);
        READWRITE(obj.script_flags);
        READWRITE(obj.source);
        READWRITE(TX_WITH_WITNESS(obj.block));
        READWRITE(obj.undo);
        SER_READ(obj, obj.event_type = static_cast<EnterpriseSpoolEventType>(event_type));
    }

    [[nodiscard]] script_verify_flags ScriptFlags() const
    {
        return script_verify_flags::from_int(script_flags);
    }
};

#endif // BITCOIN_ENTERPRISE_BLOCK_DELTA_H
