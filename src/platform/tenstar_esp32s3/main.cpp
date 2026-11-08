#include <Arduino.h>
#include <Adafruit_ST7789.h>
#include <esp_heap_caps.h>
#include <SPIFFS.h>
#include <SPI.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <cstring>
#include <memory>
#include <ostream>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include "capabilities.hpp"
#include "pins.hpp"
#include "ql32/graphics/windowed_service.hpp"
#include "ql32/vm/scheduler.hpp"
#include "ql32/platform/esp32/clock.hpp"
#include "ql32/platform/esp32/arduino_hardware.hpp"
#include "ql32/platform/esp32/cpu_frequency.hpp"
#include "ql32/ui/graphic_presenter.hpp"
#include "ql32/platform/esp32/memory.hpp"
#include "ql32/platform/esp32/network.hpp"
#include "ql32/platform/esp32/sd_channels.hpp"
#include "ql32/platform/esp32/sd_storage.hpp"
#include "ql32/platform/esp32/system_control.hpp"
#include "ql32/platform/esp32/monitor_transfer.hpp"
#include "ql32/platform/esp32/remote_debug_protocol.hpp"
#include "ql32/platform/tenstar_esp32s3/graphics.hpp"
#include "ql32/platform/tenstar_esp32s3/frame_presenter.hpp"
#include "ql32/platform/tenstar_esp32s3/text_console.hpp"

