#include <enterprise/block_spool.h>

#include <streams.h>
#include <tinyformat.h>
#include <util/check.h>
#include <util/fs_helpers.h>
#include <util/log.h>

#include <algorithm>
#include <charconv>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <exception>
#include <limits>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#ifdef HAVE_ENTERPRISE_ZLIB
#include <zlib.h>
#endif

namespace {
enum class EnterpriseSpoolEncoding : uint8_t {
    RAW = 0,
    ZLIB = 1,
};

struct EnterpriseSpoolDiskRecord {
    static constexpr uint32_t MAGIC{0x4553504c}; // ESPL
    static constexpr uint32_t CURRENT_VERSION{1};

    uint32_t magic{MAGIC};
    uint32_t version{CURRENT_VERSION};
    EnterpriseSpoolEncoding encoding{EnterpriseSpoolEncoding::RAW};
    uint64_t uncompressed_size{0};
    std::vector<unsigned char> payload;

    SERIALIZE_METHODS(EnterpriseSpoolDiskRecord, obj)
    {
        uint8_t encoding{static_cast<uint8_t>(obj.encoding)};
        READWRITE(obj.magic);
        READWRITE(obj.version);
        READWRITE(encoding);
        READWRITE(obj.uncompressed_size);
        READWRITE(obj.payload);
        SER_READ(obj, obj.encoding = static_cast<EnterpriseSpoolEncoding>(encoding));
    }
};

const char* EventSuffix(EnterpriseSpoolEventType event_type)
{
    switch (event_type) {
    case EnterpriseSpoolEventType::CONNECT:
        return "connect";
    case EnterpriseSpoolEventType::DISCONNECT:
        return "disconnect";
    }
    Assume(false);
    return "unknown";
}

std::optional<EnterpriseSpoolEventType> EventTypeFromSuffix(std::string_view suffix)
{
    if (suffix == "connect") return EnterpriseSpoolEventType::CONNECT;
    if (suffix == "disconnect") return EnterpriseSpoolEventType::DISCONNECT;
    return std::nullopt;
}

std::vector<unsigned char> DataStreamBytes(const DataStream& stream)
{
    std::vector<unsigned char> bytes(stream.size());
    if (!bytes.empty()) std::memcpy(bytes.data(), stream.data(), stream.size());
    return bytes;
}

std::optional<uint64_t> SequenceFromFileName(const fs::path& path)
{
    const auto info{EnterpriseBlockSpool::InfoFromFileName(path)};
    if (!info) return std::nullopt;
    return info->sequence;
}

EnterpriseSpoolDiskRecord MakeDiskRecord(const EnterpriseBlockDelta& delta)
{
    DataStream raw_stream;
    raw_stream << delta;

    EnterpriseSpoolDiskRecord record;
    record.uncompressed_size = raw_stream.size();
    record.payload = DataStreamBytes(raw_stream);

#ifdef HAVE_ENTERPRISE_ZLIB
    if (!record.payload.empty()) {
        uLongf compressed_size{compressBound(record.payload.size())};
        std::vector<unsigned char> compressed(compressed_size);
        const int rc{compress2(compressed.data(), &compressed_size, record.payload.data(), record.payload.size(), Z_BEST_SPEED)};
        if (rc == Z_OK && compressed_size < record.payload.size()) {
            compressed.resize(compressed_size);
            record.encoding = EnterpriseSpoolEncoding::ZLIB;
            record.payload = std::move(compressed);
        }
    }
#endif

    return record;
}

bool DecodeDiskRecord(const EnterpriseSpoolDiskRecord& record, EnterpriseBlockDelta& delta)
{
    if (record.magic != EnterpriseSpoolDiskRecord::MAGIC || record.version != EnterpriseSpoolDiskRecord::CURRENT_VERSION) {
        LogError("enterprise: unsupported block spool disk record magic=%u version=%u", record.magic, record.version);
        return false;
    }
    if (record.uncompressed_size > std::numeric_limits<std::size_t>::max()) {
        LogError("enterprise: block spool record too large: %u bytes", record.uncompressed_size);
        return false;
    }

    std::vector<unsigned char> payload;
    switch (record.encoding) {
    case EnterpriseSpoolEncoding::RAW:
        payload = record.payload;
        break;
    case EnterpriseSpoolEncoding::ZLIB:
#ifdef HAVE_ENTERPRISE_ZLIB
    {
        payload.resize(static_cast<std::size_t>(record.uncompressed_size));
        uLongf uncompressed_size{static_cast<uLongf>(payload.size())};
        const int rc{uncompress(payload.data(), &uncompressed_size, record.payload.data(), record.payload.size())};
        if (rc != Z_OK || uncompressed_size != payload.size()) {
            LogError("enterprise: failed to decompress block spool record");
            return false;
        }
        break;
    }
#else
        LogError("enterprise: zlib-compressed block spool record cannot be read by this build");
        return false;
#endif
    }

    if (payload.size() != record.uncompressed_size) {
        LogError("enterprise: block spool payload size mismatch: got %u expected %u", payload.size(), record.uncompressed_size);
        return false;
    }

    DataStream stream{std::span<const uint8_t>{payload.data(), payload.size()}};
    stream >> delta;
    return true;
}
} // namespace

