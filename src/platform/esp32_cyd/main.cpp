#include "ql32/version.hpp"
#include "ql32/platform/esp32/application_input.hpp"
#include "ql32/platform/esp32/diagnostics.hpp"
#include <Arduino.h>
#include <Adafruit_ILI9341.h>
#include <SD.h>
#include <esp_heap_caps.h>
#if defined(QL32_CYD_USE_SPIFFS)
#include <SPIFFS.h>
#endif
#include <SPI.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <ctime>
#include <cstring>
#include <memory>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include "capabilities.hpp"
#include "pins.hpp"
#include "ql32/graphics/windowed_service.hpp"
#include "ql32/vm/scheduler.hpp"
#include "ql32/vm/tick_batch.hpp"
#include "ql32/runtime/file_ops.hpp"
#include "ql32/platform/esp32/clock.hpp"
#include "ql32/platform/esp32/arduino_hardware.hpp"
#include "ql32/platform/esp32/cpu_frequency.hpp"
#include "ql32/platform/esp32/drive_router.hpp"
#include "ql32/ui/graphic_presenter.hpp"
#include "ql32/platform/esp32/memory.hpp"
#include "ql32/platform/esp32/network.hpp"
#include "ql32/platform/esp32/sd_channels.hpp"
#include "ql32/platform/esp32/sd_storage.hpp"
#include "ql32/platform/olimex_vga/music.hpp"
#include "ql32/platform/esp32/system_control.hpp"
#include "ql32/platform/esp32/monitor_transfer.hpp"
#include "ql32/platform/esp32/remote_debug_protocol.hpp"
#include "ql32/platform/esp32/serial_key_decoder.hpp"
#include "ql32/platform/esp32_cyd/graphics.hpp"
#include "ql32/platform/esp32_cyd/frame_presenter.hpp"
#include "ql32/platform/esp32_cyd/text_console.hpp"
#include "ql32/platform/esp32_cyd/touch_input.hpp"

