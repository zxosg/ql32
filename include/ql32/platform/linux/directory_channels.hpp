#pragma once

#include "ql32/runtime/channel.hpp"
#include "ql32/runtime/storage.hpp"

#include <filesystem>
#include <fstream>
#include <mutex>
#include <unordered_map>

namespace ql32::platform::linux_host {

class DirectoryChannels final : public runtime::ChannelService {
public:
    explicit DirectoryChannels(std::filesystem::path root);

    runtime::ChannelId open(runtime::SessionId session, const std::string& path,
                            runtime::ChannelMode mode) override;
    std::optional<std::string> readLine(runtime::SessionId session,
                                        runtime::ChannelId channel) override;
    std::string readBytes(runtime::SessionId session, runtime::ChannelId channel,
                          std::size_t count) override;
    void writeBytes(runtime::SessionId session, runtime::ChannelId channel,
                    std::string_view bytes) override;
    void seek(runtime::SessionId session, runtime::ChannelId channel,
              std::uint64_t offset) override;
    bool atEnd(runtime::SessionId session, runtime::ChannelId channel) override;
    void write(runtime::SessionId session, runtime::ChannelId channel,
               const std::string& text) override;
    void close(runtime::SessionId session, runtime::ChannelId channel) override;
    void closeSession(runtime::SessionId session) override;
    std::vector<runtime::ChannelInfo> listOpen(runtime::SessionId session) override;

private:
    struct OpenChannel {
        runtime::SessionId session;
        runtime::ChannelMode mode;
        std::fstream file;
    };

    std::filesystem::path root_;
    std::unordered_map<runtime::ChannelId, OpenChannel> channels_;
    runtime::ChannelId nextId_ = 2;
    std::mutex mutex_;

    std::filesystem::path resolve(const std::string& path,
                                  runtime::ChannelMode mode) const;
    OpenChannel& owned(runtime::SessionId session, runtime::ChannelId channel);
};

} // namespace ql32::platform::linux_host