EnterpriseBlockSpool::EnterpriseBlockSpool(fs::path spool_dir) : m_spool_dir{std::move(spool_dir)}
{
    fs::create_directories(m_spool_dir);
    InitNextSequence();
}

void EnterpriseBlockSpool::InitNextSequence()
{
    uint64_t max_sequence{0};
    uint64_t usage_bytes{0};
    bool found_sequence{false};
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator{m_spool_dir, ec}) {
        if (ec) {
            LogError("enterprise: failed to scan block spool directory %s: %s", fs::PathToString(m_spool_dir), ec.message());
            return;
        }
        if (!entry.is_regular_file(ec) || entry.path().extension() != ".ebd") continue;
        const auto file_size{entry.file_size(ec)};
        if (!ec) usage_bytes += file_size;
        ec.clear();
        const auto sequence{SequenceFromFileName(entry.path())};
        if (!sequence) continue;
        found_sequence = true;
        max_sequence = std::max(max_sequence, *sequence);
    }
    std::lock_guard<std::mutex> lock{m_state_mutex};
    m_next_sequence = found_sequence ? max_sequence + 1 : 0;
    m_usage_bytes = usage_bytes;
}

uint64_t EnterpriseBlockSpool::NextSequence()
{
    std::lock_guard<std::mutex> lock{m_state_mutex};
    return m_next_sequence++;
}

fs::path EnterpriseBlockSpool::FileNameFor(uint64_t sequence, EnterpriseSpoolEventType event_type, int32_t height, const uint256& block_hash)
{
    return fs::u8path(strprintf("%020u-%010d-%s-%s.ebd", sequence, height, block_hash.ToString(), EventSuffix(event_type)));
}

std::optional<EnterpriseBlockSpoolFileInfo> EnterpriseBlockSpool::InfoFromFileName(const fs::path& path)
{
    const std::string name{fs::PathToString(path.filename())};
    static constexpr std::string_view EXTENSION{".ebd"};
    static constexpr std::size_t SEQUENCE_WIDTH{20};
    static constexpr std::size_t HEIGHT_WIDTH{10};
    static constexpr std::size_t HASH_WIDTH{64};
    static constexpr std::size_t FIRST_DASH{SEQUENCE_WIDTH};
    static constexpr std::size_t HEIGHT_BEGIN{FIRST_DASH + 1};
    static constexpr std::size_t HEIGHT_END{HEIGHT_BEGIN + HEIGHT_WIDTH};
    static constexpr std::size_t HASH_BEGIN{HEIGHT_END + 1};
    static constexpr std::size_t HASH_END{HASH_BEGIN + HASH_WIDTH};
    static constexpr std::size_t EVENT_BEGIN{HASH_END + 1};

    if (name.size() <= EVENT_BEGIN + EXTENSION.size()) return std::nullopt;
    if (name.compare(name.size() - EXTENSION.size(), EXTENSION.size(), EXTENSION.data(), EXTENSION.size()) != 0) return std::nullopt;
    if (name.at(FIRST_DASH) != '-' || name.at(HEIGHT_END) != '-' || name.at(HASH_END) != '-') return std::nullopt;

    EnterpriseBlockSpoolFileInfo info;
    const char* sequence_begin{name.data()};
    const char* sequence_end{name.data() + SEQUENCE_WIDTH};
    const auto [sequence_ptr, sequence_ec]{std::from_chars(sequence_begin, sequence_end, info.sequence)};
    if (sequence_ec != std::errc{} || sequence_ptr != sequence_end) return std::nullopt;

    const char* height_begin{name.data() + HEIGHT_BEGIN};
    const char* height_end{name.data() + HEIGHT_END};
    const auto [height_ptr, height_ec]{std::from_chars(height_begin, height_end, info.height)};
    if (height_ec != std::errc{} || height_ptr != height_end) return std::nullopt;

    const auto hash{uint256::FromHex(std::string_view{name.data() + HASH_BEGIN, HASH_WIDTH})};
    if (!hash) return std::nullopt;
    info.block_hash = *hash;

    const std::string_view event_suffix{name.data() + EVENT_BEGIN, name.size() - EVENT_BEGIN - EXTENSION.size()};
    const auto event_type{EventTypeFromSuffix(event_suffix)};
    if (!event_type) return std::nullopt;
    info.event_type = *event_type;
    return info;
}

