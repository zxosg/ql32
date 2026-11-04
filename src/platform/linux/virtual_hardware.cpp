#include "ql32/platform/linux/virtual_hardware.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace ql32::platform::linux_host {
namespace {
using runtime::ChannelDevice;
using runtime::DeviceArgument;
using runtime::ServiceError;
using runtime::ServiceErrorCode;
constexpr int PinInput = 0;
constexpr int PinOutput = 1;
constexpr int PinInputOutput = 2;
constexpr int PinModeMarker = 100;
constexpr int InitialValueMarker = 101;

[[noreturn]] void invalid(const std::string& message) {
    throw ServiceError(ServiceErrorCode::InvalidPath, message);
}
} // namespace

double VirtualHardware::numeric(const DeviceArgument& argument, const char* field) {
    if (const auto* value = std::get_if<double>(&argument); value && std::isfinite(*value))
        return *value;
    invalid(std::string(field) + " must be numeric");
}

int VirtualHardware::integer(const DeviceArgument& argument, const char* field,
                             int minimum, int maximum) {
    const double value = numeric(argument, field);
    if (std::trunc(value) != value || value < minimum || value > maximum)
        invalid(std::string("invalid ") + field);
    return static_cast<int>(value);
}

runtime::ChannelId VirtualHardware::open(
    runtime::SessionId session, ChannelDevice device,
    const std::vector<DeviceArgument>& parameters) {
    std::lock_guard lock(mutex_);
    if (nextId_ == 0) throw ServiceError(ServiceErrorCode::Unavailable,
                                         "hardware channel handles exhausted");
    Endpoint opened;
    opened.session = session;
    std::vector<int> reserved;
    const auto requireCount = [&](std::size_t min, std::size_t max) {
        if (parameters.size() < min || parameters.size() > max)
            invalid("incorrect hardware OPEN parameter count");
    };
    const auto addPin = [&](int pin) {
        if (std::find(reserved.begin(), reserved.end(), pin) != reserved.end())
            invalid("hardware OPEN contains duplicate pins");
        const auto owner = pinOwners_.find(pin);
        if (owner != pinOwners_.end())
            throw ServiceError(ServiceErrorCode::Unavailable, "hardware pin is already reserved");
        reserved.push_back(pin);
    };
    switch (device) {
    case ChannelDevice::Gpio: {
        requireCount(2, 5);
        opened.kind = Kind::Gpio;
        const int pin = integer(parameters[0], "GPIO pin", 0, 63);
        opened.mode = integer(parameters[1], "GPIO mode", PinInput, PinInputOutput);
        std::size_t option = 2;
        if (option < parameters.size() && numeric(parameters[option], "GPIO option") == InitialValueMarker) {
            ++option;
            if (option >= parameters.size()) invalid("missing GPIO initial value");
            opened.value = integer(parameters[option++], "initial GPIO value", 0, 1);
        } else if (option < parameters.size()) {
            opened.pull = integer(parameters[option++], "GPIO pull", 0, 2);
        }
        if (option < parameters.size()) {
            if (integer(parameters[option++], "GPIO initial marker", InitialValueMarker,
                        InitialValueMarker) != InitialValueMarker || option >= parameters.size())
                invalid("invalid GPIO initial value option");
            opened.value = integer(parameters[option++], "initial GPIO value", 0, 1);
        }
        if (option != parameters.size()) invalid("unexpected GPIO OPEN parameter");
        addPin(pin);
        opened.pins = {pin};
        if (opened.mode == PinOutput) {
            pinInputs_[pin] = opened.value;
        } else if (!pinInputs_.count(pin)) {
            pinInputs_[pin] = opened.pull == 1 ? 1 : 0;
        }
        break;
    }
    case ChannelDevice::Adc: {
        requireCount(1, 2);
        opened.kind = Kind::Adc;
        const int pin = integer(parameters[0], "ADC pin", 0, 63);
        if (parameters.size() == 2) (void)integer(parameters[1], "ADC resolution", 9, 12);
        addPin(pin);
        opened.pins = {pin};
        break;
    }
    case ChannelDevice::Spi: {
        requireCount(4, 5);
        opened.kind = Kind::Spi;
        opened.bus = integer(parameters[0], "SPI bus", 0, 16);
        opened.chipSelect = integer(parameters[1], "SPI chip select pin", 0, 63);
        (void)integer(parameters[2], "SPI clock", 1, 100000000);
        (void)integer(parameters[3], "SPI mode", 0, 3);
        if (parameters.size() == 5) (void)integer(parameters[4], "SPI bit order", 0, 1);
        addPin(opened.chipSelect);
        opened.pins = {opened.chipSelect};
        break;
    }
    case ChannelDevice::I2c: {
        requireCount(2, 3);
        opened.kind = Kind::I2c;
        opened.bus = integer(parameters[0], "I2C bus", 0, 16);
        opened.address = integer(parameters[1], "I2C address", 3, 119);
        if (i2cOwners_.find({opened.bus, opened.address}) != i2cOwners_.end())
            throw ServiceError(ServiceErrorCode::Unavailable,
                               "I2C device address is already open");
        if (parameters.size() == 3) (void)integer(parameters[2], "I2C clock", 1, 1000000);
        break;
    }
    case ChannelDevice::Parallel: {
        requireCount(10, 14);
        opened.kind = Kind::Parallel;
        for (std::size_t i = 0; i < 8; ++i) {
            const int pin = integer(parameters[i], "parallel pin", 0, 63);
            addPin(pin);
            opened.pins.push_back(pin);
        }
        std::size_t option = 8;
        if (integer(parameters[option++], "parallel mode marker", PinModeMarker,
                    PinModeMarker) != PinModeMarker) invalid("missing KPinMode marker");
        opened.mode = integer(parameters[option++], "parallel direction", 0, 2);
        opened.directionSwitchable = opened.mode == PinInputOutput;
        if (option < parameters.size()) {
            if (integer(parameters[option++], "parallel initial marker", InitialValueMarker,
                        InitialValueMarker) != InitialValueMarker || option >= parameters.size())
                invalid("invalid parallel initial value option");
            opened.value = integer(parameters[option++], "parallel initial value", 0, 255);
            if (option != parameters.size()) invalid("unexpected parallel OPEN parameter");
        }
        break;
    }
    case ChannelDevice::Serial: {
        requireCount(2, 4);
        opened.kind = Kind::Serial;
        opened.bus = integer(parameters[0], "UART port", 1, 2);
        (void)integer(parameters[1], "UART baud", 1, 4000000);
        if (parameters.size() > 2 && integer(parameters[2], "UART frame", 0, 16) != 0)
            throw ServiceError(ServiceErrorCode::Unavailable, "UART frame is unsupported");
        if (parameters.size() > 3 && integer(parameters[3], "UART flow control", 0, 2) != 0)
            throw ServiceError(ServiceErrorCode::Unavailable, "UART flow control is unsupported");
        break;
    }
    case ChannelDevice::CapTouch: {
        requireCount(1, 1);
        opened.kind = Kind::CapTouch;
        const int pin = integer(parameters[0], "touch pad", 0, 63);
        addPin(pin);
        opened.pins = {pin};
        break;
    }
    case ChannelDevice::Timer: {
        requireCount(2, 2);
        opened.kind = Kind::Timer;
        opened.periodMs = integer(parameters[0], "timer period", 1, 86400000);
        opened.timerMode = integer(parameters[1], "timer mode", 0, 1);
        opened.nextExpiration = std::chrono::steady_clock::now() +
                                std::chrono::milliseconds(opened.periodMs);
        break;
    }
    default:
        throw ServiceError(ServiceErrorCode::Unavailable, "hardware device is unavailable");
    }
    const runtime::ChannelId id = nextId_++;
    endpoints_.emplace(id, std::move(opened));
    const auto& stored = endpoints_.at(id);
    if (stored.kind == Kind::I2c) i2cOwners_.insert({stored.bus, stored.address});
    for (const int pin : reserved) pinOwners_[pin] = id;
    return id;
}

