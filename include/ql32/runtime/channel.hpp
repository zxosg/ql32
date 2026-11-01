#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "ql32/runtime/storage.hpp"

namespace ql32::runtime {

using SessionId = std::uint64_t;
using ChannelId = std::uint64_t;

enum class ChannelMode { Read, Write, Append };

enum class ChannelType { Console, Window, File, Serial, Network, Null };

enum class ChannelDevice : std::uint64_t { File = 1, Serial = 2, Null = 3 };

struct ChannelInfo {
    ChannelId handle = 0;
    ChannelType type = ChannelType::File;
};

inline const char* channelTypeName(ChannelType type) noexcept {
    switch (type) {
    case ChannelType::Console: return "CONSOLE";
    case ChannelType::Window: return "WINDOW";
    case ChannelType::File: return "FILE";
    case ChannelType::Serial: return "SERIAL";
    case ChannelType::Network: return "NETWORK";
    case ChannelType::Null: return "NULL";
    }
    return "UNKNOWN";
}

// Channels are owned by the session that opened them. Line reads omit their
// newline; exact-byte operations preserve NUL and arbitrary byte sequences.
class ChannelService {
public:
    virtual ~ChannelService() = default;
    virtual ChannelId open(SessionId session, const std::string& path,
                           ChannelMode mode) = 0;
    virtual ChannelId openDevice(SessionId session, ChannelDevice device,
                                 const std::vector<std::string>& parameters) {
        if (device != ChannelDevice::File || parameters.size() != 2)
            throw ServiceError(ServiceErrorCode::Unavailable,
                               "channel device is unavailable");
        std::string mode = parameters[1];
        for (char& character : mode) {
            const auto value = static_cast<unsigned char>(character);
            if (value >= 'a' && value <= 'z') character = static_cast<char>(value - 'a' + 'A');
        }
        ChannelMode fileMode;
        if (mode == "READ") fileMode = ChannelMode::Read;
        else if (mode == "WRITE") fileMode = ChannelMode::Write;
        else if (mode == "APPEND") fileMode = ChannelMode::Append;
        else throw ServiceError(ServiceErrorCode::InvalidPath,
                                "OPEN mode must be READ, WRITE, or APPEND");
        return open(session, parameters[0], fileMode);
    }
    virtual std::optional<std::string> readLine(SessionId session,
                                                ChannelId channel) = 0;
    // Binary transfers preserve every byte, including NUL and invalid UTF-8.
    // Seek positions are zero-based absolute offsets and are supported only
    // by seekable endpoints.
    virtual std::string readBytes(SessionId session, ChannelId channel,
                                  std::size_t count) = 0;
    virtual void writeBytes(SessionId session, ChannelId channel,
                             std::string_view bytes) = 0;
    virtual void seek(SessionId session, ChannelId channel,
                      std::uint64_t offset) = 0;
    virtual bool atEnd(SessionId session, ChannelId channel) = 0;
    virtual void write(SessionId session, ChannelId channel,
                       const std::string& text) = 0;
    virtual void close(SessionId session, ChannelId channel) = 0;
    virtual void closeSession(SessionId session) = 0;

    // Lists open endpoints owned by this session. Adapters that do not expose
    // endpoint enumeration may keep the default empty implementation.
    virtual std::vector<ChannelInfo> listOpen(SessionId) { return {}; }
};

// Central per-session handle registry. File operations are forwarded to the
// platform file adapter; serial and null endpoints live in the registry.
class ChannelRegistry final : public ChannelService {
public:
    using SerialWriter = std::function<void(std::string_view)>;

    explicit ChannelRegistry(std::shared_ptr<ChannelService> files,
                             SerialWriter serialWriter = {});
    ~ChannelRegistry() override;

    ChannelId open(SessionId session, const std::string& path,
                   ChannelMode mode) override;
    ChannelId openDevice(SessionId session, ChannelDevice device,
                         const std::vector<std::string>& parameters) override;
    std::optional<std::string> readLine(SessionId session,
                                        ChannelId channel) override;
    std::string readBytes(SessionId session, ChannelId channel,
                          std::size_t count) override;
    void writeBytes(SessionId session, ChannelId channel,
                    std::string_view bytes) override;
    void seek(SessionId session, ChannelId channel, std::uint64_t offset) override;
    bool atEnd(SessionId session, ChannelId channel) override;
    void write(SessionId session, ChannelId channel,
               const std::string& text) override;
    void close(SessionId session, ChannelId channel) override;
    void closeSession(SessionId session) override;
    std::vector<ChannelInfo> listOpen(SessionId session) override;

private:
    struct Endpoint {
        SessionId session = 0;
        ChannelType type = ChannelType::File;
        ChannelId backend = 0;
    };
    std::shared_ptr<ChannelService> files_;
    SerialWriter serialWriter_;
    std::unordered_map<ChannelId, Endpoint> endpoints_;
    ChannelId nextId_ = 2;
    mutable std::mutex mutex_;

    ChannelId allocate(SessionId session, ChannelType type, ChannelId backend = 0);
    Endpoint endpoint(SessionId session, ChannelId channel) const;
};

} // namespace ql32::runtime