namespace {

SPIClass tftBus(HSPI);
SPIClass sdBus(VSPI);
ql32::platform::esp32_cyd::Xpt2046Touch touchPanel(
    ql32::boards::esp32_2432s028::touchSck,
    ql32::boards::esp32_2432s028::touchMiso,
    ql32::boards::esp32_2432s028::touchMosi,
    ql32::boards::esp32_2432s028::touchChipSelect,
    ql32::boards::esp32_2432s028::touchInterrupt);
ql32::platform::esp32_cyd::CydIli9341Display display(&tftBus,
    ql32::boards::esp32_2432s028::tftDataCommand,
    ql32::boards::esp32_2432s028::tftChipSelect, -1);
std::shared_ptr<ql32::graphics::WindowedService> graphics;
std::shared_ptr<ql32::runtime::ChannelRegistry> channelRegistry;
ql32::platform::esp32::ArduinoHardware hardwareAdapter(
    &tftBus, &sdBus, &Wire, 22, 27, 17, 16, 26, 25,
    {14, 12, 13, 15, 2, 21, 18, 19, 23, 5, 22, 27,
     ql32::boards::esp32_2432s028::touchSck,
     ql32::boards::esp32_2432s028::touchMiso,
     ql32::boards::esp32_2432s028::touchMosi,
     ql32::boards::esp32_2432s028::touchChipSelect,
     ql32::boards::esp32_2432s028::touchInterrupt});
ql32::platform::esp32::Clock boardClock;
ql32::platform::esp32::CpuFrequency boardCpuFrequency;
ql32::platform::esp32::MemoryService boardMemory;
ql32::platform::esp32::WifiNetworkService boardNetwork;
ql32::platform::esp32::SystemControl boardSystemControl;
#if defined(QL32_CYD_USE_SPIFFS)
bool spiffsSpace(std::uint64_t& total, std::uint64_t& used) {
    total = SPIFFS.totalBytes();
    used = SPIFFS.usedBytes();
    return total != 0;
}
ql32::platform::esp32::SdStorage sdStorage(SPIFFS, "FFS", spiffsSpace, "/spiffs");
ql32::platform::esp32::SdChannels sdChannels(SPIFFS, "FFS");
// Optional removable card, addressed as SDC:/. Unqualified paths stay on FFS:.
bool sdCardSpace(std::uint64_t& total, std::uint64_t& used) {
    total = SD.totalBytes();
    used = SD.usedBytes();
    return total != 0;
}
ql32::platform::esp32::SdStorage sdcStorage(SD, "SDC", sdCardSpace, "/sd");
ql32::platform::esp32::SdChannels sdcChannels(SD, "SDC");
ql32::platform::esp32::DriveRouterStorage driveStorage(sdStorage, sdcStorage, true);
ql32::platform::esp32::DriveRouterChannels driveChannels(sdChannels, sdcChannels, true);
ql32::runtime::StorageService& monitorStorage = driveStorage;
ql32::runtime::ChannelService& monitorChannels = driveChannels;
#else
bool sdCardSpace(std::uint64_t& total, std::uint64_t& used) {
    total = SD.totalBytes();
    used = SD.usedBytes();
    return total != 0;
}
ql32::platform::esp32::SdStorage sdStorage(SD, "SDC", sdCardSpace, "/sd");
ql32::platform::esp32::SdChannels sdChannels(SD, "SDC");
ql32::runtime::StorageService& monitorStorage = sdStorage;
ql32::runtime::ChannelService& monitorChannels = sdChannels;
#endif
std::unique_ptr<ql32::platform::esp32_cyd::TftTextConsole> tftConsole;
std::unique_ptr<std::ostream> basicOutput;
std::unique_ptr<ql32::vm::SessionScheduler> scheduler;
ql32::vm::TaskId replTask = 0;
std::shared_ptr<ql32::ui::QueuedService> uiService =
    std::make_shared<ql32::ui::QueuedService>();
std::shared_ptr<ql32::input::KeyRouter> applicationKeys = std::make_shared<ql32::input::KeyRouter>();
ql32::input::SerialDecoder applicationSerialDecoder;
ql32::platform::esp32::ApplicationProtocolGate applicationProtocol;
std::uint64_t applicationInputOwner = 0;

std::unique_ptr<ql32::ui::GraphicPresenter> uiPresenter;
std::unique_ptr<ql32::platform::esp32::RemoteDebugProtocol> remoteDebugger;
std::string inputLine;
std::string lastSubmittedLine;
std::string inputPrompt;
std::size_t inputCursor = 0;
ql32::platform::esp32::SerialKeyDecoder serialKeyDecoder;
int editingLineNumber = 0;
bool protocolInputLine = false;
bool serialSawCarriageReturn = false;
bool replReadyMessageShown = false;
bool replPromptShown = false;
std::uint32_t lastDebugOverlayRefresh = 0;
std::optional<ql32::graphics::HitTarget> capturedTouchTarget;
std::optional<ql32::graphics::ChannelId> capturedTouchChannel;
bool touchWasDown = false;
std::uint32_t lastTouchMoveMs = 0;
ql32::graphics::Point lastTouchPoint{};
ql32::graphics::Point lastTouchLocalPoint{};

void pollTouch() {
    if (!graphics || !channelRegistry) return;
    const auto [width, height] = graphics->displaySize();
    ql32::graphics::Point point{};
    const bool down = touchPanel.readPoint(point, width, height,
                                           graphics->rotationDegrees());
    const std::uint32_t now = millis();
    if (down) {
        ql32::runtime::TouchEvent event;
        if (!touchWasDown) {
            capturedTouchTarget = graphics->windowManager().hitTest(point);
            touchWasDown = true;
            if (capturedTouchTarget)
                capturedTouchChannel = graphics->channelForWindow(
                    capturedTouchTarget->owner, capturedTouchTarget->window);
            if (!capturedTouchTarget || !capturedTouchChannel) {
                capturedTouchTarget.reset();
                capturedTouchChannel.reset();
                return;
            }
            event.type = ql32::runtime::TouchEventType::Down;
        } else {
            if (!capturedTouchTarget || point.x == lastTouchPoint.x &&
                point.y == lastTouchPoint.y ||
                static_cast<std::uint32_t>(now - lastTouchMoveMs) < 20) {
                lastTouchPoint = point;
                return;
            }
            event.type = ql32::runtime::TouchEventType::Move;
        }
        try {
            const auto window = graphics->windowManager().windowState(
                capturedTouchTarget->window);
            event.x = point.x - window.bounds.x;
            event.y = point.y - window.bounds.y;
            lastTouchLocalPoint = {event.x, event.y};
            event.window = *capturedTouchChannel;
            channelRegistry->publishTouch(capturedTouchTarget->owner, event);
            lastTouchMoveMs = now;
            lastTouchPoint = point;
        } catch (const std::out_of_range&) {
            if (capturedTouchTarget && capturedTouchChannel)
                channelRegistry->publishTouch(capturedTouchTarget->owner,
                    {ql32::runtime::TouchEventType::Cancel,
                     lastTouchLocalPoint.x, lastTouchLocalPoint.y,
                     *capturedTouchChannel});
            capturedTouchTarget.reset();
            capturedTouchChannel.reset();
        }
        return;
    }

    if (touchWasDown && capturedTouchTarget) {
        try {
            const auto window = graphics->windowManager().windowState(
                capturedTouchTarget->window);
            channelRegistry->publishTouch(capturedTouchTarget->owner,
                {ql32::runtime::TouchEventType::Up,
                 lastTouchPoint.x - window.bounds.x,
                 lastTouchPoint.y - window.bounds.y,
                 *capturedTouchChannel});
        } catch (const std::out_of_range&) {
            // The owning window may have been closed while the finger was down.
        }
    }
    touchWasDown = false;
    capturedTouchTarget.reset();
    capturedTouchChannel.reset();
}

void refreshDebugOverlay() {
    if (!graphics || !graphics->debugOverlayEnabled()) return;
    const std::uint32_t now = millis();
    if (static_cast<std::uint32_t>(now - lastDebugOverlayRefresh) < 1000) return;
    lastDebugOverlayRefresh = now;

    const auto freeBytes = boardMemory.freeBytes();
    const auto heap = boardMemory.fragmentationInfo();
    const std::size_t activeTasks = scheduler ? scheduler->activeTaskCount() : 0;
    char clockText[16]{};
    const auto timestamp = boardClock.unixTimeSeconds();
    std::tm parts{};
    const std::time_t value = timestamp ? static_cast<std::time_t>(*timestamp) : 0;
    if (timestamp && gmtime_r(&value, &parts)) {
        std::snprintf(clockText, sizeof(clockText), "%02d:%02d:%02d",
                      parts.tm_hour, parts.tm_min, parts.tm_sec);
    } else {
        // Unsynchronized wall clock: show uptime instead, marked with "UP".
        const std::uint32_t seconds = now / 1000;
        std::snprintf(clockText, sizeof(clockText), "UP %02u:%02u:%02u",
                      static_cast<unsigned>((seconds / 3600) % 100),
                      static_cast<unsigned>((seconds / 60) % 60),
                      static_cast<unsigned>(seconds % 60));
    }
    const auto freeK = freeBytes / 1024;
    const auto maxK = heap ? heap->largestFreeBlockBytes / 1024 : 0;
    const auto minK = boardMemory.minimumFreeBytes().value_or(0) / 1024;
    char io[24]{};
    const auto disk = ql32::runtime::fileops::activity();
    if (disk.activeJobs != 0) {
        if (disk.total != 0)
            std::snprintf(io, sizeof(io), " IO %u/%u",
                          static_cast<unsigned>(disk.completed), static_cast<unsigned>(disk.total));
        else
            std::snprintf(io, sizeof(io), " IO %u", static_cast<unsigned>(disk.completed));
    }
    char late[24]{};
    const auto& music = ql32::platform::esp32::diagnostics();
    const auto lateBlocks = music.musicLateBlocks.load() + music.musicWriteErrors.load();
    if (lateBlocks != 0) std::snprintf(late, sizeof(late), " LATE:%u", static_cast<unsigned>(lateBlocks));
    char line[120]{};
    std::snprintf(line, sizeof(line), "FREE:%lluK MAX:%lluK BLK:%llu MIN:%lluK TASK:%u %s%s%s",
        static_cast<unsigned long long>(freeK),
        static_cast<unsigned long long>(maxK),
        static_cast<unsigned long long>(heap ? heap->allocatedBlockCount : 0),
        static_cast<unsigned long long>(minK),
        static_cast<unsigned>(activeTasks), clockText, io, late);
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
    const bool atEnd = inputCursor == inputLine.size();
    inputLine.insert(inputCursor, 1, character);
    ++inputCursor;
    if (!protocolInputLine) {
        if (atEnd) Serial.write(static_cast<std::uint8_t>(character));
        else {
            Serial.print("\r");
            Serial.print(inputPrompt.c_str());
            Serial.print(inputLine.c_str());
            const auto suffix = inputLine.size() - inputCursor;
            for (std::size_t i = 0; i < suffix; ++i) Serial.write('\b');
        }
    }
}

void eraseInputCharacter() {
    if (inputCursor == 0) return;
    const bool atEnd = inputCursor == inputLine.size();
    const auto oldLength = inputLine.size();
    inputLine.erase(inputCursor - 1, 1);
    --inputCursor;
    if (!protocolInputLine) {
        if (atEnd) Serial.print("\b \b");
        else {
            Serial.print("\r");
            Serial.print(inputPrompt.c_str());
            Serial.print(inputLine.c_str());
            const auto erased = oldLength - inputLine.size();
            for (std::size_t i = 0; i < erased; ++i) Serial.write(' ');
            const auto backspaces = inputLine.size() - inputCursor + erased;
            for (std::size_t i = 0; i < backspaces; ++i) Serial.write('\b');
        }
    }
}

void deleteInputCharacter() {
    if (inputCursor >= inputLine.size()) return;
    const auto oldLength = inputLine.size();
    inputLine.erase(inputCursor, 1);
    Serial.print("\r");
    Serial.print(inputPrompt.c_str());
    Serial.print(inputLine.c_str());
    const auto erased = oldLength - inputLine.size();
    for (std::size_t i = 0; i < erased; ++i) Serial.write(' ');
    const auto backspaces = inputLine.size() - inputCursor + erased;
    for (std::size_t i = 0; i < backspaces; ++i) Serial.write('\b');
}

void redrawInputCursor() {
    if (protocolInputLine) return;
    Serial.print("\r");
    Serial.print(inputPrompt.c_str());
    Serial.print(inputLine.c_str());
    const auto suffix = inputLine.size() - inputCursor;
    for (std::size_t i = 0; i < suffix; ++i) Serial.write('\b');
}

void submitInputLine() {
    const bool isProtocolRequest = protocolInputLine;
    if (inputLine.empty() && editingLineNumber > 0)
        inputLine = std::to_string(editingLineNumber);
    if (!isProtocolRequest && !inputLine.empty() &&
        (!uiPresenter || !uiPresenter->active()))
        lastSubmittedLine = inputLine;
    Serial.println();
    if (inputLine == "@SCREEN" &&
        ql32::platform::esp32::handleMonitorTransferCommand(
            "SCREEN", monitorStorage, monitorChannels, Serial,
            [] { return screenCaptureSource(); })) {
    } else if (remoteDebugger && remoteDebugger->handles(inputLine)) {
        remoteDebugger->handle(inputLine, *scheduler, monitorStorage, *basicOutput,
            [](std::string_view bytes) {
                Serial.write(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
                Serial.flush();
            });
    } else if (uiPresenter && uiPresenter->active()) {
        (void)uiPresenter->consumeLine(inputLine);
    } else if (ql32::platform::esp32::handleMonitorTransferCommand(
                   inputLine, monitorStorage, monitorChannels, Serial,
                   [] { return screenCaptureSource(); })) {
    } else if (scheduler && !inputLine.empty() &&
               !scheduler->submit(scheduler->focusedTask(), inputLine + "\n")) {
        Serial.println("BASIC session is busy");
    }
    inputLine.clear();
    inputCursor = 0;
    inputPrompt.clear();
    serialKeyDecoder.reset();
    editingLineNumber = 0;
    protocolInputLine = false;
    if (!isProtocolRequest) replPromptShown = false;
}

void refreshApplicationInput() {
    const auto owner = scheduler && (!uiPresenter || !uiPresenter->active()) ? scheduler->keyInputSession() : 0;
    const bool ownerChanged = applicationInputOwner != owner;
    applicationInputOwner = owner;
    const bool routeChanged = applicationKeys->focus(owner);
    if (ownerChanged || routeChanged) { applicationSerialDecoder.reset(); applicationProtocol.reset(); }
    applicationProtocol.flush(millis(), applicationSerialDecoder, *applicationKeys);
    if (const auto event = applicationSerialDecoder.flush(millis())) applicationKeys->publish(*event);
}

void receiveSerialCharacter(char character) {
    refreshApplicationInput();
    if (character == '\n' && serialSawCarriageReturn) {
        serialSawCarriageReturn = false;
        return;
    }
    if (applicationInputOwner && !protocolInputLine) {
        serialSawCarriageReturn = false;
        applicationProtocol.feed(static_cast<unsigned char>(character), millis(), applicationSerialDecoder,
            *applicationKeys, [](std::string_view prefix) { for (char byte : prefix) appendInputCharacter(byte); });
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
    if (protocolInputLine) {
        if (character == '\b' || character == 127) eraseInputCharacter();
        else if ((character >= 32 && character <= 126) || character == '\t')
            appendInputCharacter(character);
        return;
    }
    const auto key = serialKeyDecoder.feed(character);
    if (!key) return;
    switch (key->key) {
    case ql32::platform::esp32::SerialKey::Character:
        appendInputCharacter(key->character);
        break;
    case ql32::platform::esp32::SerialKey::Backspace:
        eraseInputCharacter();
        break;
    case ql32::platform::esp32::SerialKey::Delete:
        deleteInputCharacter();
        break;
    case ql32::platform::esp32::SerialKey::Up:
        if (inputLine.empty() && !lastSubmittedLine.empty()) {
            inputLine = lastSubmittedLine;
            inputCursor = inputLine.size();
            Serial.print(inputLine.c_str());
        }
        break;
    case ql32::platform::esp32::SerialKey::Down:
        break;
    case ql32::platform::esp32::SerialKey::Left:
        if (inputCursor > 0) { --inputCursor; Serial.write('\b'); }
        break;
    case ql32::platform::esp32::SerialKey::Right:
        if (inputCursor < inputLine.size()) {
            Serial.write(static_cast<std::uint8_t>(inputLine[inputCursor++]));
        }
        break;
    case ql32::platform::esp32::SerialKey::Home:
        inputCursor = 0;
        redrawInputCursor();
        break;
    case ql32::platform::esp32::SerialKey::End:
        inputCursor = inputLine.size();
        redrawInputCursor();
        break;
    case ql32::platform::esp32::SerialKey::Enter:
        submitInputLine();
        break;
    }
}

bool acceptsInput() {
    if (uiPresenter && uiPresenter->active()) return true;
    if (!scheduler) return false;
    const auto status = scheduler->taskStatus(scheduler->focusedTask());
    return status && *status == ql32::vm::TaskStatus::WaitingInput;
}

// Slices run back to back for up to this long per loop() pass; the loop only sleeps when idle.
constexpr std::uint64_t kSchedulerBatchMicros = 6000;
bool schedulerRanSlice = false;

void advanceScheduler() {
    if (!scheduler) return;
    schedulerRanSlice = ql32::vm::tickBatch(*scheduler, kSchedulerBatchMicros,
        [] { return static_cast<std::uint64_t>(micros()); });
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

    refreshApplicationInput();

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
                inputCursor = inputLine.size();
                editingLineNumber = edit->lineNumber;
            }
            if (focused == replTask) inputPrompt = "> ";
            else {
                char prompt[32];
                std::snprintf(prompt, sizeof(prompt), "task %llu> ",
                    static_cast<unsigned long long>(focused));
                inputPrompt = prompt;
            }
            Serial.print(inputPrompt.c_str());
            if (edit) Serial.print(inputLine.c_str());
            replPromptShown = true;
        }
    } else {
        replPromptShown = false;
    }
}

} // namespace

