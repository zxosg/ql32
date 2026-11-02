#include <Arduino.h>
#include <Adafruit_ILI9341.h>
#include <SD.h>
#if defined(QL32_CYD_USE_SPIFFS)
#include <SPIFFS.h>
#endif
#include <SPI.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <ostream>
#include <string_view>
#include <utility>

#include "capabilities.hpp"
#include "pins.hpp"
#include "ql32/graphics/windowed_service.hpp"
#include "ql32/vm/scheduler.hpp"
#include "ql32/platform/esp32/clock.hpp"
#include "ql32/platform/esp32/cpu_frequency.hpp"
#include "ql32/platform/esp32/line_ui_presenter.hpp"
#include "ql32/platform/esp32/memory.hpp"
#include "ql32/platform/esp32/network.hpp"
#include "ql32/platform/esp32/sd_channels.hpp"
#include "ql32/platform/esp32/sd_storage.hpp"
#include "ql32/platform/esp32/system_control.hpp"
#include "ql32/platform/esp32_cyd/graphics.hpp"
#include "ql32/platform/esp32_cyd/frame_presenter.hpp"
#include "ql32/platform/esp32_cyd/text_console.hpp"

namespace {

SPIClass tftBus(HSPI);
SPIClass sdBus(VSPI);
ql32::platform::esp32_cyd::CydIli9341Display display(&tftBus,
    ql32::boards::esp32_2432s028::tftDataCommand,
    ql32::boards::esp32_2432s028::tftChipSelect, -1);
std::shared_ptr<ql32::graphics::WindowedService> graphics;
ql32::platform::esp32::Clock boardClock;
ql32::platform::esp32::CpuFrequency boardCpuFrequency;
ql32::platform::esp32::MemoryService boardMemory;
ql32::platform::esp32::WifiNetworkService boardNetwork;
ql32::platform::esp32::SystemControl boardSystemControl;
#if defined(QL32_CYD_USE_SPIFFS)
ql32::platform::esp32::SdStorage sdStorage(SPIFFS);
ql32::platform::esp32::SdChannels sdChannels(SPIFFS);
#else
ql32::platform::esp32::SdStorage sdStorage(SD);
ql32::platform::esp32::SdChannels sdChannels(SD);
#endif
std::unique_ptr<ql32::platform::esp32_cyd::TftTextConsole> tftConsole;
std::unique_ptr<std::ostream> basicOutput;
std::unique_ptr<ql32::vm::SessionScheduler> scheduler;
ql32::vm::TaskId replTask = 0;
std::shared_ptr<ql32::ui::QueuedService> uiService =
    std::make_shared<ql32::ui::QueuedService>();
std::unique_ptr<ql32::platform::esp32::LineUiPresenter> uiPresenter;
std::string inputLine;
bool serialSawCarriageReturn = false;
bool replReadyMessageShown = false;
bool replPromptShown = false;

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
    if (uiPresenter && uiPresenter->active()) {
        (void)uiPresenter->consumeLine(inputLine);
    } else if (scheduler && !inputLine.empty() &&
               !scheduler->submit(scheduler->focusedTask(), inputLine + "\n")) {
        Serial.println("BASIC session is busy");
    }
    inputLine.clear();
    replPromptShown = false;
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

bool acceptsInput() {
    if (uiPresenter && uiPresenter->active()) return true;
    if (!scheduler) return false;
    const auto state = scheduler->task(scheduler->focusedTask());
    return state && state->status == ql32::vm::TaskStatus::WaitingInput;
}

void advanceScheduler() {
    if (!scheduler) return;
    (void)scheduler->tick();

    const auto tasks = scheduler->tasks();
    for (const auto& task : tasks) {
        const std::string error = scheduler->takeError(task.id);
        if (!error.empty()) {
            Serial.printf("BASIC session %llu VM error: %s\n",
                          static_cast<unsigned long long>(task.id), error.c_str());
            if (basicOutput)
                *basicOutput << "BASIC session " << task.id << " VM error: " << error << '\n';
        }
    }

    const bool sessionWaitingForUi = std::any_of(tasks.begin(), tasks.end(),
        [](const ql32::vm::TaskInfo& task) {
            return task.status == ql32::vm::TaskStatus::WaitingUi;
        });
    if (uiPresenter && !uiPresenter->active() && !uiPresenter->beginNext() &&
        sessionWaitingForUi)
        Serial.println("suspended session has no queued UI request");

    const auto focused = scheduler->focusedTask();
    const auto state = scheduler->task(focused);
    if (!state) return;
    if (state->status == ql32::vm::TaskStatus::WaitingInput &&
        (!uiPresenter || !uiPresenter->active())) {
        if (!replReadyMessageShown) {
            Serial.println("BASIC REPL ready (USB serial)");
            replReadyMessageShown = true;
        }
        if (!replPromptShown) {
            if (focused == replTask) Serial.print("> ");
            else Serial.printf("task %llu> ", static_cast<unsigned long long>(focused));
            replPromptShown = true;
        }
    } else {
        replPromptShown = false;
    }
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
    display.setRotationDegrees(0);
    display.fillScreen(ILI9341_BLACK);
    tftConsole = std::make_unique<ql32::platform::esp32_cyd::TftTextConsole>(display);
    basicOutput = std::make_unique<std::ostream>(tftConsole.get());
    auto framePresenter = std::make_shared<ql32::platform::esp32_cyd::Ili9341FramePresenter>(
        display,
        [](ql32::graphics::Rect region) {
            if (tftConsole) tftConsole->restoreOverlay(region);
        },
        [] { if (tftConsole) tftConsole->reconfigure(); });
    graphics = std::make_shared<ql32::graphics::WindowedService>(
        display.width(), display.height(), framePresenter, 4, 76800);
    graphics->setDefaultWindowBounds(0, {
        0, std::max<std::int32_t>(0, static_cast<std::int32_t>(display.height()) - 64),
        static_cast<std::uint32_t>(display.width()),
        static_cast<std::uint32_t>(std::min<std::int32_t>(64, display.height()))});
    graphics->setConsoleBoundsCallback([](ql32::graphics::Rect bounds) {
        if (tftConsole) tftConsole->setBounds(bounds);
    });
    graphics->setConsoleColorsCallback([](ql32::graphics::ColorIndex ink,
                                          ql32::graphics::ColorIndex paper) {
        if (tftConsole) tftConsole->setColors(ink, paper);
    });
    graphics->setConsoleClearCallback([] {
        if (tftConsole) tftConsole->clear();
    });

#if defined(QL32_CYD_USE_SPIFFS)
    const bool storageReady = SPIFFS.begin(true);
    if (storageReady && !SPIFFS.exists("/supervisor"))
        (void)SPIFFS.mkdir("/supervisor");
#else
    sdBus.begin(ql32::boards::esp32_2432s028::sdSck,
                ql32::boards::esp32_2432s028::sdMiso,
                ql32::boards::esp32_2432s028::sdMosi,
                ql32::boards::esp32_2432s028::sdChipSelect);
    const bool storageReady = SD.begin(ql32::boards::esp32_2432s028::sdChipSelect,
                                      sdBus, 4000000, "/sd", 5);
#endif

    Serial.println("QL/32 ESP32-2432S028 ready");
    Serial.printf("Uptime ms: %llu\n",
                  static_cast<unsigned long long>(boardClock.monotonicMilliseconds()));
    const auto wallTime = boardClock.unixTimeSeconds();
    if (wallTime)
        Serial.printf("Unix time: %lld\n", static_cast<long long>(*wallTime));
    else
        Serial.println("Wall clock unsynchronized");
    Serial.println(storageReady ? "Storage ready" : "Storage unavailable");

    std::string startupProgram;
    if (storageReady) {
#if defined(QL32_CYD_USE_SPIFFS)
        if (SPIFFS.exists("/startup.bas")) {
            try {
                startupProgram = sdStorage.readText("startup.bas");
                Serial.println("Running Storage:/startup.bas");
            } catch (const ql32::runtime::ServiceError& error) {
                Serial.printf("Storage startup read error: %s\n", error.what());
                return;
            }
        } else {
            Serial.println("Storage:/startup.bas not found; starting with a clear display");
        }
#else
        try {
            startupProgram = sdStorage.readText("startup.bas");
            Serial.println("Running SD:/startup.bas");
        } catch (const ql32::runtime::ServiceError& error) {
            if (error.code() != ql32::runtime::ServiceErrorCode::NotFound) {
                Serial.printf("Storage startup read error: %s\n", error.what());
                return;
            }
            Serial.println("SD:/startup.bas not found; starting with a clear display");
        }
#endif
    }

    ql32::vm::Services services;
    services.storage = std::shared_ptr<ql32::runtime::StorageService>(
        &sdStorage, [](ql32::runtime::StorageService*) {});
    auto fileChannels = std::shared_ptr<ql32::runtime::ChannelService>(
        &sdChannels, [](ql32::runtime::ChannelService*) {});
    services.channels = std::make_shared<ql32::runtime::ChannelRegistry>(
        std::move(fileChannels), [](std::string_view bytes) {
            Serial.write(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
        });
    services.ui = uiService;
    services.graphics = std::static_pointer_cast<ql32::graphics::Service>(graphics);
    services.network = std::shared_ptr<ql32::runtime::NetworkService>(
        &boardNetwork, [](ql32::runtime::NetworkService*) {});
    services.systemControl = std::shared_ptr<ql32::runtime::SystemControlService>(
        &boardSystemControl, [](ql32::runtime::SystemControlService*) {});
    services.memory = std::shared_ptr<ql32::runtime::MemoryService>(
        &boardMemory, [](ql32::runtime::MemoryService*) {});
    services.cpuFrequency = std::shared_ptr<ql32::runtime::CpuFrequencyService>(
        &boardCpuFrequency, [](ql32::runtime::CpuFrequencyService*) {});
    uiPresenter = std::make_unique<ql32::platform::esp32::LineUiPresenter>(
        uiService, *basicOutput);
    scheduler = std::make_unique<ql32::vm::SessionScheduler>(std::move(services), 128);
    replTask = scheduler->launchInteractive(*basicOutput, std::move(startupProgram));
}

void loop() {
    while (acceptsInput() && Serial.available() > 0)
        receiveSerialCharacter(static_cast<char>(Serial.read()));
    advanceScheduler();
    delay(1);
}