bool EnterpriseBlockSpool::Write(const EnterpriseBlockDelta& delta)
{
    try {
        fs::create_directories(m_spool_dir);
        const fs::path final_path{m_spool_dir / FileNameFor(NextSequence(), delta.event_type, delta.height, delta.block_hash)};
        const fs::path tmp_path{final_path + ".tmp"};

        AutoFile file{fsbridge::fopen(tmp_path, "wb")};
        if (file.IsNull()) {
            LogError("enterprise: failed to open block spool file %s", fs::PathToString(tmp_path));
            return false;
        }
        file << MakeDiskRecord(delta);
        if (!file.Commit()) {
            LogError("enterprise: failed to flush block spool file %s", fs::PathToString(tmp_path));
            return false;
        }
        if (file.fclose() != 0) {
            LogError("enterprise: failed to close block spool file %s", fs::PathToString(tmp_path));
            return false;
        }
        if (!RenameOver(tmp_path, final_path)) {
            LogError("enterprise: failed to atomically publish block spool file %s", fs::PathToString(final_path));
            return false;
        }
        std::error_code ec;
        const auto file_size{fs::file_size(final_path, ec)};
        if (!ec) {
            std::lock_guard<std::mutex> lock{m_state_mutex};
            m_usage_bytes += file_size;
        }
        DirectoryCommit(m_spool_dir);
        return true;
    } catch (const std::exception& e) {
        LogError("enterprise: failed to write block spool record for %s: %s", delta.block_hash.ToString(), e.what());
        return false;
    }
}

std::vector<fs::path> EnterpriseBlockSpool::Pending() const
{
    std::vector<fs::path> paths;
    std::error_code ec;
    if (!fs::exists(m_spool_dir)) return paths;

    for (const auto& entry : fs::directory_iterator{m_spool_dir, ec}) {
        if (ec) {
            LogError("enterprise: failed to scan block spool directory %s: %s", fs::PathToString(m_spool_dir), ec.message());
            return paths;
        }
        if (!entry.is_regular_file(ec)) continue;
        if (entry.path().extension() == ".ebd") paths.push_back(entry.path());
    }
    std::sort(paths.begin(), paths.end(), [](const fs::path& a, const fs::path& b) {
        const auto a_sequence{SequenceFromFileName(a)};
        const auto b_sequence{SequenceFromFileName(b)};
        if (a_sequence && b_sequence) return *a_sequence < *b_sequence;
        if (a_sequence || b_sequence) return !a_sequence;
        return fs::PathToString(a.filename()) < fs::PathToString(b.filename());
    });
    return paths;
}

bool EnterpriseBlockSpool::Read(const fs::path& path, EnterpriseBlockDelta& delta) const
{
    try {
        AutoFile file{fsbridge::fopen(path, "rb")};
        if (file.IsNull()) {
            LogError("enterprise: failed to open block spool file %s", fs::PathToString(path));
            return false;
        }
        EnterpriseSpoolDiskRecord record;
        file >> record;
        if (!DecodeDiskRecord(record, delta)) return false;
        if (delta.version != EnterpriseBlockDelta::CURRENT_VERSION) {
            LogError("enterprise: unsupported block spool version %u in %s", delta.version, fs::PathToString(path));
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        LogError("enterprise: failed to read block spool file %s: %s", fs::PathToString(path), e.what());
        return false;
    }
}

bool EnterpriseBlockSpool::Remove(const fs::path& path) const
{
    return RemoveMany({path});
}

bool EnterpriseBlockSpool::RemoveMany(const std::vector<fs::path>& paths) const
{
    std::error_code ec;
    bool removed_any{false};
    bool success{true};
    uint64_t removed_bytes{0};

    for (const fs::path& path : paths) {
        uint64_t file_size{0};
        const auto maybe_file_size{fs::file_size(path, ec)};
        if (ec) {
            ec.clear();
        } else {
            file_size = maybe_file_size;
        }
        const bool removed{fs::remove(path, ec)};
        if (ec) {
            LogError("enterprise: failed to remove block spool file %s: %s", fs::PathToString(path), ec.message());
            success = false;
            break;
        }
        if (removed) {
            removed_any = true;
            removed_bytes += file_size;
        }
    }

    if (removed_any) {
        std::lock_guard<std::mutex> lock{m_state_mutex};
        m_usage_bytes = removed_bytes > m_usage_bytes ? 0 : m_usage_bytes - removed_bytes;
        DirectoryCommit(m_spool_dir);
    }

    return success;
}

uint64_t EnterpriseBlockSpool::UsageBytes() const
{
    std::lock_guard<std::mutex> lock{m_state_mutex};
    return m_usage_bytes;
}