VirtualHardware::Endpoint& VirtualHardware::endpoint(runtime::SessionId session,
                                                      runtime::ChannelId id) {
    const auto found = endpoints_.find(id);
    if (found == endpoints_.end() || found->second.session != session)
        throw ServiceError(ServiceErrorCode::InvalidHandle,
                           "hardware channel is not open for this session");
    return found->second;
}

const VirtualHardware::Endpoint& VirtualHardware::endpoint(runtime::SessionId session,
                                                            runtime::ChannelId id) const {
    const auto found = endpoints_.find(id);
    if (found == endpoints_.end() || found->second.session != session)
        throw ServiceError(ServiceErrorCode::InvalidHandle,
                           "hardware channel is not open for this session");
    return found->second;
}

double VirtualHardware::readValue(runtime::SessionId session, runtime::ChannelId id,
                                  std::optional<double> reg) {
    std::lock_guard lock(mutex_);
    Endpoint& opened = endpoint(session, id);
    switch (opened.kind) {
    case Kind::Gpio:
        if (opened.mode == PinOutput) throw ServiceError(ServiceErrorCode::Unavailable,
                                                          "GPIO channel is output-only");
        return pinInputs_[opened.pins.front()] ? 1 : 0;
    case Kind::Adc:
        return analogInputs_[opened.pins.front()];
    case Kind::Parallel: {
        if (opened.mode == PinOutput) throw ServiceError(ServiceErrorCode::Unavailable,
                                                          "parallel channel is output-only");
        int value = 0;
        for (std::size_t i = 0; i < opened.pins.size(); ++i)
            value = (value << 1) | (pinInputs_[opened.pins[i]] ? 1 : 0);
        return value;
    }
    case Kind::I2c: {
        if (!reg || std::trunc(*reg) != *reg || *reg < 0 || *reg > 255)
            invalid("I2C scalar read requires an 8-bit register");
        const auto found = opened.registers.find(static_cast<unsigned>(*reg));
        return found == opened.registers.end() || found->second.empty() ? 0 : found->second[0];
    }
    case Kind::CapTouch: return touchInputs_[opened.pins.front()] ? 1 : 0;
    case Kind::Timer: {
        const auto now = std::chrono::steady_clock::now();
        if (opened.periodMs > 0 && now >= opened.nextExpiration) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - opened.nextExpiration).count();
            const int count = 1 + static_cast<int>(elapsed / opened.periodMs);
            opened.pendingExpirations += count;
            if (opened.timerMode == 0) opened.periodMs = 0;
            else opened.nextExpiration += std::chrono::milliseconds(count * opened.periodMs);
        }
        const int count = opened.pendingExpirations;
        opened.pendingExpirations = 0;
        return count;
    }
    default:
        throw ServiceError(ServiceErrorCode::Unavailable, "device does not support scalar input");
    }
}

