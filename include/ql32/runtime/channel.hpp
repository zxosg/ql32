#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace ql32::runtime {

using SessionId = std::uint64_t;
using ChannelId = std::uint64_t;

enum class ChannelMode { Read, Write, Append };

// Text channels are owned by the session that opened them. Reads return one
// line without its newline, or nullopt at EOF. Writes preserve the supplied
// bytes; adapters may define how the host represents text files.
class ChannelService {
public:
    virtual ~ChannelService() = default;
    virtual ChannelId open(SessionId session, const std::string& path,
                           ChannelMode mode) = 0;
    virtual std::optional<std::string> readLine(SessionId session,
                                                ChannelId channel) = 0;
    virtual bool atEnd(SessionId session, ChannelId channel) = 0;
    virtual void write(SessionId session, ChannelId channel,
                       const std::string& text) = 0;
    virtual void close(SessionId session, ChannelId channel) = 0;
    virtual void closeSession(SessionId session) = 0;
};

} // namespace ql32::runtime
