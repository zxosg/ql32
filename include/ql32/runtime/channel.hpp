#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <variant>

#include "ql32/runtime/storage.hpp"

namespace ql32::runtime {

// XFER stages transmit and receive data in the portable VM before invoking a
// channel adapter. Keep their combined footprint bounded on small targets.
inline constexpr std::size_t maximumHardwareTransferBytes = 4096;

using SessionId = std::uint64_t;
using ChannelId = std::uint64_t;

enum class ChannelMode { Read, Write, Append };

enum class ChannelType { Console, Window, File, Serial, Network, Null, Hardware, Touch };

enum class ChannelDevice : std::uint64_t {
    File = 1, Serial = 2, Null = 3, Window = 4, Gpio = 5, Adc = 6,
    Spi = 7, I2c = 8, Parallel = 9, Infrared = 10, CapTouch = 11, Timer = 12,
    Touch = 13
};

enum class TouchEventType : std::uint8_t { Down = 1, Move = 2, Up = 3, Cancel = 4 };

struct TouchEvent {
    TouchEventType type = TouchEventType::Down;
    std::int32_t x = 0; // coordinates local to window
    std::int32_t y = 0;
    std::uint64_t window = 0; // BASIC graphics channel handle
};

using DeviceArgument = std::variant<double, std::string>;

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
    case ChannelType::Hardware: return "HARDWARE";
    case ChannelType::Touch: return "TOUCH";
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
    virtual ChannelId openTypedDevice(SessionId session, ChannelDevice device,
                                     const std::vector<DeviceArgument>& parameters) {
        std::vector<std::string> strings;
        strings.reserve(parameters.size());
        for (const auto& value : parameters) {
            if (const auto* text = std::get_if<std::string>(&value)) strings.push_back(*text);
            else strings.push_back(std::to_string(std::get<double>(value)));
        }
        return openDevice(session, device, strings);
    }
    virtual double readValue(SessionId, ChannelId, std::optional<double> = std::nullopt) {
        throw ServiceError(ServiceErrorCode::Unavailable, "device input is unavailable");
    }
    virtual void writeValue(SessionId, ChannelId, double,
                            std::optional<double> = std::nullopt) {
        throw ServiceError(ServiceErrorCode::Unavailable, "device output is unavailable");
    }
    virtual std::size_t readDeviceBytes(SessionId, ChannelId,
                                        std::optional<double>, std::uint8_t*, std::size_t) {
        throw ServiceError(ServiceErrorCode::Unavailable, "device byte input is unavailable");
    }
    virtual void writeDeviceBytes(SessionId, ChannelId, std::optional<double>,
                                  const std::uint8_t*, std::size_t) {
        throw ServiceError(ServiceErrorCode::Unavailable, "device byte output is unavailable");
    }
    virtual void transfer(SessionId, ChannelId, const std::uint8_t*, std::size_t,
                          std::uint8_t*, std::size_t) {
        throw ServiceError(ServiceErrorCode::Unavailable, "device transfer is unavailable");
    }
    virtual std::optional<std::string> readLine(SessionId session,
                                                ChannelId channel) = 0;
    virtual bool lineAvailable(SessionId, ChannelId) { return false; }
    virtual bool isHardwareChannel(SessionId, ChannelId) const { return false; }
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
    // The display adapter publishes a routed event to every touch channel
    // owned by the task that owns the hit window.
    virtual void publishTouch(SessionId, const TouchEvent&) {}
};

// Hardware implementations are injected by platform startup. The BASIC VM
// sees only the portable channel operations above, never vendor GPIO/bus APIs.
class HardwareChannelService {
public:
    virtual ~HardwareChannelService() = default;
    virtual ChannelId open(SessionId, ChannelDevice,
                           const std::vector<DeviceArgument>&) = 0;
    virtual double readValue(SessionId, ChannelId, std::optional<double>) = 0;
    virtual void writeValue(SessionId, ChannelId, double, std::optional<double>) = 0;
    virtual std::size_t readBytes(SessionId, ChannelId, std::optional<double>,
                                  std::uint8_t*, std::size_t) = 0;
    virtual void writeBytes(SessionId, ChannelId, std::optional<double>,
                            const std::uint8_t*, std::size_t) = 0;
    virtual void transfer(SessionId, ChannelId, const std::uint8_t*, std::size_t,
                          std::uint8_t*, std::size_t) = 0;
    virtual std::optional<std::string> readLine(SessionId, ChannelId) = 0;
    virtual bool lineAvailable(SessionId, ChannelId) = 0;
    virtual void writeText(SessionId, ChannelId, std::string_view) = 0;
    virtual void close(SessionId, ChannelId) = 0;
    virtual void closeSession(SessionId) = 0;
    virtual ChannelType type(ChannelId) const = 0;
};

// Central per-session handle registry. File operations are forwarded to the
// platform file adapter; serial and null endpoints live in the registry.
class ChannelRegistry final : public ChannelService {
public:
    using SerialWriter = std::function<void(std::string_view)>;

    explicit ChannelRegistry(std::shared_ptr<ChannelService> files,
                             SerialWriter serialWriter = {},
                             std::shared_ptr<HardwareChannelService> hardware = {},
                             bool touchAvailable = false);
    ~ChannelRegistry() override;

    ChannelId open(SessionId session, const std::string& path,
                   ChannelMode mode) override;
    ChannelId openDevice(SessionId session, ChannelDevice device,
                         const std::vector<std::string>& parameters) override;
    ChannelId openTypedDevice(SessionId session, ChannelDevice device,
                              const std::vector<DeviceArgument>& parameters) override;
    double readValue(SessionId session, ChannelId channel,
                     std::optional<double> reg = std::nullopt) override;
    void writeValue(SessionId session, ChannelId channel, double value,
                    std::optional<double> reg = std::nullopt) override;
    std::size_t readDeviceBytes(SessionId session, ChannelId channel,
                                std::optional<double> reg, std::uint8_t* buffer,
                                std::size_t capacity) override;
    void writeDeviceBytes(SessionId session, ChannelId channel,
                          std::optional<double> reg, const std::uint8_t* buffer,
                          std::size_t length) override;
    void transfer(SessionId session, ChannelId channel, const std::uint8_t* tx,
                  std::size_t txLength, std::uint8_t* rx, std::size_t rxLength) override;
    std::optional<std::string> readLine(SessionId session,
                                        ChannelId channel) override;
    bool lineAvailable(SessionId session, ChannelId channel) override;
    bool isHardwareChannel(SessionId session, ChannelId channel) const override;
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
    void publishTouch(SessionId session, const TouchEvent& event) override;

private:
    struct Endpoint {
        SessionId session = 0;
        ChannelType type = ChannelType::File;
        ChannelId backend = 0;
    };
    std::shared_ptr<ChannelService> files_;
    SerialWriter serialWriter_;
    std::shared_ptr<HardwareChannelService> hardware_;
    bool touchAvailable_ = false;
    std::unordered_map<ChannelId, Endpoint> endpoints_;
    std::unordered_map<ChannelId, std::deque<TouchEvent>> touchEvents_;
    ChannelId nextId_ = 2;
    mutable std::mutex mutex_;

    ChannelId allocate(SessionId session, ChannelType type, ChannelId backend = 0);
    Endpoint endpoint(SessionId session, ChannelId channel) const;
};

} // namespace ql32::runtime
