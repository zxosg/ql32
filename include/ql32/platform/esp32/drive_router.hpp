#pragma once

#include "ql32/runtime/channel.hpp"
#include "ql32/runtime/storage.hpp"

#include <mutex>
#include <unordered_map>

namespace ql32::platform::esp32 {

// Routes explicitly qualified FFS:/ and SDC:/ paths to their independent
// platform adapters. Unqualified paths use the SD card drive.
class DriveRouterStorage final : public runtime::StorageService {
public:
    DriveRouterStorage(runtime::StorageService& ffs, runtime::StorageService& sdc)
        : ffs_(ffs), sdc_(sdc) {}

    std::string readText(const std::string& path) override;
    void writeText(const std::string& path, const std::string& contents) override;
    std::vector<runtime::FileEntry> list(const std::string& path) override;
    bool exists(const std::string& path) override;
    std::uintmax_t size(const std::string& path) override;
    void rename(const std::string& path, const std::string& newPath) override;
    void remove(const std::string& path) override;
    void removeDirectory(const std::string& path) override;
    void format(const std::string& path) override;

private:
    runtime::StorageService& ffs_;
    runtime::StorageService& sdc_;
    runtime::StorageService& forPath(const std::string& path) const;
};

// Provides one channel handle space while preserving ownership in each
// filesystem-specific channel adapter.
class DriveRouterChannels final : public runtime::ChannelService {
public:
    DriveRouterChannels(runtime::ChannelService& ffs, runtime::ChannelService& sdc)
        : ffs_(ffs), sdc_(sdc) {}

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
    struct RoutedChannel {
        runtime::SessionId session = 0;
        runtime::ChannelService* service = nullptr;
        runtime::ChannelId channel = 0;
    };

    runtime::ChannelService& ffs_;
    runtime::ChannelService& sdc_;
    mutable std::mutex mutex_;
    std::unordered_map<runtime::ChannelId, RoutedChannel> channels_;
    runtime::ChannelId nextChannel_ = 1;

    runtime::ChannelId remember(runtime::SessionId session,
                                 runtime::ChannelService& service,
                                 runtime::ChannelId channel);
    RoutedChannel lookup(runtime::SessionId session, runtime::ChannelId channel) const;
    runtime::ChannelService& forPath(const std::string& path) const;
};

} // namespace ql32::platform::esp32
