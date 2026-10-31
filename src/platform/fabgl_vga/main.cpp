#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <driver/gpio.h>
#include <fabgl.h>
#include <cstdint>
#include <memory>
#include <ostream>
#include <optional>
#include <streambuf>

#include "capabilities.hpp"
#include "pins.hpp"
#include "ql32/interpreter.hpp"
#include "ql32/platform/esp32/sd_channels.hpp"
#include "ql32/platform/esp32/clock.hpp"
#include "ql32/platform/esp32/sd_storage.hpp"
#include "ql32/platform/fabgl_vga/graphics.hpp"
#include "ql32/platform/fabgl_vga/keyboard.hpp"

namespace {

class SerialOutputBuffer final : public std::streambuf {
protected:
    int_type overflow(int_type character) override {
        if (traits_type::eq_int_type(character, traits_type::eof()))
            return traits_type::not_eof(character);
        return Serial.write(static_cast<std::uint8_t>(character)) == 1
            ? character : traits_type::eof();
    }

    std::streamsize xsputn(const char* data, std::streamsize size) override {
        if (size <= 0) return 0;
        return static_cast<std::streamsize>(Serial.write(
            reinterpret_cast<const std::uint8_t*>(data), static_cast<std::size_t>(size)));
    }
};

fabgl::VGAController display;
fabgl::Canvas canvas(&display);
fabgl::PS2Controller ps2;
ql32::platform::fabgl_vga::FabglGraphics graphics(canvas);
std::optional<ql32::platform::fabgl_vga::FabglKeyboard> keyboardInput;
ql32::platform::esp32::Clock boardClock;
SPIClass sdBus(HSPI);
ql32::platform::esp32::SdStorage sdStorage(SD);
ql32::platform::esp32::SdChannels sdChannels(SD);
SerialOutputBuffer serialBuffer;
std::ostream serialOutput(&serialBuffer);
std::unique_ptr<ql32::Interpreter> interpreter;
std::string inputLine;
bool serialSawCarriageReturn = false;

void appendInputCharacter(char character) {
    if (inputLine.size() >= 256) return;
    inputLine.push_back(character);
    Serial.write(static_cast<std::uint8_t>(character));
}

void eraseInputCharacter() {
    if (inputLine.empty()) return;
    inputLine.pop_back();
    Serial.print("\b \b");
}

void submitInputLine() {
    Serial.println();
    if (interpreter && !inputLine.empty()) {
        try {
            interpreter->executeInteractive(inputLine + "\n", serialOutput);
        } catch (const std::exception& error) {
            Serial.printf("%s\n", error.what());
        }
    }
    inputLine.clear();
    Serial.print("> ");
}

void receiveSerialCharacter(char character) {
    if (character == '\n' && serialSawCarriageReturn) {
        serialSawCarriageReturn = false;
        return;
    }
    if (character == '\r' || character == '\n') {
        serialSawCarriageReturn = character == '\r';
        submitInputLine();
        return;
    }
    serialSawCarriageReturn = false;
    if (character == '\b' || character == 127) {
        eraseInputCharacter();
    } else if (character >= 32 && character <= 126) {
        appendInputCharacter(character);
    }
}

} // namespace

