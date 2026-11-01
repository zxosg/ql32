#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ql32::runtime {

using SessionId = std::uint64_t;
using ChannelId = std::uint64_t;

enum class ChannelMode { Read, Write, Append };

enum class ChannelType { Console, Window, File, Serial, Network, Null };

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

} // namespace ql32::runtime
