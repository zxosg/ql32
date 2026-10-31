#pragma once

#include "ql32/runtime/channel.hpp"
#include "ql32/runtime/storage.hpp"

#include <FS.h>
#include <mutex>
#include <unordered_map>

namespace ql32::platform::esp32 {

// Session-owned text channels backed by an Arduino filesystem (normally SD).
class SdChannels final : public runtime::ChannelService {
public:
    explicit SdChannels(fs::FS& filesystem) : filesystem_(filesystem) {}
    ~SdChannels() override;

    runtime::ChannelId open(runtime::SessionId session, const std::string& path,
                            runtime::ChannelMode mode) override;
    std::optional<std::string> readLine(runtime::SessionId session,
                                        runtime::ChannelId channel) override;
    bool atEnd(runtime::SessionId session, runtime::ChannelId channel) override;
    void write(runtime::SessionId session, runtime::ChannelId channel,
               const std::string& text) override;
    void close(runtime::SessionId session, runtime::ChannelId channel) override;
    void closeSession(runtime::SessionId session) override;

private:
    struct OpenChannel {
        runtime::SessionId session;
        runtime::ChannelMode mode;
        fs::File file;
    };

    fs::FS& filesystem_;
    std::unordered_map<runtime::ChannelId, OpenChannel> channels_;
    runtime::ChannelId nextId_ = 1;
    std::mutex mutex_;

    void ensureAvailable();
    static std::string resolve(const std::string& path);
    OpenChannel& owned(runtime::SessionId session, runtime::ChannelId channel);
};

} // namespace ql32::platform::esp32
