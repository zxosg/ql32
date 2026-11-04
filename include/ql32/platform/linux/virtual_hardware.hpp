#pragma once

#include "ql32/runtime/channel.hpp"

#include <chrono>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace ql32::platform::linux_host {

// Deterministic hardware model for CLI/REPL acceptance and desktop development.
class VirtualHardware final : public runtime::HardwareChannelService {
public:
    runtime::ChannelId open(runtime::SessionId, runtime::ChannelDevice,
                            const std::vector<runtime::DeviceArgument>&) override;
    double readValue(runtime::SessionId, runtime::ChannelId,
                     std::optional<double>) override;
    void writeValue(runtime::SessionId, runtime::ChannelId, double,
                    std::optional<double>) override;
    std::size_t readBytes(runtime::SessionId, runtime::ChannelId,
                          std::optional<double>, std::uint8_t*, std::size_t) override;
    void writeBytes(runtime::SessionId, runtime::ChannelId, std::optional<double>,
                    const std::uint8_t*, std::size_t) override;
    void transfer(runtime::SessionId, runtime::ChannelId, const std::uint8_t*,
                  std::size_t, std::uint8_t*, std::size_t) override;
    std::optional<std::string> readLine(runtime::SessionId, runtime::ChannelId) override;
    bool lineAvailable(runtime::SessionId, runtime::ChannelId) override;
    void writeText(runtime::SessionId, runtime::ChannelId, std::string_view) override;
    void close(runtime::SessionId, runtime::ChannelId) override;
    void closeSession(runtime::SessionId) override;
    runtime::ChannelType type(runtime::ChannelId) const override;

    void setPinInput(int pin, int value);
    void setAnalogInput(int pin, int value);
    void setTouchInput(int pin, int value);
    void setSerialLoopback(bool enabled);
    void queueSerialLine(runtime::ChannelId channel, std::string line);
    std::string takeSerialOutput(runtime::ChannelId channel);

private:
    enum class Kind { Gpio, Adc, Spi, I2c, Parallel, Serial, CapTouch, Timer };
    struct Endpoint {
        runtime::SessionId session = 0;
        Kind kind = Kind::Gpio;
        std::vector<int> pins;
        int mode = 0;
        bool directionSwitchable = false;
        int pull = 0;
        int value = 0;
        int bus = 0;
        int address = 0;
        int chipSelect = -1;
        int periodMs = 0;
        int timerMode = 0;
        int pendingExpirations = 0;
        std::chrono::steady_clock::time_point nextExpiration{};
        std::map<unsigned, std::vector<std::uint8_t>> registers;
        std::deque<std::uint8_t> rx;
        std::deque<std::string> lines;
        std::string tx;
    };
    static double numeric(const runtime::DeviceArgument&, const char* field);
    static int integer(const runtime::DeviceArgument&, const char* field,
                       int minimum, int maximum);
    Endpoint& endpoint(runtime::SessionId, runtime::ChannelId);
    const Endpoint& endpoint(runtime::SessionId, runtime::ChannelId) const;
    std::unordered_map<runtime::ChannelId, Endpoint> endpoints_;
    std::map<int, runtime::ChannelId> pinOwners_;
    std::set<std::pair<int, int>> i2cOwners_;
    std::unordered_map<int, int> pinInputs_;
    std::unordered_map<int, int> analogInputs_;
    std::unordered_map<int, int> touchInputs_;
    runtime::ChannelId nextId_ = 1;
    bool serialLoopback_ = false;
    mutable std::mutex mutex_;
};

} // namespace ql32::platform::linux_host