void VirtualHardware::writeValue(runtime::SessionId session, runtime::ChannelId id,
                                 double value, std::optional<double> reg) {
    if (!std::isfinite(value) || std::trunc(value) != value)
        invalid("device output must be an integer");
    std::lock_guard lock(mutex_);
    Endpoint& opened = endpoint(session, id);
    if (opened.kind == Kind::Gpio) {
        if (opened.mode == PinInput) throw ServiceError(ServiceErrorCode::Unavailable,
                                                         "GPIO channel is input-only");
        if (value < 0 || value > 1) invalid("GPIO output must be 0 or 1");
        opened.value = static_cast<int>(value);
        pinInputs_[opened.pins.front()] = opened.value;
        return;
    }
    if (opened.kind == Kind::Parallel) {
        if (reg && *reg == PinModeMarker) {
            const int mode = static_cast<int>(value);
            if (!opened.directionSwitchable || (mode != PinInput && mode != PinOutput))
                invalid("parallel direction change is not allowed");
            opened.mode = mode;
            if (mode == PinOutput) {
                for (std::size_t i = 0; i < opened.pins.size(); ++i)
                    pinInputs_[opened.pins[i]] = (opened.value >> (7 - i)) & 1;
            }
            return;
        }
        if (value < 0 || value > 255) invalid("parallel output must be 0 through 255");
        opened.value = static_cast<int>(value);
        if (opened.mode == PinOutput)
            for (std::size_t i = 0; i < opened.pins.size(); ++i)
                pinInputs_[opened.pins[i]] = (opened.value >> (7 - i)) & 1;
        else if (opened.mode != PinInputOutput)
            throw ServiceError(ServiceErrorCode::Unavailable, "parallel channel is input-only");
        return;
    }
    if (opened.kind == Kind::I2c) {
        if (!reg || std::trunc(*reg) != *reg || *reg < 0 || *reg > 255 || value < 0 || value > 255)
            invalid("I2C scalar write requires 8-bit register and value");
        opened.registers[static_cast<unsigned>(*reg)] = {
            static_cast<std::uint8_t>(value)};
        return;
    }
    throw ServiceError(ServiceErrorCode::Unavailable, "device does not support scalar output");
}

