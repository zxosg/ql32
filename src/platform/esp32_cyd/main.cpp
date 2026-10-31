#include <Arduino.h>
#include <Adafruit_ILI9341.h>
#include <SD.h>
#include <SPI.h>

#include <cstdint>
#include <memory>
#include <ostream>

#include "capabilities.hpp"
#include "pins.hpp"
#include "ql32/interpreter.hpp"
#include "ql32/platform/esp32/clock.hpp"
#include "ql32/platform/esp32/line_ui_presenter.hpp"
#include "ql32/platform/esp32/network.hpp"
#include "ql32/platform/esp32/sd_channels.hpp"
#include "ql32/platform/esp32/sd_storage.hpp"
#include "ql32/platform/esp32_cyd/graphics.hpp"
#include "ql32/platform/esp32_cyd/text_console.hpp"

namespace {

SPIClass tftBus(HSPI);
SPIClass sdBus(VSPI);
ql32::platform::esp32_cyd::CydIli9341Display display(&tftBus,
    ql32::boards::esp32_2432s028::tftDataCommand,
    ql32::boards::esp32_2432s028::tftChipSelect, -1);
ql32::platform::esp32_cyd::Ili9341Graphics graphics(display);
ql32::platform::esp32::Clock boardClock;
ql32::platform::esp32::WifiNetworkService boardNetwork;
ql32::platform::esp32::SdStorage sdStorage(SD);
ql32::platform::esp32::SdChannels sdChannels(SD);
std::unique_ptr<ql32::platform::esp32_cyd::TftTextConsole> tftConsole;
std::unique_ptr<std::ostream> basicOutput;
std::unique_ptr<ql32::Interpreter> interpreter;
std::shared_ptr<ql32::ui::QueuedService> uiService =
    std::make_shared<ql32::ui::QueuedService>();
std::unique_ptr<ql32::platform::esp32::LineUiPresenter> uiPresenter;
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

bool resumePendingExecution() {
    try {
        const auto status = interpreter->resume(*basicOutput);
        if (status == ql32::ExecutionStatus::Completed) return true;
        if (uiPresenter->beginNext()) return false;
        throw std::runtime_error("suspended VM has no queued UI request");
    } catch (const std::exception& error) {
        Serial.printf("%s\n", error.what());
        *basicOutput << error.what() << '\n';
        return true;
    }
}

void submitInputLine() {
    Serial.println();
    bool showPrompt = true;
    if (interpreter && (!inputLine.empty() || (uiPresenter && uiPresenter->active()))) {
        try {
            if (uiPresenter->active()) {
                if (uiPresenter->consumeLine(inputLine))
                    showPrompt = resumePendingExecution();
                else
                    showPrompt = false;
            } else {
                const auto status = interpreter->executeInteractiveResumable(
                    inputLine + "\n", *basicOutput);
                if (status == ql32::ExecutionStatus::Suspended) {
                    if (uiPresenter->beginNext()) showPrompt = false;
                    else throw std::runtime_error("suspended VM has no queued UI request");
                }
            }
        } catch (const std::exception& error) {
            Serial.printf("%s\n", error.what());
            *basicOutput << error.what() << '\n';
        }
    }
    inputLine.clear();
    if (showPrompt && (!uiPresenter || !uiPresenter->active())) Serial.print("> ");
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
    if (character == '\b' || character == 127) eraseInputCharacter();
    else if (character >= 32 && character <= 126) appendInputCharacter(character);
}

} // namespace

void setup() {
    Serial.begin(115200);

    if (!ql32::boards::esp32_2432s028::capabilities().valid()) {
        Serial.println("QL/32 ESP32-2432S028 capability profile invalid");
        return;
    }

    pinMode(ql32::boards::esp32_2432s028::tftBacklight, OUTPUT);
    digitalWrite(ql32::boards::esp32_2432s028::tftBacklight, HIGH);
    tftBus.begin(ql32::boards::esp32_2432s028::tftSck,
                 ql32::boards::esp32_2432s028::tftMiso,
                 ql32::boards::esp32_2432s028::tftMosi,
                 ql32::boards::esp32_2432s028::tftChipSelect);
    display.begin(40000000);
    graphics.setRotation(1, 0);
    display.fillScreen(ILI9341_BLACK);
    tftConsole = std::make_unique<ql32::platform::esp32_cyd::TftTextConsole>(display);
    basicOutput = std::make_unique<std::ostream>(tftConsole.get());
    graphics.setTextClearCallback([] {
        if (tftConsole) tftConsole->clear();
    });

    sdBus.begin(ql32::boards::esp32_2432s028::sdSck,
                ql32::boards::esp32_2432s028::sdMiso,
                ql32::boards::esp32_2432s028::sdMosi,
                ql32::boards::esp32_2432s028::sdChipSelect);
    const bool sdReady = SD.begin(ql32::boards::esp32_2432s028::sdChipSelect,
                                 sdBus, 4000000, "/sd", 5);

    constexpr ql32::graphics::SessionId demoSession = 1;
    graphics.setPaper(demoSession, 0);
    graphics.clear(demoSession);
    graphics.setInk(demoSession, 7);
    graphics.text(demoSession, {8, 8}, "QL/32 ESP32-2432S028");
    graphics.text(demoSession, {8, 24}, "ILI9341 TFT ready");
    graphics.text(demoSession, {8, 40}, "BASIC input via USB serial");
    graphics.rectangle(demoSession, {8, 58}, 100, 50);
    graphics.line(demoSession, {8, 58}, {107, 107});
    graphics.circle(demoSession, {160, 82}, 24);

    Serial.println("QL/32 ESP32-2432S028 ready");
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
    auto networkService = std::shared_ptr<ql32::runtime::NetworkService>(
        &boardNetwork, [](ql32::runtime::NetworkService*) {});
    uiPresenter = std::make_unique<ql32::platform::esp32::LineUiPresenter>(
        uiService, *basicOutput);
    interpreter = std::make_unique<ql32::Interpreter>(
        storageService, channelService, uiService, graphicsService, networkService);

    std::string startupProgram =
        "INK 7\nTEXT 8,132,\"BASIC VM ready\"\n"
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
        const auto status = interpreter->executeResumable(startupProgram, *basicOutput);
        if (status == ql32::ExecutionStatus::Suspended) {
            if (!uiPresenter->beginNext())
                throw std::runtime_error("suspended VM has no queued UI request");
        }
    } catch (const std::exception& error) {
        Serial.printf("BASIC VM error: %s\n", error.what());
        *basicOutput << "BASIC VM error: " << error.what() << '\n';
    }
    Serial.println("BASIC REPL ready (USB serial)");
    if (!uiPresenter->active()) Serial.print("> ");
}

void loop() {
    while (Serial.available() > 0)
        receiveSerialCharacter(static_cast<char>(Serial.read()));
    delay(1);
}
