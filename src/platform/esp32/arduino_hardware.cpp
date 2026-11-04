#include "ql32/platform/esp32/arduino_hardware.hpp"

#include <algorithm>
#include <cmath>

namespace ql32::platform::esp32 {
namespace {
constexpr int PinInput = 0;
constexpr int PinOutput = 1;
constexpr int PinInputOutput = 2;
constexpr int PinModeMarker = 100;
constexpr int InitialValueMarker = 101;
bool validGpioPin(int pin) {
    return (pin >= 0 && pin <= 5) || (pin >= 12 && pin <= 19) ||
           (pin >= 21 && pin <= 23) || (pin >= 25 && pin <= 27) ||
           (pin >= 32 && pin <= 39);
}
bool outputCapableGpioPin(int pin) { return pin < 34; }
[[noreturn]] void invalid(const std::string& message) {
    throw runtime::ServiceError(runtime::ServiceErrorCode::InvalidPath, message);
}
} // namespace

ArduinoHardware::ArduinoHardware(SPIClass* spi0, SPIClass* spi1, TwoWire* i2c,
                                 int i2cSda, int i2cScl, int uart1Tx, int uart1Rx,
                                 int uart2Tx, int uart2Rx, std::vector<int> reservedPins)
    : spi0_(spi0), spi1_(spi1), i2c_(i2c), i2cSda_(i2cSda), i2cScl_(i2cScl),
      uart1Tx_(uart1Tx), uart1Rx_(uart1Rx), uart2Tx_(uart2Tx), uart2Rx_(uart2Rx) {
    pinOwners_[1] = 0; // USB/REPL UART0 TX
    pinOwners_[3] = 0; // USB/REPL UART0 RX
    for (int pin : reservedPins) pinOwners_[pin] = 0;
}

double ArduinoHardware::numeric(const runtime::DeviceArgument& argument, const char* field) {
    if (const auto* number = std::get_if<double>(&argument); number && std::isfinite(*number))
        return *number;
    invalid(std::string(field) + " must be numeric");
}

int ArduinoHardware::integer(const runtime::DeviceArgument& argument, const char* field,
                             int minimum, int maximum) {
    const double value = numeric(argument, field);
    if (std::trunc(value) != value || value < minimum || value > maximum)
        invalid(std::string("invalid ") + field);
    return static_cast<int>(value);
}

ArduinoHardware::Endpoint& ArduinoHardware::endpoint(runtime::SessionId session,
                                                       runtime::ChannelId id) {
    auto found = endpoints_.find(id);
    if (found == endpoints_.end() || found->second.session != session)
        throw runtime::ServiceError(runtime::ServiceErrorCode::InvalidHandle,
                                    "hardware channel is not open for this session");
    return found->second;
}

const ArduinoHardware::Endpoint& ArduinoHardware::endpoint(runtime::SessionId session,
                                                             runtime::ChannelId id) const {
    auto found = endpoints_.find(id);
    if (found == endpoints_.end() || found->second.session != session)
        throw runtime::ServiceError(runtime::ServiceErrorCode::InvalidHandle,
                                    "hardware channel is not open for this session");
    return found->second;
}

HardwareSerial* ArduinoHardware::serial(int port) const {
    if (port == 1) return &Serial1;
    if (port == 2) return &Serial2;
    return nullptr;
}

SPIClass* ArduinoHardware::spi(int bus) const {
    if (bus == 0) return spi0_;
    if (bus == 1) return spi1_;
    return nullptr;
}

void ArduinoHardware::initializeI2c(int clockHz) {
    if (!i2c_) throw runtime::ServiceError(runtime::ServiceErrorCode::Unavailable,
                                           "I2C adapter is unavailable");
    if (!i2cStarted_) {
        i2c_->begin(i2cSda_, i2cScl_, static_cast<std::uint32_t>(clockHz));
        i2cStarted_ = true;
        i2cClockHz_ = clockHz;
    } else if (i2cClockHz_ != clockHz) {
        throw runtime::ServiceError(runtime::ServiceErrorCode::Unavailable,
                                    "I2C bus is already open at a different clock rate");
    }
}

runtime::ChannelId ArduinoHardware::open(
    runtime::SessionId session, runtime::ChannelDevice device,
    const std::vector<runtime::DeviceArgument>& parameters) {
    std::lock_guard lock(mutex_);
    if (nextId_ == 0) throw runtime::ServiceError(runtime::ServiceErrorCode::Unavailable,
                                                   "hardware channel handles exhausted");
    Endpoint opened;
    opened.session = session;
    std::vector<int> pins;
    const auto count = [&](std::size_t minimum, std::size_t maximum) {
        if (parameters.size() < minimum || parameters.size() > maximum)
            invalid("incorrect hardware OPEN parameter count");
    };
    const auto reservePin = [&](int pin) {
        if (std::find(pins.begin(), pins.end(), pin) != pins.end())
            invalid("hardware OPEN contains duplicate pins");
        if (pinOwners_.find(pin) != pinOwners_.end())
            throw runtime::ServiceError(runtime::ServiceErrorCode::Unavailable,
                                        "hardware pin is already reserved");
        pins.push_back(pin);
    };
    switch (device) {
    case runtime::ChannelDevice::Gpio: {
        count(2, 5);
        opened.kind = Kind::Gpio;
        const int pin = integer(parameters[0], "GPIO pin", 0, 39);
        if (!validGpioPin(pin)) invalid("GPIO pin is not present on this ESP32");
        opened.mode = integer(parameters[1], "GPIO mode", PinInput, PinInputOutput);
        if (opened.mode == PinInputOutput)
            invalid("single GPIO channels require input or output mode");
        if (opened.mode == PinOutput && !outputCapableGpioPin(pin))
            invalid("GPIO pin is input-only");
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
        reservePin(pin);
        opened.pins = {pin};
        if (opened.mode == PinOutput) {
            pinMode(pin, INPUT);
            digitalWrite(pin, opened.value ? HIGH : LOW);
            pinMode(pin, OUTPUT);
        } else {
            pinMode(pin, opened.pull == 1 ? INPUT_PULLUP :
                         opened.pull == 2 ? INPUT_PULLDOWN : INPUT);
        }
        break;
    }
    case runtime::ChannelDevice::Adc: {
        count(1, 2);
        opened.kind = Kind::Adc;
        const int pin = integer(parameters[0], "ADC pin", 0, 39);
        if (!validGpioPin(pin)) invalid("ADC pin is not present on this ESP32");
        if (!(pin <= 4 || (pin >= 12 && pin <= 15) ||
              (pin >= 25 && pin <= 27) || (pin >= 32 && pin <= 39)))
            invalid("pin does not support ESP32 analog input");
        if (parameters.size() == 2) {
            const int bits = integer(parameters[1], "ADC resolution", 9, 12);
            analogReadResolution(bits);
        }
        reservePin(pin);
        pinMode(pin, INPUT);
        opened.pins = {pin};
        break;
    }
    case runtime::ChannelDevice::Spi: {
        count(4, 5);
        opened.kind = Kind::Spi;
        opened.bus = integer(parameters[0], "SPI bus", 0, 1);
        opened.chipSelect = integer(parameters[1], "SPI chip select", 0, 39);
        if (!validGpioPin(opened.chipSelect) || !outputCapableGpioPin(opened.chipSelect))
            invalid("SPI chip-select pin is not output-capable");
        opened.clockHz = integer(parameters[2], "SPI clock", 1, 40000000);
        opened.spiMode = integer(parameters[3], "SPI mode", 0, 3);
        opened.spiBitOrder = parameters.size() == 5 ?
            integer(parameters[4], "SPI bit order", 0, 1) : 0;
        if (!spi(opened.bus)) throw runtime::ServiceError(
            runtime::ServiceErrorCode::Unavailable, "SPI bus is unavailable");
        reservePin(opened.chipSelect);
        opened.pins = {opened.chipSelect};
        digitalWrite(opened.chipSelect, HIGH);
        pinMode(opened.chipSelect, OUTPUT);
        break;
    }
    case runtime::ChannelDevice::I2c: {
        count(2, 3);
        opened.kind = Kind::I2c;
        opened.bus = integer(parameters[0], "I2C bus", 0, 1);
        if (opened.bus != 0) throw runtime::ServiceError(
            runtime::ServiceErrorCode::Unavailable, "I2C bus is unavailable");
        opened.address = integer(parameters[1], "I2C address", 0x03, 0x77);
        if (i2cOwners_.find({opened.bus, opened.address}) != i2cOwners_.end())
            throw runtime::ServiceError(runtime::ServiceErrorCode::Unavailable,
                                        "I2C device address is already open");
        opened.clockHz = parameters.size() == 3 ?
            integer(parameters[2], "I2C clock", 10000, 1000000) : 100000;
        initializeI2c(opened.clockHz);
        break;
    }
    case runtime::ChannelDevice::Parallel: {
        count(10, 14);
        opened.kind = Kind::Parallel;
        for (std::size_t index = 0; index < 8; ++index) {
            const int pin = integer(parameters[index], "parallel pin", 0, 39);
            if (!validGpioPin(pin)) invalid("parallel pin is not present on this ESP32");
            reservePin(pin);
            opened.pins.push_back(pin);
        }
        std::size_t option = 8;
        if (integer(parameters[option++], "parallel KPinMode marker", PinModeMarker,
                    PinModeMarker) != PinModeMarker) invalid("missing KPinMode marker");
        opened.mode = integer(parameters[option++], "parallel direction", 0, 2);
        if (opened.mode != PinInput)
            for (int pin : opened.pins)
                if (!outputCapableGpioPin(pin)) invalid("parallel output contains an input-only pin");
        opened.directionSwitchable = opened.mode == PinInputOutput;
        if (option < parameters.size()) {
            if (integer(parameters[option++], "parallel initial marker", InitialValueMarker,
                        InitialValueMarker) != InitialValueMarker || option >= parameters.size())
                invalid("invalid parallel initial value option");
            opened.value = integer(parameters[option++], "parallel initial value", 0, 255);
            if (option != parameters.size()) invalid("unexpected parallel OPEN parameter");
        }
        for (const int pin : opened.pins) pinMode(pin, INPUT);
        for (std::size_t index = 0; index < 8; ++index)
            digitalWrite(opened.pins[index],
                         (opened.value >> (7 - index)) & 1 ? HIGH : LOW);
        if (opened.mode == PinOutput)
            for (const int pin : opened.pins) pinMode(pin, OUTPUT);
        break;
    }
    case runtime::ChannelDevice::Serial: {
        count(2, 4);
        opened.kind = Kind::Serial;
        opened.bus = integer(parameters[0], "UART port", 1, 2);
        const int baud = integer(parameters[1], "UART baud", 1, 4000000);
        if (opened.bus == 0) invalid("UART 0 is reserved for the REPL");
        auto* port = serial(opened.bus);
        if (!port) throw runtime::ServiceError(runtime::ServiceErrorCode::Unavailable,
                                               "UART is unavailable");
        const int frame = parameters.size() > 2 ? integer(parameters[2], "UART frame", 0, 0) : 0;
        if (parameters.size() > 3 &&
            integer(parameters[3], "UART flow control", 0, 2) != 0)
            throw runtime::ServiceError(runtime::ServiceErrorCode::Unavailable,
                                        "requested UART flow control is unavailable");
        const int tx = opened.bus == 1 ? uart1Tx_ : uart2Tx_;
        const int rx = opened.bus == 1 ? uart1Rx_ : uart2Rx_;
        reservePin(tx);
        reservePin(rx);
        opened.pins = {tx, rx};
        port->begin(baud, frame == 0 ? SERIAL_8N1 : SERIAL_8N1, rx, tx);
        break;
    }
    case runtime::ChannelDevice::CapTouch: {
        count(1, 1);
        opened.kind = Kind::CapTouch;
        const int pin = integer(parameters[0], "touch pad", 0, 39);
        if (!(pin == 0 || pin == 2 || pin == 4 || pin == 12 || pin == 13 ||
              pin == 14 || pin == 15 || pin == 27 || pin == 32 || pin == 33))
            invalid("pin does not support ESP32 capacitive touch");
        reservePin(pin);
        opened.pins = {pin};
        opened.touchBaseline = touchRead(pin);
        if (opened.touchBaseline <= 0) throw runtime::ServiceError(
            runtime::ServiceErrorCode::Unavailable, "capacitive touch pad is unavailable");
        break;
    }
    case runtime::ChannelDevice::Timer: {
        count(2, 2);
        opened.kind = Kind::Timer;
        opened.periodMs = integer(parameters[0], "timer period", 1, 86400000);
        opened.timerMode = integer(parameters[1], "timer mode", 0, 1);
        opened.nextExpirationMs = millis() + opened.periodMs;
        break;
    }
    default:
        throw runtime::ServiceError(runtime::ServiceErrorCode::Unavailable,
                                    "hardware device is unavailable");
    }
    const auto id = nextId_++;
    endpoints_.emplace(id, std::move(opened));
    const auto& stored = endpoints_.at(id);
    if (stored.kind == Kind::I2c) i2cOwners_.insert({stored.bus, stored.address});
    for (int pin : pins) pinOwners_[pin] = id;
    return id;
}

double ArduinoHardware::readValue(runtime::SessionId session, runtime::ChannelId id,
                                  std::optional<double> reg) {
    std::lock_guard lock(mutex_);
    Endpoint& opened = endpoint(session, id);
    switch (opened.kind) {
    case Kind::Gpio:
        if (opened.mode == PinOutput) throw runtime::ServiceError(
            runtime::ServiceErrorCode::Unavailable, "GPIO channel is output-only");
        return digitalRead(opened.pins.front()) == HIGH ? 1 : 0;
    case Kind::Adc: return analogRead(opened.pins.front());
    case Kind::Parallel: {
        if (opened.mode == PinOutput) throw runtime::ServiceError(
            runtime::ServiceErrorCode::Unavailable, "parallel channel is output-only");
        int value = 0;
        for (int pin : opened.pins) value = (value << 1) | (digitalRead(pin) == HIGH ? 1 : 0);
        return value;
    }
    case Kind::I2c: {
        if (!reg || std::trunc(*reg) != *reg || *reg < 0 || *reg > 255)
            invalid("I2C scalar read requires an 8-bit register");
        i2c_->beginTransmission(opened.address);
        i2c_->write(static_cast<std::uint8_t>(*reg));
        if (i2c_->endTransmission(false) != 0 || i2c_->requestFrom(opened.address, 1) != 1)
            throw runtime::ServiceError(runtime::ServiceErrorCode::IoError, "I2C read failed");
        return i2c_->read();
    }
    case Kind::CapTouch: {
        const int touched = touchRead(opened.pins.front()) < opened.touchBaseline * 0.7 ? 1 : 0;
        if (touched == opened.touchCandidate) ++opened.touchStableCount;
        else { opened.touchCandidate = touched; opened.touchStableCount = 1; }
        if (opened.touchStableCount >= 3) opened.touchState = touched;
        return opened.touchState;
    }
    case Kind::Timer: {
        const std::uint32_t now = millis();
        if (opened.periodMs > 0 && static_cast<std::int32_t>(now - opened.nextExpirationMs) >= 0) {
            const std::uint32_t elapsed = now - static_cast<std::uint32_t>(opened.nextExpirationMs);
            const int count = 1 + static_cast<int>(elapsed / opened.periodMs);
            opened.value += count;
            if (opened.timerMode == 0) opened.periodMs = 0;
            else opened.nextExpirationMs += static_cast<std::uint64_t>(count) * opened.periodMs;
        }
        const int count = opened.value;
        opened.value = 0;
        return count;
    }
    default:
        throw runtime::ServiceError(runtime::ServiceErrorCode::Unavailable,
                                    "device does not support scalar input");
    }
}

void ArduinoHardware::writeValue(runtime::SessionId session, runtime::ChannelId id,
                                 double value, std::optional<double> reg) {
    if (!std::isfinite(value) || std::trunc(value) != value)
        invalid("device output must be an integer");
    std::lock_guard lock(mutex_);
    Endpoint& opened = endpoint(session, id);
    if (opened.kind == Kind::Gpio) {
        if (opened.mode == PinInput) throw runtime::ServiceError(
            runtime::ServiceErrorCode::Unavailable, "GPIO channel is input-only");
        if (value < 0 || value > 1) invalid("GPIO output must be 0 or 1");
        digitalWrite(opened.pins.front(), value ? HIGH : LOW);
        return;
    }
    if (opened.kind == Kind::Parallel) {
        if (reg && *reg == PinModeMarker) {
            const int mode = static_cast<int>(value);
            if (!opened.directionSwitchable || (mode != PinInput && mode != PinOutput))
                invalid("parallel direction change is not allowed");
            if (mode == PinOutput)
                for (std::size_t i = 0; i < 8; ++i)
                    digitalWrite(opened.pins[i], (opened.value >> (7 - i)) & 1 ? HIGH : LOW);
            for (int pin : opened.pins) pinMode(pin, mode == PinOutput ? OUTPUT : INPUT);
            opened.mode = mode;
            return;
        }
        if (value < 0 || value > 255) invalid("parallel output must be 0 through 255");
        opened.value = static_cast<int>(value);
        if (opened.mode == PinOutput)
            for (std::size_t i = 0; i < 8; ++i)
                digitalWrite(opened.pins[i], (opened.value >> (7 - i)) & 1 ? HIGH : LOW);
        else if (opened.mode != PinInputOutput)
            throw runtime::ServiceError(runtime::ServiceErrorCode::Unavailable,
                                        "parallel channel is input-only");
        return;
    }
    if (opened.kind == Kind::I2c) {
        if (!reg || std::trunc(*reg) != *reg || *reg < 0 || *reg > 255 || value < 0 || value > 255)
            invalid("I2C scalar write requires 8-bit register and value");
        i2c_->beginTransmission(opened.address);
        i2c_->write(static_cast<std::uint8_t>(*reg));
        i2c_->write(static_cast<std::uint8_t>(value));
        if (i2c_->endTransmission() != 0)
            throw runtime::ServiceError(runtime::ServiceErrorCode::IoError, "I2C write failed");
        return;
    }
    throw runtime::ServiceError(runtime::ServiceErrorCode::Unavailable,
                                "device does not support scalar output");
}

std::size_t ArduinoHardware::readBytes(runtime::SessionId session, runtime::ChannelId id,
                                       std::optional<double> reg, std::uint8_t* buffer,
                                       std::size_t capacity) {
    std::lock_guard lock(mutex_);
    Endpoint& opened = endpoint(session, id);
    if (opened.kind == Kind::I2c) {
        if (!reg || std::trunc(*reg) != *reg || *reg < 0 || *reg > 255)
            invalid("I2C buffer read requires an 8-bit register");
        i2c_->beginTransmission(opened.address);
        i2c_->write(static_cast<std::uint8_t>(*reg));
        if (i2c_->endTransmission(false) != 0)
            throw runtime::ServiceError(runtime::ServiceErrorCode::IoError, "I2C register select failed");
        const auto requested = static_cast<std::uint8_t>(std::min<std::size_t>(capacity, 255));
        const auto available = i2c_->requestFrom(
            static_cast<std::uint8_t>(opened.address), requested);
        std::size_t count = 0;
        while (count < available && count < capacity && i2c_->available())
            buffer[count++] = static_cast<std::uint8_t>(i2c_->read());
        return count;
    }
    if (opened.kind == Kind::Serial && !reg) {
        auto* port = serial(opened.bus);
        std::size_t count = 0;
        while (port && port->available() > 0 && count < capacity)
            buffer[count++] = static_cast<std::uint8_t>(port->read());
        return count;
    }
    throw runtime::ServiceError(runtime::ServiceErrorCode::Unavailable,
                                "device does not support byte input");
}

void ArduinoHardware::writeBytes(runtime::SessionId session, runtime::ChannelId id,
                                 std::optional<double> reg, const std::uint8_t* buffer,
                                 std::size_t length) {
    std::lock_guard lock(mutex_);
    Endpoint& opened = endpoint(session, id);
    if (opened.kind == Kind::I2c) {
        if (!reg || std::trunc(*reg) != *reg || *reg < 0 || *reg > 255)
            invalid("I2C buffer write requires an 8-bit register");
        i2c_->beginTransmission(opened.address);
        i2c_->write(static_cast<std::uint8_t>(*reg));
        const auto sent = i2c_->write(buffer, length);
        const auto result = i2c_->endTransmission();
        if (sent != length || result != 0)
            throw runtime::ServiceError(runtime::ServiceErrorCode::IoError, "I2C buffer write failed");
        return;
    }
    if (opened.kind == Kind::Serial && !reg) {
        auto* port = serial(opened.bus);
        if (!port || port->write(buffer, length) != length)
            throw runtime::ServiceError(runtime::ServiceErrorCode::IoError, "UART write failed");
        return;
    }
    throw runtime::ServiceError(runtime::ServiceErrorCode::Unavailable,
                                "device does not support byte output");
}

void ArduinoHardware::transfer(runtime::SessionId session, runtime::ChannelId id,
                               const std::uint8_t* tx, std::size_t txLength,
                               std::uint8_t* rx, std::size_t rxLength) {
    std::lock_guard lock(mutex_);
    Endpoint& opened = endpoint(session, id);
    if (opened.kind == Kind::Spi) {
        SPIClass* bus = spi(opened.bus);
        if (!bus) throw runtime::ServiceError(runtime::ServiceErrorCode::Unavailable,
                                              "SPI bus is unavailable");
        bus->beginTransaction(SPISettings(opened.clockHz,
            opened.spiBitOrder == 0 ? MSBFIRST : LSBFIRST, opened.spiMode));
        digitalWrite(opened.chipSelect, LOW);
        struct TransactionGuard {
            SPIClass* bus;
            int chipSelect;
            ~TransactionGuard() {
                digitalWrite(chipSelect, HIGH);
                bus->endTransaction();
            }
        } guard{bus, opened.chipSelect};
        for (std::size_t i = 0; i < txLength; ++i) bus->transfer(tx[i]);
        for (std::size_t i = 0; i < rxLength; ++i) rx[i] = bus->transfer(0);
        return;
    }
    if (opened.kind == Kind::I2c) {
        if (txLength > 255 || rxLength > 255)
            invalid("I2C raw transfer is limited to 255 bytes per phase");
        if (txLength) {
            i2c_->beginTransmission(opened.address);
            const auto sent = i2c_->write(tx, txLength);
            const auto result = i2c_->endTransmission(rxLength != 0);
            if (sent != txLength || result != 0)
                throw runtime::ServiceError(runtime::ServiceErrorCode::IoError, "I2C transfer write failed");
        }
        if (rxLength) {
            const auto requested = static_cast<std::uint8_t>(std::min<std::size_t>(rxLength, 255));
            const auto available = i2c_->requestFrom(
                static_cast<std::uint8_t>(opened.address), requested);
            for (std::size_t i = 0; i < rxLength; ++i)
                rx[i] = i < available && i2c_->available() ? static_cast<std::uint8_t>(i2c_->read()) : 0;
        }
        return;
    }
    throw runtime::ServiceError(runtime::ServiceErrorCode::Unavailable,
                                "device does not support XFER");
}

std::optional<std::string> ArduinoHardware::readLine(runtime::SessionId session,
                                                      runtime::ChannelId id) {
    std::lock_guard lock(mutex_);
    Endpoint& opened = endpoint(session, id);
    if (opened.kind != Kind::Serial)
        throw runtime::ServiceError(runtime::ServiceErrorCode::Unavailable,
                                    "device does not support line input");
    auto* port = serial(opened.bus);
    while (port && port->available() > 0) {
        const char character = static_cast<char>(port->read());
        if (opened.skipNextLineFeed) {
            opened.skipNextLineFeed = false;
            if (character == '\n') continue;
        }
        if (character == '\r' || character == '\n') {
            std::string line;
            line.swap(opened.lineBuffer);
            opened.skipNextLineFeed = character == '\r';
            return line;
        }
        opened.lineBuffer.push_back(character);
    }
    return std::nullopt;
}

bool ArduinoHardware::lineAvailable(runtime::SessionId session, runtime::ChannelId id) {
    std::lock_guard lock(mutex_);
    const Endpoint& opened = endpoint(session, id);
    return opened.kind == Kind::Serial && serial(opened.bus) &&
           serial(opened.bus)->available() > 0;
}

void ArduinoHardware::writeText(runtime::SessionId session, runtime::ChannelId id,
                                std::string_view text) {
    std::lock_guard lock(mutex_);
    Endpoint& opened = endpoint(session, id);
    if (opened.kind != Kind::Serial)
        throw runtime::ServiceError(runtime::ServiceErrorCode::Unavailable,
                                    "device does not support text output");
    auto* port = serial(opened.bus);
    if (!port || port->write(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()) != text.size())
        throw runtime::ServiceError(runtime::ServiceErrorCode::IoError, "UART text write failed");
}

void ArduinoHardware::releasePins(const Endpoint& opened) {
    for (int pin : opened.pins) {
        pinOwners_.erase(pin);
        pinMode(pin, INPUT);
    }
}

void ArduinoHardware::close(runtime::SessionId session, runtime::ChannelId id) {
    std::lock_guard lock(mutex_);
    auto found = endpoints_.find(id);
    if (found == endpoints_.end() || found->second.session != session)
        throw runtime::ServiceError(runtime::ServiceErrorCode::InvalidHandle,
                                    "invalid hardware channel");
    Endpoint opened = found->second;
    if (opened.kind == Kind::I2c) i2cOwners_.erase({opened.bus, opened.address});
    if (opened.kind == Kind::Serial) {
        if (auto* port = serial(opened.bus)) port->end();
    }
    releasePins(opened);
    endpoints_.erase(found);
}

void ArduinoHardware::closeSession(runtime::SessionId session) {
    std::lock_guard lock(mutex_);
    for (auto it = endpoints_.begin(); it != endpoints_.end();) {
        if (it->second.session != session) { ++it; continue; }
        if (it->second.kind == Kind::I2c)
            i2cOwners_.erase({it->second.bus, it->second.address});
        if (it->second.kind == Kind::Serial)
            if (auto* port = serial(it->second.bus)) port->end();
        releasePins(it->second);
        it = endpoints_.erase(it);
    }
}

runtime::ChannelType ArduinoHardware::type(runtime::ChannelId id) const {
    std::lock_guard lock(mutex_);
    if (endpoints_.find(id) == endpoints_.end())
        throw runtime::ServiceError(runtime::ServiceErrorCode::InvalidHandle,
                                    "invalid hardware channel");
    return runtime::ChannelType::Hardware;
}

} // namespace ql32::platform::esp32