std::size_t VirtualHardware::readBytes(runtime::SessionId session, runtime::ChannelId id,
                                       std::optional<double> reg, std::uint8_t* buffer,
                                       std::size_t capacity) {
    std::lock_guard lock(mutex_);
    Endpoint& opened = endpoint(session, id);
    if (opened.kind == Kind::I2c) {
        if (!reg || std::trunc(*reg) != *reg || *reg < 0 || *reg > 255)
            invalid("I2C buffer read requires an 8-bit register");
        const auto found = opened.registers.find(static_cast<unsigned>(*reg));
        if (found == opened.registers.end()) {
            std::fill_n(buffer, capacity, 0);
            return capacity;
        }
        const auto count = std::min(capacity, found->second.size());
        std::copy_n(found->second.data(), count, buffer);
        return count;
    }
    if (opened.kind == Kind::Serial) {
        const auto count = std::min(capacity, opened.rx.size());
        for (std::size_t i = 0; i < count; ++i) {
            buffer[i] = opened.rx.front();
            opened.rx.pop_front();
        }
        return count;
    }
    throw ServiceError(ServiceErrorCode::Unavailable, "device does not support byte input");
}

void VirtualHardware::writeBytes(runtime::SessionId session, runtime::ChannelId id,
                                 std::optional<double> reg, const std::uint8_t* buffer,
                                 std::size_t length) {
    std::lock_guard lock(mutex_);
    Endpoint& opened = endpoint(session, id);
    if (opened.kind == Kind::I2c) {
        if (!reg || std::trunc(*reg) != *reg || *reg < 0 || *reg > 255)
            invalid("I2C buffer write requires an 8-bit register");
        opened.registers[static_cast<unsigned>(*reg)] =
            std::vector<std::uint8_t>(buffer, buffer + length);
        return;
    }
    if (opened.kind == Kind::Serial && !reg) {
        opened.tx.append(reinterpret_cast<const char*>(buffer), length);
        return;
    }
    throw ServiceError(ServiceErrorCode::Unavailable, "device does not support byte output");
}

void VirtualHardware::transfer(runtime::SessionId session, runtime::ChannelId id,
                               const std::uint8_t* tx, std::size_t txLength,
                               std::uint8_t* rx, std::size_t rxLength) {
    std::lock_guard lock(mutex_);
    Endpoint& opened = endpoint(session, id);
    if (opened.kind != Kind::Spi && opened.kind != Kind::I2c)
        throw ServiceError(ServiceErrorCode::Unavailable, "device does not support XFER");
    for (std::size_t i = 0; i < rxLength; ++i)
        rx[i] = txLength == 0 ? 0 : tx[i % txLength];
}

std::optional<std::string> VirtualHardware::readLine(runtime::SessionId session,
                                                       runtime::ChannelId id) {
    std::lock_guard lock(mutex_);
    Endpoint& opened = endpoint(session, id);
    if (opened.kind != Kind::Serial)
        throw ServiceError(ServiceErrorCode::Unavailable, "device does not support line input");
    if (opened.lines.empty()) return std::nullopt;
    std::string line = std::move(opened.lines.front());
    opened.lines.pop_front();
    return line;
}

