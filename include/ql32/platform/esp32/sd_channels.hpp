#pragma once

#include "ql32/runtime/bulk_write_gate.hpp"
#include "ql32/runtime/channel.hpp"
#include "ql32/runtime/storage.hpp"

#include <FS.h>
#include <mutex>
#include <string>
#include <unordered_map>

namespace ql32::platform::esp32 {

// Session-owned text channels backed by an Arduino filesystem (normally SD).
class SdChannels final : public runtime::ChannelService {
public:
    explicit SdChannels(fs::FS& filesystem, std::string drive = "SDC")
        : filesystem_(filesystem), drive_(std::move(drive)) {}
    ~SdChannels() override;

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
    std::uint64_t position(runtime::SessionId session, runtime::ChannelId channel) override;
    std::uint64_t length(runtime::SessionId session, runtime::ChannelId channel) override;
    bool atEnd(runtime::SessionId session, runtime::ChannelId channel) override;
    void write(runtime::SessionId session, runtime::ChannelId channel,
               const std::string& text) override;
    void close(runtime::SessionId session, runtime::ChannelId channel) override;
    void closeSession(runtime::SessionId session) override;
    std::vector<runtime::ChannelInfo> listOpen(runtime::SessionId session) override;

    // Optional: told when a large write begins/ends (internal flash only).
    void setBulkWriteGate(runtime::BulkWriteGate* gate) noexcept { bulkWriteGate_ = gate; }

private:
    struct OpenChannel {
        runtime::SessionId session;
        runtime::ChannelMode mode;
        fs::File file;
    };

    runtime::BulkWriteGate* bulkWriteGate_ = nullptr;

    fs::FS& filesystem_;
    std::string drive_;
    std::unordered_map<runtime::ChannelId, OpenChannel> channels_;
    runtime::ChannelId nextId_ = 2;
    std::mutex mutex_;

    void ensureAvailable();
    std::string resolve(const std::string& path) const;
    OpenChannel& owned(runtime::SessionId session, runtime::ChannelId channel);
};

} // namespace ql32::platform::esp32