void setup() {
    Serial.begin(115200);

    if (!ql32::boards::lilygo_vga32::capabilities().valid()) {
        Serial.println("QL/32 VGA32 capability profile invalid");
        return;
    }

    display.begin(
        static_cast<gpio_num_t>(ql32::boards::lilygo_vga32::vgaRed),
        static_cast<gpio_num_t>(ql32::boards::lilygo_vga32::vgaGreen),
        static_cast<gpio_num_t>(ql32::boards::lilygo_vga32::vgaBlue),
        static_cast<gpio_num_t>(ql32::boards::lilygo_vga32::vgaHSync),
        static_cast<gpio_num_t>(ql32::boards::lilygo_vga32::vgaVSync));
    display.setResolution(VGA_320x200_70Hz);

    ps2.begin(fabgl::PS2Preset::KeyboardPort0);
    if (fabgl::PS2Controller::keyboard())
        keyboardInput.emplace(*fabgl::PS2Controller::keyboard());

    sdBus.begin(ql32::boards::lilygo_vga32::sdClock,
                ql32::boards::lilygo_vga32::sdMiso,
                ql32::boards::lilygo_vga32::sdMosi,
                ql32::boards::lilygo_vga32::sdChipSelect);
    const bool sdReady = SD.begin(ql32::boards::lilygo_vga32::sdChipSelect,
                                 sdBus, 4000000, "/sd", 5);

    constexpr ql32::graphics::SessionId demoSession = 1;
    graphics.setPaper(demoSession, 0);
    graphics.clear(demoSession);
    graphics.setInk(demoSession, 7);
    graphics.text(demoSession, {8, 8}, "QL/32 VGA32 V1.4");
    graphics.text(demoSession, {8, 24}, "FabGL VGA + PS/2 ready");
    graphics.text(demoSession, {8, 36}, "BASIC prompt on PS/2 keyboard");
    graphics.rectangle(demoSession, {8, 48}, 96, 48);
    graphics.line(demoSession, {8, 48}, {103, 95});
    graphics.circle(demoSession, {150, 72}, 24);

    Serial.println("QL/32 VGA32 V1.4 bring-up ready");
    Serial.printf("Uptime ms: %llu\n",
                  static_cast<unsigned long long>(boardClock.monotonicMilliseconds()));
    const auto wallTime = boardClock.unixTimeSeconds();
    if (wallTime)
        Serial.printf("Unix time: %lld\n", static_cast<long long>(*wallTime));
    else
        Serial.println("Wall clock unsynchronized");
    Serial.println(sdReady ? "SD storage ready" : "SD storage unavailable");

    auto graphicsService = std::shared_ptr<ql32::graphics::Service>(
        &graphics, [](ql32::graphics::Service*) {});
    auto storageService = std::shared_ptr<ql32::runtime::StorageService>(
        &sdStorage, [](ql32::runtime::StorageService*) {});
    auto channelService = std::shared_ptr<ql32::runtime::ChannelService>(
        &sdChannels, [](ql32::runtime::ChannelService*) {});
    interpreter = std::make_unique<ql32::Interpreter>(
        storageService, channelService, nullptr, graphicsService);
    std::string startupProgram =
        "INK(7)\nTEXT(8,112,\"BASIC VM ready\")\n"
        "PRINT \"QL/32 BASIC VM running\"\n";
    if (sdReady) {
        try {
            startupProgram = sdStorage.readText("startup.bas");
            Serial.println("Running SD:/startup.bas");
        } catch (const ql32::runtime::ServiceError& error) {
            if (error.code() != ql32::runtime::ServiceErrorCode::NotFound) {
                Serial.printf("SD startup read error: %s\n", error.what());
                return;
            }
            Serial.println("SD:/startup.bas not found; running built-in demo");
        }
    }
    try {
        interpreter->execute(startupProgram, serialOutput);
    } catch (const std::exception& error) {
        Serial.printf("BASIC VM error: %s\n", error.what());
    }
    Serial.println("BASIC REPL ready (USB serial and PS/2 keyboard)");
    Serial.print("> ");
}

void loop() {
    while (Serial.available() > 0) {
        receiveSerialCharacter(static_cast<char>(Serial.read()));
    }

    if (keyboardInput) {
        const auto event = keyboardInput->poll();
        if (event && event->pressed) {
            if (event->key == ql32::input::Key::Character &&
                event->character >= 32 && event->character <= 126) {
                appendInputCharacter(static_cast<char>(event->character));
            } else if (event->key == ql32::input::Key::Backspace && !inputLine.empty()) {
                eraseInputCharacter();
            } else if (event->key == ql32::input::Key::Enter) {
                submitInputLine();
            }
        }
    }
    delay(1);
}
