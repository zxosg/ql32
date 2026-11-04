#pragma once

#include "ql32/runtime/channel.hpp"

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>

#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <vector>

namespace ql32::platform::esp32 {

class ArduinoHardware final : public runtime::HardwareChannelService {
public:
    ArduinoHardware(SPIClass* spi0, SPIClass* spi1, TwoWire* i2c,
                    int i2cSda, int i2cScl, int uart1Tx, int uart1Rx,
                    int uart2Tx, int uart2Rx, std::vector<int> reservedPins = {});
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
        int clockHz = 1000000;
        int spiMode = 0;
        int spiBitOrder = 0;
        int periodMs = 0;
        int timerMode = 0;
        std::uint64_t nextExpirationMs = 0;
        int touchBaseline = 0;
        int touchCandidate = 0;
        int touchStableCount = 0;
        int touchState = 0;
        bool skipNextLineFeed = false;
        std::string lineBuffer;
    };
    static double numeric(const runtime::DeviceArgument&, const char* field);
    static int integer(const runtime::DeviceArgument&, const char* field,
                       int minimum, int maximum);
    Endpoint& endpoint(runtime::SessionId, runtime::ChannelId);
    const Endpoint& endpoint(runtime::SessionId, runtime::ChannelId) const;
    HardwareSerial* serial(int port) const;
    SPIClass* spi(int bus) const;
    void initializeI2c(int clockHz);
    void releasePins(const Endpoint&);

    SPIClass* spi0_;
    SPIClass* spi1_;
    TwoWire* i2c_;
    int i2cSda_;
    int i2cScl_;
    int uart1Tx_;
    int uart1Rx_;
    int uart2Tx_;
    int uart2Rx_;
    bool i2cStarted_ = false;
    int i2cClockHz_ = 100000;
    runtime::ChannelId nextId_ = 1;
    std::map<runtime::ChannelId, Endpoint> endpoints_;
    std::map<int, runtime::ChannelId> pinOwners_;
    std::set<std::pair<int, int>> i2cOwners_;
    mutable std::mutex mutex_;
};

} // namespace ql32::platform::esp32