bool VirtualHardware::lineAvailable(runtime::SessionId session, runtime::ChannelId id) {
    std::lock_guard lock(mutex_);
    const Endpoint& opened = endpoint(session, id);
    if (opened.kind != Kind::Serial) return false;
    return !opened.lines.empty();
}

void VirtualHardware::writeText(runtime::SessionId session, runtime::ChannelId id,
                                std::string_view text) {
    std::lock_guard lock(mutex_);
    Endpoint& opened = endpoint(session, id);
    if (opened.kind != Kind::Serial)
        throw ServiceError(ServiceErrorCode::Unavailable, "device does not support text output");
    opened.tx.append(text.data(), text.size());
    if (serialLoopback_) {
        std::string line;
        for (char character : text) {
            if (character == '\r' || character == '\n') {
                if (!line.empty()) opened.lines.push_back(line);
                line.clear();
            } else line.push_back(character);
        }
        if (!line.empty()) opened.lines.push_back(std::move(line));
    }
}

void VirtualHardware::close(runtime::SessionId session, runtime::ChannelId id) {
    std::lock_guard lock(mutex_);
    const auto found = endpoints_.find(id);
    if (found == endpoints_.end() || found->second.session != session)
        throw ServiceError(ServiceErrorCode::InvalidHandle, "invalid hardware channel");
    for (const int pin : found->second.pins) {
        pinOwners_.erase(pin);
        pinInputs_[pin] = 0;
    }
    if (found->second.kind == Kind::I2c)
        i2cOwners_.erase({found->second.bus, found->second.address});
    endpoints_.erase(found);
}

void VirtualHardware::closeSession(runtime::SessionId session) {
    std::lock_guard lock(mutex_);
    for (auto it = endpoints_.begin(); it != endpoints_.end();) {
        if (it->second.session == session) {
            if (it->second.kind == Kind::I2c)
                i2cOwners_.erase({it->second.bus, it->second.address});
            for (const int pin : it->second.pins) {
                pinOwners_.erase(pin);
                pinInputs_[pin] = 0;
            }
            it = endpoints_.erase(it);
        } else ++it;
    }
}

runtime::ChannelType VirtualHardware::type(runtime::ChannelId id) const {
    std::lock_guard lock(mutex_);
    if (endpoints_.find(id) == endpoints_.end())
        throw ServiceError(ServiceErrorCode::InvalidHandle, "invalid hardware channel");
    return runtime::ChannelType::Hardware;
}

void VirtualHardware::setPinInput(int pin, int value) {
    std::lock_guard lock(mutex_);
    pinInputs_[pin] = value != 0;
}

void VirtualHardware::setAnalogInput(int pin, int value) {
    std::lock_guard lock(mutex_);
    analogInputs_[pin] = value;
}

void VirtualHardware::setTouchInput(int pin, int value) {
    std::lock_guard lock(mutex_);
    touchInputs_[pin] = value != 0;
}

void VirtualHardware::setSerialLoopback(bool enabled) {
    std::lock_guard lock(mutex_);
    serialLoopback_ = enabled;
}

void VirtualHardware::queueSerialLine(runtime::ChannelId channel, std::string line) {
    std::lock_guard lock(mutex_);
    const auto found = endpoints_.find(channel);
    if (found == endpoints_.end() || found->second.kind != Kind::Serial)
        throw ServiceError(ServiceErrorCode::InvalidHandle, "invalid virtual serial channel");
    found->second.lines.push_back(std::move(line));
}

std::string VirtualHardware::takeSerialOutput(runtime::ChannelId channel) {
    std::lock_guard lock(mutex_);
    const auto found = endpoints_.find(channel);
    if (found == endpoints_.end() || found->second.kind != Kind::Serial)
        throw ServiceError(ServiceErrorCode::InvalidHandle, "invalid virtual serial channel");
    std::string result;
    result.swap(found->second.tx);
    return result;
}

} // namespace ql32::platform::linux_host
