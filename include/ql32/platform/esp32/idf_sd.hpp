#pragma once

#include "ql32/runtime/channel.hpp"
#include "ql32/runtime/storage.hpp"

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace ql32::platform::esp32 {

struct SdSpiPins {
    int mosi = -1;
    int miso = -1;
    int clock = -1;
    int chipSelect = -1;
};

// Owns an ESP-IDF SDSPI mount. This is separate from Arduino SD so Olimex can
// use the same SPI2/VFS path proven by ESPectrum.
class IdfSdCard final {
public:
    ~IdfSdCard();

    IdfSdCard() = default;
    IdfSdCard(const IdfSdCard&) = delete;
    IdfSdCard& operator=(const IdfSdCard&) = delete;

    bool mount(SdSpiPins pins, std::string mountPoint = "/sd");
    void unmount();
    bool mounted() const noexcept { return mounted_; }
    const std::string& mountPoint() const noexcept { return mountPoint_; }
    const std::string& error() const noexcept { return error_; }

private:
    // Kept opaque so ESP-IDF types do not leak from this platform adapter.
    void* card_ = nullptr;
    std::string mountPoint_ = "/sd";
    std::string error_;
    bool mounted_ = false;
    bool busInitialized_ = false;
};

class IdfSdStorage final : public runtime::StorageService {
public:
    explicit IdfSdStorage(const IdfSdCard& card) : card_(card) {}

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
    const IdfSdCard& card_;

    void ensureAvailable() const;
    std::string resolve(const std::string& path, bool allowRoot) const;
};

class IdfSdChannels final : public runtime::ChannelService {
public:
    explicit IdfSdChannels(const IdfSdCard& card) : card_(card) {}
    ~IdfSdChannels() override;

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
        runtime::SessionId session = 0;
        runtime::ChannelMode mode = runtime::ChannelMode::Read;
        int descriptor = -1;
    };

    const IdfSdCard& card_;
    std::unordered_map<runtime::ChannelId, OpenChannel> channels_;
    runtime::ChannelId nextId_ = 2;
    std::mutex mutex_;

    void ensureAvailable() const;
    std::string resolve(const std::string& path) const;
    OpenChannel& owned(runtime::SessionId session, runtime::ChannelId channel);
    static void writeAll(int descriptor, std::string_view bytes);
};

} // namespace ql32::platform::esp32