namespace {

SPIClass tftBus(HSPI);
ql32::platform::tenstar_esp32s3::TenstarSt7789Display display(&tftBus,
    ql32::boards::tenstar_esp32s3::tftDataCommand,
    ql32::boards::tenstar_esp32s3::tftChipSelect,
    ql32::boards::tenstar_esp32s3::tftReset);
std::shared_ptr<ql32::graphics::WindowedService> graphics;
ql32::platform::esp32::ArduinoHardware hardwareAdapter(
    &tftBus, nullptr, &Wire,
    ql32::boards::tenstar_esp32s3::i2cSda,
    ql32::boards::tenstar_esp32s3::i2cScl, 17, 18, 15, 16,
    {35, 36, 37, 7, 39, 40, 45, 42, 41});
ql32::platform::esp32::Clock boardClock;
ql32::platform::esp32::CpuFrequency boardCpuFrequency;
ql32::platform::esp32::MemoryService boardMemory;
ql32::platform::esp32::WifiNetworkService boardNetwork;
ql32::platform::esp32::SystemControl boardSystemControl;
ql32::platform::esp32::SdStorage sdStorage(SPIFFS, "FFS");
ql32::platform::esp32::SdChannels sdChannels(SPIFFS, "FFS");
std::unique_ptr<ql32::platform::tenstar_esp32s3::TftTextConsole> tftConsole;
std::unique_ptr<std::ostream> basicOutput;
std::unique_ptr<ql32::vm::SessionScheduler> scheduler;
ql32::vm::TaskId replTask = 0;
std::shared_ptr<ql32::ui::QueuedService> uiService =
    std::make_shared<ql32::ui::QueuedService>();
std::unique_ptr<ql32::ui::GraphicPresenter> uiPresenter;
std::unique_ptr<ql32::platform::esp32::RemoteDebugProtocol> remoteDebugger;
std::string inputLine;
int editingLineNumber = 0;
bool protocolInputLine = false;
bool serialSawCarriageReturn = false;
bool replReadyMessageShown = false;
bool replPromptShown = false;
std::uint32_t lastDebugOverlayRefresh = 0;

void refreshDebugOverlay() {
    if (!graphics || !graphics->debugOverlayEnabled()) return;
    const std::uint32_t now = millis();
    if (static_cast<std::uint32_t>(now - lastDebugOverlayRefresh) < 1000) return;
    lastDebugOverlayRefresh = now;

    const auto freeBytes = boardMemory.freeBytes();
    const auto heap = boardMemory.fragmentationInfo();
    const std::size_t activeTasks = scheduler ? scheduler->activeTaskCount() : 0;
    char clockText[9] = "--:--:--";
    if (const auto timestamp = boardClock.unixTimeSeconds()) {
        const std::time_t value = static_cast<std::time_t>(*timestamp);
        std::tm parts{};
        if (gmtime_r(&value, &parts))
            std::snprintf(clockText, sizeof(clockText), "%02d:%02d:%02d",
                          parts.tm_hour, parts.tm_min, parts.tm_sec);
    }
    const auto freeK = freeBytes / 1024;
    const auto maxK = heap ? heap->largestFreeBlockBytes / 1024 : 0;
    char line[64]{};
    std::snprintf(line, sizeof(line), "FREE:%lluK MAX:%lluK ALLOC:%llu TASK:%u %s",
        static_cast<unsigned long long>(freeK),
        static_cast<unsigned long long>(maxK),
        static_cast<unsigned long long>(heap ? heap->allocatedBlockCount : 0),
        static_cast<unsigned>(activeTasks), clockText);
    graphics->setDebugOverlayText(line);
}

ql32::runtime::YModemSource screenCaptureSource() {
    const auto width = graphics->windowManager().screenWidth();
    const auto height = graphics->windowManager().screenHeight();
    constexpr std::uint32_t paletteEntries = 256;
    constexpr std::uint32_t headerSize = 14 + 40 + paletteEntries * 4;
    const std::uint32_t rowBytes = (width + 3) & ~3u;
    const std::uint32_t imageSize = rowBytes * height;
    std::array<std::uint8_t, headerSize> bitmapHeader{};
    const auto put16 = [&bitmapHeader](std::size_t at, std::uint16_t value) {
        bitmapHeader[at] = static_cast<std::uint8_t>(value);
        bitmapHeader[at + 1] = static_cast<std::uint8_t>(value >> 8);
    };
    const auto put32 = [&bitmapHeader](std::size_t at, std::uint32_t value) {
        for (unsigned byte = 0; byte < 4; ++byte)
            bitmapHeader[at + byte] = static_cast<std::uint8_t>(value >> (byte * 8));
    };
    bitmapHeader[0] = 'B';
    bitmapHeader[1] = 'M';
    put32(2, headerSize + imageSize);
    put32(10, headerSize);
    put32(14, 40);
    put32(18, width);
    put32(22, height); // positive height stores rows bottom-to-top
    put16(26, 1);
    put16(28, 8);
    put32(34, imageSize);
    put32(38, 2835);
    put32(42, 2835);
    put32(46, paletteEntries);
    put32(50, paletteEntries);
    for (std::uint32_t index = 0; index < paletteEntries; ++index) {
        const auto color = ql32::graphics::WindowedService::defaultPaletteColor(index);
        const auto at = static_cast<std::size_t>(54 + index * 4);
        bitmapHeader[at] = color.blue;
        bitmapHeader[at + 1] = color.green;
        bitmapHeader[at + 2] = color.red;
    }
    const std::string header(reinterpret_cast<const char*>(bitmapHeader.data()),
                             bitmapHeader.size());

    ql32::runtime::YModemSource source;
    source.filename = "ql32-screen.bmp";
    source.size = header.size() + imageSize;
    source.read = [header, width, height, rowBytes, imageSize,
                   indexedRow = std::vector<ql32::graphics::ColorIndex>{}](
                      std::uint64_t offset, std::uint8_t* destination,
                      std::size_t count) mutable {
        constexpr std::size_t maxRowBytes = 320;
        std::array<std::uint8_t, maxRowBytes> packedRow{};
        std::size_t written = 0;
        while (written < count) {
            if (offset < header.size()) {
                const auto start = static_cast<std::size_t>(offset);
                const auto part = std::min(count - written, header.size() - start);
                std::memcpy(destination + written, header.data() + start, part);
                offset += part;
                written += part;
                continue;
            }

            const std::uint64_t bodyOffset = offset - header.size();
            if (bodyOffset >= imageSize)
                throw std::out_of_range("screen capture read exceeds BMP image size");
            const auto bitmapRow = static_cast<std::uint32_t>(bodyOffset / rowBytes);
            const auto y = height - 1 - bitmapRow;
            const auto firstByte = static_cast<std::size_t>(bodyOffset % rowBytes);
            graphics->composeDisplayRow(y, indexedRow);
            std::copy(indexedRow.begin(), indexedRow.end(), packedRow.begin());
            const auto available = static_cast<std::size_t>(rowBytes) - firstByte;
            const auto part = std::min(count - written, available);
            std::memcpy(destination + written, packedRow.data() + firstByte, part);
            offset += part;
            written += part;
        }
    };
    return source;
}

void appendInputCharacter(char character) {
    if (inputLine.size() >= 1024) return;
    if (inputLine.empty() && character == '@') protocolInputLine = true;
    inputLine.push_back(character);
    if (!protocolInputLine) Serial.write(static_cast<std::uint8_t>(character));
}

void eraseInputCharacter() {
    if (inputLine.empty()) return;
    inputLine.pop_back();
    if (!protocolInputLine) Serial.print("\b \b");
}

void submitInputLine() {
    const bool isProtocolRequest = protocolInputLine;
    if (inputLine.empty() && editingLineNumber > 0)
        inputLine = std::to_string(editingLineNumber);
    Serial.println();
    if (inputLine == "@SCREEN" &&
        ql32::platform::esp32::handleMonitorTransferCommand(
            "SCREEN", sdStorage, sdChannels, Serial,
            [] { return screenCaptureSource(); })) {
    } else if (remoteDebugger && remoteDebugger->handles(inputLine)) {
        remoteDebugger->handle(inputLine, *scheduler, sdStorage, *basicOutput,
            [](std::string_view bytes) {
                Serial.write(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
                Serial.flush();
            });
    } else if (uiPresenter && uiPresenter->active()) {
        (void)uiPresenter->consumeLine(inputLine);
    } else if (ql32::platform::esp32::handleMonitorTransferCommand(
                   inputLine, sdStorage, sdChannels, Serial,
                   [] { return screenCaptureSource(); })) {
    } else if (scheduler && !inputLine.empty() &&
               !scheduler->submit(scheduler->focusedTask(), inputLine + "\n")) {
        Serial.println("BASIC session is busy");
    }
    inputLine.clear();
    editingLineNumber = 0;
    protocolInputLine = false;
    if (!isProtocolRequest) replPromptShown = false;
}

void receiveSerialCharacter(char character) {
    if (character == '\n' && serialSawCarriageReturn) {
        serialSawCarriageReturn = false;
        return;
    }
    if (uiPresenter && uiPresenter->graphicActive() && !protocolInputLine) {
        if (character == '@' && inputLine.empty()) {
            protocolInputLine = true;
            appendInputCharacter(character);
            return;
        }
        serialSawCarriageReturn = character == '\r';
        uiPresenter->handleSerialByte(character, millis());
        return;
    }
    if (character == '\r' || character == '\n') {
        serialSawCarriageReturn = character == '\r';
        submitInputLine();
        return;
    }
    serialSawCarriageReturn = false;
    if (character == '\b' || character == 127) eraseInputCharacter();
    else if ((character >= 32 && character <= 126) ||
             (protocolInputLine && character == '\t'))
        appendInputCharacter(character);
}

bool acceptsInput() {
    if (uiPresenter && uiPresenter->active()) return true;
    if (!scheduler) return false;
    const auto status = scheduler->taskStatus(scheduler->focusedTask());
    return status && *status == ql32::vm::TaskStatus::WaitingInput;
}

void advanceScheduler() {
    if (!scheduler) return;
    (void)scheduler->tick();
    refreshDebugOverlay();
    if (uiPresenter) uiPresenter->refresh();

    scheduler->visitTaskErrors([](ql32::vm::TaskId id, const char* error, void* context) {
        Serial.printf("BASIC session %llu VM error: %s\n",
                      static_cast<unsigned long long>(id), error);
        if (auto* output = static_cast<std::ostream*>(context))
            *output << "BASIC session " << id << " VM error: " << error << '\n';
    }, basicOutput.get());

    const bool sessionWaitingForUi = scheduler->hasWaitingUiTask();
    if (uiPresenter && !uiPresenter->active() && !uiPresenter->beginNext() &&
        sessionWaitingForUi)
        Serial.println("suspended session has no queued UI request");

    const auto focused = scheduler->focusedTask();
    const auto status = scheduler->taskStatus(focused);
    if (!status) return;
    if (*status == ql32::vm::TaskStatus::WaitingInput &&
        (!uiPresenter || !uiPresenter->active())) {
        if (!replReadyMessageShown) {
            Serial.println("BASIC REPL ready (USB serial)");
            replReadyMessageShown = true;
        }
        if (!replPromptShown) {
            const auto edit = scheduler->takePendingLineEdit(focused);
            if (edit) {
                inputLine = std::move(edit->text);
                editingLineNumber = edit->lineNumber;
            }
            if (focused == replTask) Serial.print("> ");
            else Serial.printf("task %llu> ", static_cast<unsigned long long>(focused));
            if (edit) Serial.print(inputLine.c_str());
            replPromptShown = true;
        }
    } else {
        replPromptShown = false;
    }
}

} // namespace

void setup() {
    Serial.begin(115200);
    if (!boardMemory.initializeEmergencyReserve(
            ql32::platform::esp32::defaultEmergencyReserveBytes)) {
        Serial.println("Fatal: unable to reserve memory for recovery operations");
        return;
    }

    if (!ql32::boards::tenstar_esp32s3::capabilities().valid()) {
        Serial.println("QL/32 Tenstar ESP32-S3 capability profile invalid");
        return;
    }

    pinMode(ql32::boards::tenstar_esp32s3::tftBacklight, OUTPUT);
    digitalWrite(ql32::boards::tenstar_esp32s3::tftBacklight, HIGH);
    tftBus.begin(ql32::boards::tenstar_esp32s3::tftSck,
                 ql32::boards::tenstar_esp32s3::tftMiso,
                 ql32::boards::tenstar_esp32s3::tftMosi,
                 ql32::boards::tenstar_esp32s3::tftChipSelect);
    display.beginTenstar();
    display.fillScreen(ST77XX_BLACK);
    tftConsole = std::make_unique<ql32::platform::tenstar_esp32s3::TftTextConsole>(display);
    basicOutput = std::make_unique<std::ostream>(tftConsole.get());
    auto framePresenter = std::make_shared<ql32::platform::tenstar_esp32s3::St7789FramePresenter>(
        display,
        [](ql32::graphics::Rect region) {
            if (tftConsole) tftConsole->restoreOverlay(region);
        },
        [] { if (tftConsole) tftConsole->reconfigure(); });
    graphics = std::make_shared<ql32::graphics::WindowedService>(
        display.width(), display.height(), framePresenter);
    graphics->setCanvasResizeObserver(
        [](ql32::graphics::CanvasResizePhase phase, std::size_t oldBytes,
           std::size_t newBytes) {
            const char* phaseName = "unknown";
            switch (phase) {
            case ql32::graphics::CanvasResizePhase::BeforeAllocation:
                phaseName = "before-allocation";
                break;
            case ql32::graphics::CanvasResizePhase::BothBuffersLive:
                phaseName = "both-buffers-live";
                break;
            case ql32::graphics::CanvasResizePhase::AfterSwap:
                phaseName = "after-swap";
                break;
            }
            // Capture heap values before serial output so logging cannot skew
            // the reported checkpoint.
            const auto freeBytes = heap_caps_get_free_size(MALLOC_CAP_8BIT);
            const auto largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
            const auto liveWindows = graphics ? graphics->windowManager().size() : 0;
            Serial.printf("CANVAS_HEAP|%s|%u|%u|%u|%u|%u\n", phaseName,
                          static_cast<unsigned>(oldBytes),
                          static_cast<unsigned>(newBytes),
                          static_cast<unsigned>(freeBytes),
                          static_cast<unsigned>(largestBlock),
                          static_cast<unsigned>(liveWindows));
        });
    graphics->setDefaultWindowBounds(0, {
        0, std::max<std::int32_t>(0, static_cast<std::int32_t>(display.height()) - 40),
        static_cast<std::uint32_t>(display.width()),
        static_cast<std::uint32_t>(std::min<std::int32_t>(40, display.height()))});
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

    const bool storageReady = SPIFFS.begin(true);
    if (storageReady && !SPIFFS.exists("/supervisor"))
        (void)SPIFFS.mkdir("/supervisor");

    Serial.println("QL/32 Tenstar ESP32-S3 ready");
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
        if (SPIFFS.exists("/startup.bas")) {
            try {
                startupProgram = sdStorage.readText("startup.bas");
                Serial.println("Running FFS:/startup.bas");
            } catch (const ql32::runtime::ServiceError& error) {
                Serial.printf("Storage startup read error: %s\n", error.what());
                return;
            }
        } else {
            Serial.println("FFS:/startup.bas not found; starting with a clear display");
        }
    }