void setup() {
    uiService->setKeyInput(applicationKeys);
    Serial.begin(115200);
    if (!boardMemory.initializeEmergencyReserve(
            ql32::platform::esp32::defaultEmergencyReserveBytes)) {
        Serial.println("Fatal: unable to reserve memory for recovery operations");
        return;
    }

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
    touchPanel.begin();
    tftConsole = std::make_unique<ql32::platform::esp32_cyd::TftTextConsole>(display);
    basicOutput = std::make_unique<std::ostream>(tftConsole.get());
    auto framePresenter = std::make_shared<ql32::platform::esp32_cyd::Ili9341FramePresenter>(
        display,
        [](ql32::graphics::Rect region) {
            if (tftConsole) tftConsole->restoreOverlay(region);
        },
        [] { if (tftConsole) tftConsole->reconfigure(); });
    graphics = std::make_shared<ql32::graphics::WindowedService>(
        display.width(), display.height(), framePresenter);
    graphics->setCanvasAdmission([](std::size_t bytes) {
        return boardMemory.canvasFailure(bytes);
    });
    graphics->setCanvasMemoryProbe([]() -> std::size_t {
        const auto info = boardMemory.fragmentationInfo();
        return info ? static_cast<std::size_t>(info->largestFreeBlockBytes)
                    : std::numeric_limits<std::size_t>::max();
    });
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
    constexpr std::int32_t cydConsoleHeight = 24; // 3 text rows; keep equal to text_console.cpp
    graphics->setDefaultWindowBounds(0, {
        0, std::max<std::int32_t>(0, static_cast<std::int32_t>(display.height()) - cydConsoleHeight),
        static_cast<std::uint32_t>(display.width()),
        static_cast<std::uint32_t>(std::min<std::int32_t>(cydConsoleHeight, display.height()))});
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
    graphics->setConsoleTextPositionCallback([](std::uint32_t row,
                                                 std::uint32_t column) {
        if (tftConsole) tftConsole->setTextPosition(row, column);
    });

#if defined(QL32_CYD_USE_SPIFFS)
    sdBus.begin(ql32::boards::esp32_2432s028::sdSck,
                ql32::boards::esp32_2432s028::sdMiso,
                ql32::boards::esp32_2432s028::sdMosi,
                ql32::boards::esp32_2432s028::sdChipSelect);
    const bool storageReady = SPIFFS.begin(true);
    if (storageReady && !SPIFFS.exists("/supervisor"))
        (void)SPIFFS.mkdir("/supervisor");
    Serial.println(SD.begin(ql32::boards::esp32_2432s028::sdChipSelect, sdBus, 4000000,
                            "/sd", 5) ? "SDC card mounted" : "SDC card not present");
#else
    sdBus.begin(ql32::boards::esp32_2432s028::sdSck,
                ql32::boards::esp32_2432s028::sdMiso,
                ql32::boards::esp32_2432s028::sdMosi,
                ql32::boards::esp32_2432s028::sdChipSelect);
    const bool storageReady = SD.begin(ql32::boards::esp32_2432s028::sdChipSelect,
                                      sdBus, 4000000, "/sd", 5);
#endif

    Serial.println("QL/32 ESP32-2432S028 ready, version " QL32_VERSION_STRING QL32_VERSION_FEATURES);
    Serial.printf("Uptime ms: %llu\n",
                  static_cast<unsigned long long>(boardClock.monotonicMilliseconds()));
    const auto wallTime = boardClock.unixTimeSeconds();
    if (wallTime)
        Serial.printf("Unix time: %lld\n", static_cast<long long>(*wallTime));
    else
        Serial.println("Wall clock unsynchronized");
    Serial.println(storageReady ? "Storage ready" : "Storage unavailable");
#if defined(QL32_CYD_USE_SPIFFS)
    if (storageReady) {
        // Program recovery files from an interrupted RUN are never deleted automatically.
        File root = SPIFFS.open("/");
        for (File entry = root.openNextFile(); entry; entry = root.openNextFile()) {
            const char* name = entry.name();
            if (name[0] == '/') ++name;
            if (std::strncmp(name, ".q32-run-", 9) == 0 && !std::strstr(name, ".meta"))
                Serial.printf("Recovery file kept: LOAD \"FFS:%s\" restores the program; delete it when done\n",
                              name);
        }
        try {
            for (const auto& entry : sdcStorage.list("/"))
                if (entry.name.rfind(".q32-run-", 0) == 0 && entry.name.find(".meta") == std::string::npos)
                    Serial.printf("Recovery file kept: LOAD \"SDC:%s\" restores the program; delete it when done\n",
                                  entry.name.c_str());
        } catch (...) {
        }
    }
#endif

    std::string startupProgram;
    if (storageReady) {
#if defined(QL32_CYD_USE_SPIFFS)
        if (SPIFFS.exists("/startup.bas")) {
            try {
                startupProgram = sdStorage.readText("startup.bas");
                Serial.println("Running Storage:/startup.bas");
            } catch (const ql32::runtime::ServiceError& error) {
                Serial.printf("Storage startup read error: %s; starting without it\n",
                              error.what());
                startupProgram.clear();
            }
        } else {
            Serial.println("Storage:/startup.bas not found; starting with a clear display");
        }
#else
        try {
            startupProgram = sdStorage.readText("startup.bas");
            Serial.println("Running SD:/startup.bas");
        } catch (const ql32::runtime::ServiceError& error) {
            startupProgram.clear();
            if (error.code() != ql32::runtime::ServiceErrorCode::NotFound)
                Serial.printf("Storage startup read error: %s; starting without it\n",
                              error.what());
            else
                Serial.println("SD:/startup.bas not found; starting with a clear display");
        }
#endif
    }

    ql32::vm::Services services;
    {
        ql32::runtime::SystemInfoProfile profile;
        profile.values = {QL32_VERSION_STRING, "unknown", "esp32_2432s028_yellow_tft",
                          "ili9341-tft 320x240 rgb565", "serial,touch", "sd",
                          "tft,touch,network,wifi"};
#if defined(QL32_CYD_USE_SPIFFS)
        profile.values[5] = "ffs";
        profile.values[6] += ",ffs";
#endif
        profile.capabilities = {{"display", true}, {"input", true},
                                {"storage", true}, {"network", true},
                                {"wifi", true}, {"ps2", false}, {"serial", true},
                                {"touch", true}, {"audio", true}, {"sdc", true}, {"sd",
#if defined(QL32_CYD_USE_SPIFFS)
                                false
#else
                                true
#endif
                                }, {"ffs",
#if defined(QL32_CYD_USE_SPIFFS)
                                true
#else
                                false
#endif
                                }, {"psram", false}, {"spi", true}, {"i2c", true},
                                {"gpio", true}, {"adc", true}, {"timer", true},
                                {"buttons", false}, {"usb-hid", false}};
        profile.properties = {{"display.width", 320}, {"display.height", 240},
                              {"display.depth", 16}};
        services.systemInfo = std::make_shared<ql32::runtime::StaticSystemInfoService>(
            std::move(profile));
    }
#if defined(QL32_CYD_USE_SPIFFS)
    services.storage = std::shared_ptr<ql32::runtime::StorageService>(
        &driveStorage, [](ql32::runtime::StorageService*) {});
    auto fileChannels = std::shared_ptr<ql32::runtime::ChannelService>(
        &driveChannels, [](ql32::runtime::ChannelService*) {});
#else
    services.storage = std::shared_ptr<ql32::runtime::StorageService>(
        &sdStorage, [](ql32::runtime::StorageService*) {});
    auto fileChannels = std::shared_ptr<ql32::runtime::ChannelService>(
        &sdChannels, [](ql32::runtime::ChannelService*) {});
#endif
    channelRegistry = std::make_shared<ql32::runtime::ChannelRegistry>(
        std::move(fileChannels), [](std::string_view bytes) {
            Serial.write(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
        }, std::shared_ptr<ql32::runtime::HardwareChannelService>(
            &hardwareAdapter, [](ql32::runtime::HardwareChannelService*) {}), true);
    channelRegistry->setSerialTryWriter([](std::string_view bytes) -> std::size_t {
        const std::size_t room = static_cast<std::size_t>(Serial.availableForWrite());
        const std::size_t count = std::min(room, bytes.size());
        if (count != 0)
            Serial.write(reinterpret_cast<const std::uint8_t*>(bytes.data()), count);
        return count;
    });
    services.channels = channelRegistry;
    // The CYD speaker amplifier is fed by DAC2 on GPIO26.
    services.music = std::make_shared<ql32::platform::olimex_vga::MusicService>(
        services.storage, 26, std::vector<std::string>{"SDC:/", "FFS:/"});
    services.ui = uiService;
    services.graphics = std::static_pointer_cast<ql32::graphics::Service>(graphics);
    services.network = std::shared_ptr<ql32::runtime::NetworkService>(
        &boardNetwork, [](ql32::runtime::NetworkService*) {});
    services.systemControl = std::shared_ptr<ql32::runtime::SystemControlService>(
        &boardSystemControl, [](ql32::runtime::SystemControlService*) {});
    services.memory = std::shared_ptr<ql32::runtime::MemoryService>(
        &boardMemory, [](ql32::runtime::MemoryService*) {});
    // QL/32-specific admission limits, below the measured working states
    // (4 workers + music: ~38 KB free / 17 KB largest at music start).
    ql32::runtime::AdmissionPolicy admission;
    admission.workerTotalBytes = 14 * 1024;
    admission.workerContiguousBytes = 6 * 1024;
    // Measured: first audio start + a YM song takes ~31 KB; a later song ~10 KB.
    admission.musicStartTotalBytes = 32 * 1024;
    admission.musicStartContiguousBytes = 8 * 1024;
    admission.musicPlayTotalBytes = 14 * 1024;
    admission.musicPlayContiguousBytes = 4 * 1024;
    // Full digi-drum bank (8192 samples) took ~15 KB; below this free heap fall back to the reduced bank.
    admission.musicDrumsTotalBytes = 48 * 1024;
    admission.musicDrumsContiguousBytes = 4 * 1024;
    admission.displayModeGuard = true;
    // A full-screen canvas is 38400 bytes in one block; leave room for the music service.
    admission.canvasGuard = true;
    admission.freezeWindowsAfterRun = true;
#if !defined(QL32_PROGRAM_SPILL_MIN_BYTES)
#define QL32_PROGRAM_SPILL_MIN_BYTES 4096
#endif
#if !defined(QL32_PROGRAM_SPILL_PATH)
#define QL32_PROGRAM_SPILL_PATH "FFS:.q32-run"
#endif
#if !defined(QL32_PROGRAM_SPILL_PREFERRED_PATH)
#define QL32_PROGRAM_SPILL_PREFERRED_PATH "SDC:.q32-run"
#endif
    admission.programSpillPreferredPath = QL32_PROGRAM_SPILL_PREFERRED_PATH;
    admission.programSpillPath = QL32_PROGRAM_SPILL_PATH;
    admission.programSpillMinBytes = QL32_PROGRAM_SPILL_MIN_BYTES;
    admission.canvasHeadroomBytes = 8 * 1024;
    boardMemory.setAdmissionPolicy(admission);
    boardMemory.setReclaimer([] {
        if (graphics) graphics->releaseFrozenImages();
    });
    ql32::platform::esp32::installFailedAllocLogger();
    std::static_pointer_cast<ql32::platform::olimex_vga::MusicService>(services.music)
        ->setMemoryService(services.memory);
    services.cpuFrequency = std::shared_ptr<ql32::runtime::CpuFrequencyService>(
        &boardCpuFrequency, [](ql32::runtime::CpuFrequencyService*) {});
    services.clock = std::shared_ptr<ql32::runtime::ClockService>(
        &boardClock, [](ql32::runtime::ClockService*) {});
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
    remoteCapabilities.board = "ESP32-2432S028";
#if defined(QL32_CYD_USE_SPIFFS)
    remoteCapabilities.storageRoots = {"FFS:/"};
#else
    remoteCapabilities.storageRoots = {"SDC:/"};
#endif
    remoteCapabilities.screenCapture = true;
    remoteDebugger = std::make_unique<ql32::platform::esp32::RemoteDebugProtocol>(
        std::move(remoteCapabilities));
    // QL/32-specific: tile-sized worker canvases by default (as WINDOWMODE
    // ISOLATED); the startup program is compiled, so it cannot issue the command.
    (void)scheduler->setWindowMode(ql32::runtime::WindowMode::Isolated);
    replTask = scheduler->launchInteractive(*basicOutput, std::move(startupProgram));
}

void loop() {
    while (Serial.available() > 0) {
        // The PC debugger must be able to control a paused worker even while
        // the focused BASIC task is running or waiting for a UI response.
        if (!applicationInputOwner && !acceptsInput() && !protocolInputLine && Serial.peek() != '@') break;
        receiveSerialCharacter(static_cast<char>(Serial.read()));
    }
    pollTouch();
    if (uiPresenter) uiPresenter->flushSerialEscape(millis());
    advanceScheduler();
    // Sleep only when nothing ran, and at least every 32 ms so lower-priority tasks and the idle task get time.
    static std::uint32_t lastSleepMs = 0;
    const auto nowMs = millis();
    if (!schedulerRanSlice || nowMs - lastSleepMs >= 32) {
        lastSleepMs = nowMs;
        delay(1);
    } else {
        yield();
    }
}
