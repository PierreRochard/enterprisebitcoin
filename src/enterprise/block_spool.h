#ifndef BITCOIN_ENTERPRISE_BLOCK_SPOOL_H
#define BITCOIN_ENTERPRISE_BLOCK_SPOOL_H

#include <enterprise/block_delta.h>
#include <util/fs.h>

#include <cstdint>
#include <mutex>
#include <optional>
#include <vector>

struct EnterpriseBlockSpoolFileInfo {
    uint64_t sequence{0};
    EnterpriseSpoolEventType event_type{EnterpriseSpoolEventType::CONNECT};
    int32_t height{-1};
    uint256 block_hash{};
};

class EnterpriseBlockSpool
{
private:
    fs::path m_spool_dir;
    mutable std::mutex m_state_mutex;
    uint64_t m_next_sequence{0};
    mutable uint64_t m_usage_bytes{0};

    uint64_t NextSequence();
    void InitNextSequence();

public:
    explicit EnterpriseBlockSpool(fs::path spool_dir);

    [[nodiscard]] static fs::path FileNameFor(uint64_t sequence, EnterpriseSpoolEventType event_type, int32_t height, const uint256& block_hash);
    [[nodiscard]] static std::optional<EnterpriseBlockSpoolFileInfo> InfoFromFileName(const fs::path& path);
    [[nodiscard]] bool Write(const EnterpriseBlockDelta& delta);
    [[nodiscard]] std::vector<fs::path> Pending() const;
    [[nodiscard]] bool Read(const fs::path& path, EnterpriseBlockDelta& delta) const;
    [[nodiscard]] bool Remove(const fs::path& path) const;
    [[nodiscard]] bool RemoveMany(const std::vector<fs::path>& paths) const;
    [[nodiscard]] uint64_t UsageBytes() const;
};

#endif // BITCOIN_ENTERPRISE_BLOCK_SPOOL_H