    ql32::vm::Services services;
    services.storage = std::shared_ptr<ql32::runtime::StorageService>(
        &sdStorage, [](ql32::runtime::StorageService*) {});
    auto fileChannels = std::shared_ptr<ql32::runtime::ChannelService>(
        &sdChannels, [](ql32::runtime::ChannelService*) {});
    services.channels = std::make_shared<ql32::runtime::ChannelRegistry>(
        std::move(fileChannels), [](std::string_view bytes) {
            Serial.write(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
        }, std::shared_ptr<ql32::runtime::HardwareChannelService>(
            &hardwareAdapter, [](ql32::runtime::HardwareChannelService*) {}));
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
    graphics->setConsoleLayer([](std::uint32_t y, std::uint32_t x,
                                 std::vector<ql32::graphics::ColorIndex>& row) {
        if (tftConsole) tftConsole->composePaletteRegionRow(y, x, row);
    });
    tftConsole->setInvalidateCallback([](ql32::graphics::Rect region) {
        graphics->invalidateSystemRegion(region);
    });
    uiPresenter = std::make_unique<ql32::ui::GraphicPresenter>(uiService, graphics);
    scheduler = std::make_unique<ql32::vm::SessionScheduler>(std::move(services), 128);
    ql32::platform::esp32::RemoteDebugCapabilities remoteCapabilities;
    remoteCapabilities.board = "TENSTAR-ESP32-S3";
    remoteCapabilities.storageRoots = {"FFS:/"};
    remoteCapabilities.screenCapture = true;
    remoteDebugger = std::make_unique<ql32::platform::esp32::RemoteDebugProtocol>(
        std::move(remoteCapabilities));
    replTask = scheduler->launchInteractive(*basicOutput, std::move(startupProgram));
}

void loop() {
    while (Serial.available() > 0) {
        // The PC debugger must be able to control a paused worker even while
        // the focused BASIC task is running or waiting for a UI response.
        if (!acceptsInput() && !protocolInputLine && Serial.peek() != '@') break;
        receiveSerialCharacter(static_cast<char>(Serial.read()));
    }
    if (uiPresenter) uiPresenter->flushSerialEscape(millis());
    advanceScheduler();
    delay(1);
}
