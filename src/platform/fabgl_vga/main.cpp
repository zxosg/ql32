#include <Arduino.h>
#include <SD.h>
#if defined(QL32_OLIMEX_USE_SPIFFS)
#include <SPIFFS.h>
#endif
#include <SPI.h>
#include <driver/gpio.h>
#include <fabgl.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <memory>
#include <ostream>
#include <optional>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "capabilities.hpp"
#include "pins.hpp"
#include "ql32/vm/scheduler.hpp"
#include "ql32/platform/esp32/sd_channels.hpp"
#include "ql32/platform/esp32/idf_sd.hpp"
#include "ql32/platform/esp32/drive_router.hpp"
#include "ql32/platform/esp32/clock.hpp"
#include "ql32/platform/esp32/arduino_hardware.hpp"
#include "ql32/platform/esp32/cpu_frequency.hpp"
#include "ql32/ui/graphic_presenter.hpp"
#include "ql32/platform/esp32/memory.hpp"
#include "ql32/platform/esp32/network.hpp"
#include "ql32/platform/esp32/sd_storage.hpp"
#include "ql32/platform/esp32/system_control.hpp"
#include "ql32/platform/esp32/monitor_transfer.hpp"
#include "ql32/platform/esp32/remote_debug_protocol.hpp"
#include "ql32/graphics/windowed_service.hpp"
#include "ql32/platform/fabgl_vga/graphics.hpp"
#include "ql32/platform/fabgl_vga/frame_presenter.hpp"
#include "ql32/platform/fabgl_vga/keyboard.hpp"

#if defined(QL32_BOARD_OLIMEX_ESP32_SBC_FABGL)
namespace ql32_board = ql32::boards::olimex_esp32_sbc_fabgl;
constexpr char kBoardName[] = "Olimex ESP32-SBC-FabGL";
#elif defined(QL32_BOARD_LILYGO_VGA32)
namespace ql32_board = ql32::boards::lilygo_vga32;
constexpr char kBoardName[] = "LILYGO VGA32 V1.4";
#else
#error "Select a supported FabGL VGA board profile"
#endif

namespace {

constexpr std::uint32_t consoleRows = 3;
constexpr std::uint32_t consoleCharacterWidth = 6;
constexpr std::uint32_t consoleCharacterHeight = 8;
constexpr std::uint32_t consoleHeight = consoleRows * consoleCharacterHeight;
// QL mode-8 ink 4 is green (palette order: black, blue, red, magenta, green...).
constexpr std::uint8_t consoleInk = 4;
constexpr std::array<std::array<std::uint8_t, 3>, 16> consolePalette{{
    {{0, 0, 0}}, {{0, 0, 255}}, {{255, 0, 0}}, {{255, 0, 255}},
    {{0, 255, 0}}, {{0, 255, 255}}, {{255, 255, 0}}, {{255, 255, 255}},
    {{85, 85, 85}}, {{85, 85, 255}}, {{255, 85, 85}}, {{255, 85, 255}},
    {{85, 255, 85}}, {{85, 255, 255}}, {{255, 255, 85}}, {{192, 192, 192}}
}};

class VgaConsole {
public:
    explicit VgaConsole(fabgl::Canvas& canvas) : canvas_(canvas) {
        lines_.emplace_back();
    }

    void begin() {
        bounds_ = {0, std::max(0, canvas_.getHeight() - static_cast<int>(consoleHeight)),
                   static_cast<std::uint32_t>(canvas_.getWidth()), consoleHeight};
        updateGeometry();
        canvas_.selectFont(&fabgl::FONT_6x8);
        redraw(true);
    }

    void setBounds(ql32::graphics::Rect bounds) {
        if (bounds.width == 0 || bounds.height == 0) return;
        bounds.x = std::clamp<std::int32_t>(bounds.x, 0, canvas_.getWidth());
        bounds.y = std::clamp<std::int32_t>(bounds.y, 0, canvas_.getHeight());
        bounds.width = std::min<std::uint32_t>(bounds.width,
            static_cast<std::uint32_t>(canvas_.getWidth() - bounds.x));
        bounds.height = std::min<std::uint32_t>(bounds.height,
            static_cast<std::uint32_t>(canvas_.getHeight() - bounds.y));
        bounds_ = bounds;
        updateGeometry();
        redraw(true);
    }

    void setColors(ql32::graphics::ColorIndex ink, ql32::graphics::ColorIndex paper) {
        ink_ = ink % consolePalette.size();
        paper_ = paper % consolePalette.size();
        redraw(true);
    }

    void clear() {
        lines_.clear();
        lines_.emplace_back();
        redraw(true);
    }

    void restoreOverlay(ql32::graphics::Rect region) {
        if (invalidate_) return;
        redraw(false, region);
    }

    void setInvalidateCallback(std::function<void(ql32::graphics::Rect)> callback) {
        invalidate_ = std::move(callback);
    }

    void composePaletteRegionRow(std::uint32_t y, std::uint32_t xOrigin,
                                 std::vector<ql32::graphics::ColorIndex>& pixels) const {
        if (y < static_cast<std::uint32_t>(bounds_.y) ||
            y >= static_cast<std::uint32_t>(bounds_.y) + bounds_.height) return;
        const auto localY = y - static_cast<std::uint32_t>(bounds_.y);
        const auto textRow = localY / consoleCharacterHeight;
        const auto glyphRow = localY % consoleCharacterHeight;
        const std::string* line = textRow < lines_.size() ? &lines_[textRow] : nullptr;
        for (std::uint32_t i = 0; i < pixels.size(); ++i) {
            const auto x = xOrigin + i;
            if (x < static_cast<std::uint32_t>(bounds_.x) ||
                x >= static_cast<std::uint32_t>(bounds_.x) + bounds_.width) continue;
            bool foreground = false;
            const auto localX = x - static_cast<std::uint32_t>(bounds_.x);
            const auto column = localX / consoleCharacterWidth;
            const auto glyphColumn = localX % consoleCharacterWidth;
            if (line && glyphRow < 7 && column < line->size() && glyphColumn < 5) {
                const auto glyph = ql32::graphics::classicGlyphColumns((*line)[column]);
                foreground = (glyph[glyphColumn] & (1u << glyphRow)) != 0;
            }
            pixels[i] = foreground ? ink_ : paper_;
        }
    }

    void write(const char* data, std::size_t size) {
        bool changed = false;
        for (std::size_t index = 0; index < size; ++index) {
            const unsigned char character = static_cast<unsigned char>(data[index]);
            if (character == '\r') continue;
            if (character == '\n') {
                lines_.emplace_back();
                if (lines_.size() > rows_) lines_.pop_front();
                changed = true;
                continue;
            }
            if (character == '\b' || character == 127) {
                if (!lines_.back().empty()) {
                    lines_.back().pop_back();
                    changed = true;
                }
                continue;
            }
            if (character < 32 || character >= 127) continue;
            if (lines_.back().size() >= columns_) {
                lines_.emplace_back();
                if (lines_.size() > rows_) lines_.pop_front();
                changed = true;
            }
            lines_.back().push_back(static_cast<char>(character));
            changed = true;
        }
        if (changed) redraw(false);
    }

private:
    void updateGeometry() {
        columns_ = static_cast<std::size_t>(std::max<std::uint32_t>(1,
            bounds_.width / consoleCharacterWidth));
        rows_ = static_cast<std::size_t>(std::max<std::uint32_t>(1,
            bounds_.height / consoleCharacterHeight));
        firstRowY_ = bounds_.y;
        while (lines_.size() > rows_) lines_.pop_front();
    }

    void redraw(bool clearBackground, ql32::graphics::Rect region = {}) {
        if (invalidate_) {
            invalidate_(region.width ? region : bounds_);
            return;
        }
        if (bounds_.width == 0 || bounds_.height == 0) return;
        const auto left = std::max<std::int32_t>(bounds_.x, region.width ? region.x : bounds_.x);
        const auto top = std::max<std::int32_t>(bounds_.y, region.height ? region.y : bounds_.y);
        const auto right = std::min<std::int32_t>(bounds_.x + bounds_.width,
            region.width ? region.x + static_cast<std::int32_t>(region.width) :
                           bounds_.x + bounds_.width);
        const auto bottom = std::min<std::int32_t>(bounds_.y + bounds_.height,
            region.height ? region.y + static_cast<std::int32_t>(region.height) :
                            bounds_.y + bounds_.height);
        if (left >= right || top >= bottom) return;
        canvas_.beginUpdate();
        canvas_.setClippingRect(fabgl::Rect(left, top, right - 1, bottom - 1));
        if (clearBackground) {
            const auto& paper = consolePalette[paper_];
            canvas_.setBrushColor(paper[0], paper[1], paper[2]);
            canvas_.fillRectangle(left, top, right - 1, bottom - 1);
        }
        canvas_.selectFont(&fabgl::FONT_6x8);
        const auto& ink = consolePalette[ink_];
        canvas_.setPenColor(ink[0], ink[1], ink[2]);
        canvas_.setGlyphOptions(fabgl::GlyphOptions().FillBackground(false));
        for (std::size_t row = 0; row < lines_.size(); ++row)
            canvas_.drawText(bounds_.x,
                             firstRowY_ + static_cast<int>(row * consoleCharacterHeight),
                             lines_[row].c_str());
        canvas_.setClippingRect(fabgl::Rect(0, 0, canvas_.getWidth() - 1,
                                           canvas_.getHeight() - 1));
        canvas_.endUpdate();
        canvas_.waitCompletion(true);
    }

    fabgl::Canvas& canvas_;
    std::deque<std::string> lines_;
    std::size_t columns_ = 1;
    std::size_t rows_ = consoleRows;
    int firstRowY_ = 0;
    ql32::graphics::Rect bounds_{};
    std::uint8_t ink_ = consoleInk;
    std::uint8_t paper_ = 0;
    std::function<void(ql32::graphics::Rect)> invalidate_;
};

extern VgaConsole screenConsole;

class SerialOutputBuffer final : public std::streambuf {
protected:
    int_type overflow(int_type character) override {
        if (traits_type::eq_int_type(character, traits_type::eof()))
            return traits_type::not_eof(character);
        const char value = traits_type::to_char_type(character);
        screenConsole.write(&value, 1);
        return Serial.write(static_cast<std::uint8_t>(character)) == 1
            ? character : traits_type::eof();
    }

    std::streamsize xsputn(const char* data, std::streamsize size) override {
        if (size <= 0) return 0;
        screenConsole.write(data, static_cast<std::size_t>(size));
        return static_cast<std::streamsize>(Serial.write(
            reinterpret_cast<const std::uint8_t*>(data), static_cast<std::size_t>(size)));
    }
};

fabgl::VGAController display;
fabgl::Canvas canvas(&display);
VgaConsole screenConsole(canvas);
fabgl::PS2Controller ps2;
std::shared_ptr<ql32::graphics::WindowedService> graphics;
std::optional<ql32::platform::fabgl_vga::FabglKeyboard> keyboardInput;
ql32::platform::esp32::Clock boardClock;
ql32::platform::esp32::CpuFrequency boardCpuFrequency;
ql32::platform::esp32::MemoryService boardMemory;
ql32::platform::esp32::WifiNetworkService boardNetwork;
ql32::platform::esp32::SystemControl boardSystemControl;
SPIClass sdBus(HSPI);
SPIClass userSpiBus(VSPI);
ql32::platform::esp32::ArduinoHardware hardwareAdapter(
#if defined(QL32_BOARD_OLIMEX_ESP32_SBC_FABGL)
    &userSpiBus, nullptr,
#else
    &sdBus, &userSpiBus,
#endif
    &Wire, 19, 22, 17, 16, 4, 0,
    {21, 18, 5, 23, 15, 32, 33, 27, 26, 25, 12, 14, 13, 19, 22,
#if defined(QL32_BOARD_OLIMEX_ESP32_SBC_FABGL)
     35
#else
     2
#endif
    });
ql32::platform::esp32::SdStorage sdStorage(SD);
ql32::platform::esp32::SdChannels sdChannels(SD);
#if defined(QL32_OLIMEX_USE_SPIFFS)
ql32::platform::esp32::SdStorage flashStorage(SPIFFS, "FFS");
ql32::platform::esp32::SdChannels flashChannels(SPIFFS, "FFS");
#endif
#if defined(QL32_BOARD_OLIMEX_ESP32_SBC_FABGL)
ql32::platform::esp32::IdfSdCard idfSdCard;
ql32::platform::esp32::IdfSdStorage idfSdStorage(idfSdCard);
ql32::platform::esp32::IdfSdChannels idfSdChannels(idfSdCard);
#if defined(QL32_OLIMEX_USE_SPIFFS)
ql32::platform::esp32::DriveRouterStorage driveStorage(flashStorage, idfSdStorage);
ql32::platform::esp32::DriveRouterChannels driveChannels(flashChannels, idfSdChannels);
#endif
#endif
SerialOutputBuffer serialBuffer;
std::ostream serialOutput(&serialBuffer);
std::unique_ptr<ql32::vm::SessionScheduler> scheduler;
ql32::vm::TaskId replTask = 0;
std::shared_ptr<ql32::ui::QueuedService> uiService =
    std::make_shared<ql32::ui::QueuedService>();
std::unique_ptr<ql32::ui::GraphicPresenter> uiPresenter;
std::unique_ptr<ql32::platform::esp32::RemoteDebugProtocol> remoteDebugger;
std::string inputLine;
bool protocolInputLine = false;
bool serialSawCarriageReturn = false;
bool replReadyMessageShown = false;
bool replPromptShown = false;

#if defined(QL32_BOARD_OLIMEX_ESP32_SBC_FABGL)
ql32::runtime::YModemSource screenCaptureSource() {
    const auto width = static_cast<std::uint32_t>(canvas.getWidth());
    const auto height = static_cast<std::uint32_t>(canvas.getHeight());
    constexpr std::uint32_t headerSize = 54;
    const std::uint32_t rowBytes = (width * 3 + 3) & ~3u;
    const std::uint32_t imageSize = rowBytes * height;
    std::array<std::uint8_t, headerSize> header{};
    const auto put16 = [&header](std::size_t at, std::uint16_t value) {
        header[at] = static_cast<std::uint8_t>(value);
        header[at + 1] = static_cast<std::uint8_t>(value >> 8);
    };
    const auto put32 = [&header](std::size_t at, std::uint32_t value) {
        for (unsigned byte = 0; byte < 4; ++byte)
            header[at + byte] = static_cast<std::uint8_t>(value >> (byte * 8));
    };
    header[0] = 'B'; header[1] = 'M';
    put32(2, headerSize + imageSize); put32(10, headerSize); put32(14, 40);
    put32(18, width); put32(22, height); put16(26, 1); put16(28, 24);
    put32(34, imageSize); put32(38, 2835); put32(42, 2835);
    canvas.waitCompletion(true);
    const std::string fileHeader(reinterpret_cast<const char*>(header.data()), header.size());
    ql32::runtime::YModemSource source;
    source.filename = "ql32-screen.bmp";
    source.size = headerSize + imageSize;
    source.read = [fileHeader, width, height, rowBytes, imageSize,
                   row = std::vector<std::uint8_t>(rowBytes)](
        std::uint64_t offset, std::uint8_t* destination, std::size_t count) mutable {
        std::size_t written = 0;
        while (written < count) {
            if (offset < fileHeader.size()) {
                const auto start = static_cast<std::size_t>(offset);
                const auto part = std::min(count - written, fileHeader.size() - start);
                std::memcpy(destination + written, fileHeader.data() + start, part);
                offset += part; written += part; continue;
            }
            const auto bodyOffset = offset - fileHeader.size();
            if (bodyOffset >= imageSize)
                throw std::out_of_range("screen capture read exceeds BMP image size");
            const auto bitmapRow = static_cast<std::uint32_t>(bodyOffset / rowBytes);
            const auto y = static_cast<int>(height - 1 - bitmapRow);
            const auto firstByte = static_cast<std::size_t>(bodyOffset % rowBytes);
            std::fill(row.begin(), row.end(), 0);
            for (std::uint32_t x = 0; x < width; ++x) {
                const auto color = canvas.getPixel(static_cast<int>(x), y);
                row[x * 3] = color.B;
                row[x * 3 + 1] = color.G;
                row[x * 3 + 2] = color.R;
            }
            const auto part = std::min(count - written, row.size() - firstByte);
            std::memcpy(destination + written, row.data() + firstByte, part);
            offset += part; written += part;
        }
    };
    return source;
}
#endif

void appendInputCharacter(char character) {
    if (inputLine.size() >= 1024) return;
    if (inputLine.empty() && character == '@') protocolInputLine = true;
    inputLine.push_back(character);
    if (!protocolInputLine) {
        Serial.write(static_cast<std::uint8_t>(character));
        screenConsole.write(&character, 1);
    }
}

void eraseInputCharacter() {
    if (inputLine.empty()) return;
    inputLine.pop_back();
    if (!protocolInputLine) {
        Serial.print("\b \b");
        screenConsole.write("\b", 1);
    }
}

void submitInputLine() {
    Serial.println();
    if (!protocolInputLine) screenConsole.write("\n", 1);
    if (remoteDebugger && remoteDebugger->handles(inputLine)) {
#if defined(QL32_OLIMEX_USE_SPIFFS) && defined(QL32_BOARD_OLIMEX_ESP32_SBC_FABGL)
        auto& protocolStorage = static_cast<ql32::runtime::StorageService&>(driveStorage);
#elif defined(QL32_OLIMEX_USE_SPIFFS)
        auto& protocolStorage = static_cast<ql32::runtime::StorageService&>(flashStorage);
#elif defined(QL32_BOARD_OLIMEX_ESP32_SBC_FABGL)
        auto& protocolStorage = static_cast<ql32::runtime::StorageService&>(idfSdStorage);
#else
        auto& protocolStorage = static_cast<ql32::runtime::StorageService&>(sdStorage);
#endif
        remoteDebugger->handle(inputLine, *scheduler, protocolStorage, serialOutput,
            [](std::string_view bytes) {
                Serial.write(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
                Serial.flush();
            });
    } else if (uiPresenter && uiPresenter->active()) {
        (void)uiPresenter->consumeLine(inputLine);
    } else if (ql32::platform::esp32::handleMonitorTransferCommand(
#if defined(QL32_OLIMEX_USE_SPIFFS) && defined(QL32_BOARD_OLIMEX_ESP32_SBC_FABGL)
                   inputLine, driveStorage, driveChannels, Serial,
                   [] { return screenCaptureSource(); })) {
#elif defined(QL32_OLIMEX_USE_SPIFFS)
                   inputLine, flashStorage, flashChannels, Serial
#if defined(QL32_BOARD_OLIMEX_ESP32_SBC_FABGL)
                   , [] { return screenCaptureSource(); }
#endif
                   )) {
#elif defined(QL32_BOARD_OLIMEX_ESP32_SBC_FABGL)
                   inputLine, idfSdStorage, idfSdChannels, Serial,
                   [] { return screenCaptureSource(); })) {
#else
                   inputLine, sdStorage, sdChannels, Serial)) {
#endif
    } else if (scheduler && !inputLine.empty() &&
               !scheduler->submit(scheduler->focusedTask(), inputLine + "\n")) {
        Serial.println("BASIC session is busy");
    }
    inputLine.clear();
    protocolInputLine = false;
    replPromptShown = false;
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
    if (character == '\b' || character == 127) {
        eraseInputCharacter();
    } else if ((character >= 32 && character <= 126) ||
               (protocolInputLine && character == '\t')) {
        appendInputCharacter(character);
    }
}

bool acceptsInput() {
    if (uiPresenter && uiPresenter->active()) return true;
    if (!scheduler) return false;
    const auto state = scheduler->task(scheduler->focusedTask());
    return state && state->status == ql32::vm::TaskStatus::WaitingInput;
}

void advanceScheduler() {
    if (!scheduler) return;
    if (!scheduler->executionPaused()) (void)scheduler->tick();
    if (uiPresenter) uiPresenter->refresh();

    const auto tasks = scheduler->tasks();
    for (const auto& task : tasks) {
        const std::string error = scheduler->takeError(task.id);
        if (!error.empty()) {
            Serial.printf("BASIC session %llu VM error: %s\n",
                          static_cast<unsigned long long>(task.id), error.c_str());
            char consoleError[256];
            const int length = std::snprintf(consoleError, sizeof(consoleError),
                "BASIC session %llu VM error: %s\n",
                static_cast<unsigned long long>(task.id), error.c_str());
            if (length > 0)
                screenConsole.write(consoleError,
                    std::min<std::size_t>(static_cast<std::size_t>(length),
                                          sizeof(consoleError) - 1));
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
            Serial.println("BASIC REPL ready (USB serial and PS/2 keyboard)");
            screenConsole.write("BASIC REPL ready\n", 17);
            replReadyMessageShown = true;
        }
        if (!replPromptShown) {
            if (focused == replTask) {
                Serial.print("> ");
                screenConsole.write("> ", 2);
            } else {
                char prompt[32];
                const int length = std::snprintf(prompt, sizeof(prompt), "task %llu> ",
                    static_cast<unsigned long long>(focused));
                if (length > 0) {
                    Serial.write(reinterpret_cast<const std::uint8_t*>(prompt),
                                 static_cast<std::size_t>(length));
                    screenConsole.write(prompt, static_cast<std::size_t>(length));
                }
            }
            replPromptShown = true;
        }
    } else {
        replPromptShown = false;
    }
}

bool handleKeyboardShortcut(const ql32::input::KeyEvent& event) {
    if (!event.pressed || !scheduler) return false;
    if (event.key == ql32::input::Key::Delete && event.control && event.alt) {
        boardSystemControl.reboot();
        return true;
    }

    if (event.key != ql32::input::Key::Pause &&
        event.key != ql32::input::Key::Break)
        return false;

    const bool control = event.control || event.key == ql32::input::Key::Break;
    if (control && event.shift) {
        scheduler->stopWorkersAndReturnToSupervisor();
        Serial.println("All worker tasks stopped; supervisor ready");
    } else if (control) {
        const auto task = scheduler->focusedTask();
        if (scheduler->breakTaskExecution(task))
            Serial.printf("Break: task %llu returned to its prompt\n",
                          static_cast<unsigned long long>(task));
    } else if (event.shift) {
        const bool paused = scheduler->toggleExecutionPaused();
        Serial.println(paused ? "VM paused" : "VM resumed");
    } else {
        const auto task = scheduler->focusedTask();
        const auto paused = scheduler->toggleTaskPaused(task);
        if (paused)
            Serial.printf("Task %llu %s\n", static_cast<unsigned long long>(task),
                          *paused ? "paused" : "resumed");
    }
    return true;
}

} // namespace

void setup() {
    Serial.begin(115200);
    userSpiBus.begin();

    if (!ql32_board::capabilities().valid()) {
        Serial.printf("QL/32 %s capability profile invalid\n", kBoardName);
        return;
    }

    display.begin(
        static_cast<gpio_num_t>(ql32_board::vgaRed),
        static_cast<gpio_num_t>(ql32_board::vgaGreen),
        static_cast<gpio_num_t>(ql32_board::vgaBlue),
        static_cast<gpio_num_t>(ql32_board::vgaHSync),
        static_cast<gpio_num_t>(ql32_board::vgaVSync));
    display.setResolution(VGA_320x200_70Hz);

    screenConsole.begin();
    auto framePresenter = std::make_shared<ql32::platform::fabgl_vga::FabglFramePresenter>(
        canvas, [](ql32::graphics::Rect region) { screenConsole.restoreOverlay(region); });
    const auto screenWidth = static_cast<std::uint32_t>(canvas.getWidth());
    const auto screenHeight = static_cast<std::uint32_t>(canvas.getHeight());
    const std::size_t fullScreen4BitBytes =
        (static_cast<std::size_t>(screenWidth) * screenHeight + 1) / 2;
    graphics = std::make_shared<ql32::graphics::WindowedService>(
        screenWidth, screenHeight, framePresenter, 4,
        fullScreen4BitBytes * 2);
    graphics->setDefaultWindowBounds(0,
        {0, static_cast<std::int32_t>(screenHeight - consoleHeight), screenWidth, consoleHeight});
    graphics->setDefaultWindowBounds(1,
        {0, 0, screenWidth, screenHeight - consoleHeight});
    graphics->setConsoleBoundsCallback([](ql32::graphics::Rect bounds) {
        screenConsole.setBounds(bounds);
    });
    graphics->setConsoleColorsCallback([](ql32::graphics::ColorIndex ink,
                                          ql32::graphics::ColorIndex paper) {
        screenConsole.setColors(ink, paper);
    });
    graphics->setConsoleClearCallback([] { screenConsole.clear(); });

    ps2.begin(fabgl::PS2Preset::KeyboardPort0);
    if (auto* keyboard = fabgl::PS2Controller::keyboard()) {
        Serial.println(keyboard->isKeyboardAvailable()
            ? "PS/2 keyboard detected on port 0"
            : "No PS/2 keyboard response on port 0");
        keyboardInput.emplace(*keyboard);
    } else {
        Serial.println("FabGL keyboard interface unavailable on port 0");
    }

#if defined(QL32_OLIMEX_USE_SPIFFS) && defined(QL32_BOARD_OLIMEX_ESP32_SBC_FABGL)
    const bool ffsStorageReady = SPIFFS.begin(false);
    const bool sdcStorageReady = idfSdCard.mount({
        ql32_board::sdMosi, ql32_board::sdMiso, ql32_board::sdClock,
        ql32_board::sdChipSelect
    });
    const bool storageReady = ffsStorageReady || sdcStorageReady;
#elif defined(QL32_OLIMEX_USE_SPIFFS)
#if !defined(QL32_BOARD_OLIMEX_ESP32_SBC_FABGL)
    sdBus.begin(ql32_board::sdClock, ql32_board::sdMiso,
                ql32_board::sdMosi, ql32_board::sdChipSelect);
#endif
    const bool storageReady = SPIFFS.begin(false);
#elif defined(QL32_BOARD_OLIMEX_ESP32_SBC_FABGL)
    const bool storageReady = idfSdCard.mount({
        ql32_board::sdMosi, ql32_board::sdMiso, ql32_board::sdClock,
        ql32_board::sdChipSelect
    });
#else
    sdBus.begin(ql32_board::sdClock,
                ql32_board::sdMiso,
                ql32_board::sdMosi,
                ql32_board::sdChipSelect);
    const bool storageReady = SD.begin(ql32_board::sdChipSelect,
                                       sdBus, 4000000, "/sd", 5);
#endif

    Serial.printf("QL/32 %s bring-up ready\n", kBoardName);
    Serial.printf("Uptime ms: %llu\n",
                  static_cast<unsigned long long>(boardClock.monotonicMilliseconds()));
    const auto wallTime = boardClock.unixTimeSeconds();
    if (wallTime)
        Serial.printf("Unix time: %lld\n", static_cast<long long>(*wallTime));
    else
        Serial.println("Wall clock unsynchronized");
#if defined(QL32_OLIMEX_USE_SPIFFS) && defined(QL32_BOARD_OLIMEX_ESP32_SBC_FABGL)
    if (ffsStorageReady) Serial.println("FFS storage ready");
    else Serial.println("FFS mount failed");
    if (sdcStorageReady) Serial.println("SDC storage ready");
    else Serial.printf("SDC mount failed: %s\n", idfSdCard.error().c_str());
#else
    if (storageReady) {
#if defined(QL32_OLIMEX_USE_SPIFFS)
        Serial.println("FFS storage ready");
#else
        Serial.println("SD storage ready");
#endif
    } else {
        Serial.println("Storage unavailable");
#if defined(QL32_OLIMEX_USE_SPIFFS)
        Serial.println("FFS mount failed");
#else
#if defined(QL32_BOARD_OLIMEX_ESP32_SBC_FABGL)
        Serial.printf("SD diagnostic: %s\n", idfSdCard.error().c_str());
#endif
#endif
    }
#endif

    auto graphicsService = std::static_pointer_cast<ql32::graphics::Service>(graphics);
    std::string startupProgram =
        "INK 7\nTEXT 8,8,\"QL/32 VGA32 V1.4\"\n"
        "TEXT 8,24,\"FabGL VGA + PS/2 ready\"\n"
        "TEXT 8,36,\"BASIC prompt on PS/2 keyboard\"\n"
        "RECT 8,48,96,48\nLINE 8,48,103,95\nCIRCLE 150,72,24\n"
        "TEXT 8,112,\"BASIC VM ready\"\n"
        "PRINT \"QL/32 BASIC VM running\"\n";
#if defined(QL32_OLIMEX_USE_SPIFFS) && defined(QL32_BOARD_OLIMEX_ESP32_SBC_FABGL)
    bool startupLoaded = false;
    if (sdcStorageReady) {
        try {
            startupProgram = idfSdStorage.readText("SDC:/startup.bas");
            startupLoaded = true;
            Serial.println("Running SDC:/startup.bas");
        } catch (const ql32::runtime::ServiceError& error) {
            if (error.code() != ql32::runtime::ServiceErrorCode::NotFound) {
                Serial.printf("SDC startup read error: %s\n", error.what());
                return;
            }
            Serial.println("SDC:/startup.bas not found");
        }
    }
    if (!startupLoaded && ffsStorageReady) {
        try {
            startupProgram = flashStorage.readText("FFS:/startup.bas");
            startupLoaded = true;
            Serial.println("Running FFS:/startup.bas");
        } catch (const ql32::runtime::ServiceError& error) {
            if (error.code() != ql32::runtime::ServiceErrorCode::NotFound) {
                Serial.printf("FFS startup read error: %s\n", error.what());
                return;
            }
            Serial.println("FFS:/startup.bas not found; running built-in demo");
        }
    }
#elif defined(QL32_OLIMEX_USE_SPIFFS)
    if (storageReady) {
        try {
            startupProgram = flashStorage.readText("FFS:/startup.bas");
            Serial.println("Running FFS:/startup.bas");
        } catch (const ql32::runtime::ServiceError& error) {
            if (error.code() != ql32::runtime::ServiceErrorCode::NotFound) {
                Serial.printf("FFS startup read error: %s\n", error.what());
                return;
            }
            Serial.println("FFS:/startup.bas not found; running built-in demo");
        }
    }
#else
    if (storageReady) {
        try {
#if defined(QL32_BOARD_OLIMEX_ESP32_SBC_FABGL)
            startupProgram = idfSdStorage.readText("startup.bas");
#else
            startupProgram = sdStorage.readText("startup.bas");
#endif
            Serial.println("Running SD:/startup.bas");
        } catch (const ql32::runtime::ServiceError& error) {
            if (error.code() != ql32::runtime::ServiceErrorCode::NotFound) {
                Serial.printf("SD startup read error: %s\n", error.what());
                return;
            }
            Serial.println("SD:/startup.bas not found; running built-in demo");
        }
    }
#endif

    ql32::vm::Services services;
#if defined(QL32_OLIMEX_USE_SPIFFS) && defined(QL32_BOARD_OLIMEX_ESP32_SBC_FABGL)
    services.storage = std::shared_ptr<ql32::runtime::StorageService>(
        &driveStorage, [](ql32::runtime::StorageService*) {});
    auto fileChannels = std::shared_ptr<ql32::runtime::ChannelService>(
        &driveChannels, [](ql32::runtime::ChannelService*) {});
#elif defined(QL32_OLIMEX_USE_SPIFFS)
    services.storage = std::shared_ptr<ql32::runtime::StorageService>(
        &flashStorage, [](ql32::runtime::StorageService*) {});
    auto fileChannels = std::shared_ptr<ql32::runtime::ChannelService>(
        &flashChannels, [](ql32::runtime::ChannelService*) {});
#elif defined(QL32_BOARD_OLIMEX_ESP32_SBC_FABGL)
    services.storage = std::shared_ptr<ql32::runtime::StorageService>(
        &idfSdStorage, [](ql32::runtime::StorageService*) {});
    auto fileChannels = std::shared_ptr<ql32::runtime::ChannelService>(
        &idfSdChannels, [](ql32::runtime::ChannelService*) {});
#else
    services.storage = std::shared_ptr<ql32::runtime::StorageService>(
        &sdStorage, [](ql32::runtime::StorageService*) {});
    auto fileChannels = std::shared_ptr<ql32::runtime::ChannelService>(
        &sdChannels, [](ql32::runtime::ChannelService*) {});
#endif
    services.channels = std::make_shared<ql32::runtime::ChannelRegistry>(
        std::move(fileChannels), [](std::string_view bytes) {
            Serial.write(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
        }, std::shared_ptr<ql32::runtime::HardwareChannelService>(
            &hardwareAdapter, [](ql32::runtime::HardwareChannelService*) {}));
    services.ui = uiService;
    services.graphics = graphicsService;
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
        screenConsole.composePaletteRegionRow(y, x, row);
    });
    screenConsole.setInvalidateCallback([](ql32::graphics::Rect region) {
        graphics->invalidateSystemRegion(region);
    });
    uiPresenter = std::make_unique<ql32::ui::GraphicPresenter>(uiService, graphics);
    scheduler = std::make_unique<ql32::vm::SessionScheduler>(std::move(services), 128);
    ql32::platform::esp32::RemoteDebugCapabilities remoteCapabilities;
    remoteCapabilities.board = kBoardName;
#if defined(QL32_OLIMEX_USE_SPIFFS) && defined(QL32_BOARD_OLIMEX_ESP32_SBC_FABGL)
    remoteCapabilities.storageRoots = {"FFS:/", "SDC:/"};
#elif defined(QL32_OLIMEX_USE_SPIFFS)
    remoteCapabilities.storageRoots = {"FFS:/"};
#elif defined(QL32_BOARD_OLIMEX_ESP32_SBC_FABGL)
    remoteCapabilities.storageRoots = {"SDC:/"};
#else
    remoteCapabilities.storageRoots = {"SDC:/"};
#endif
#if defined(QL32_BOARD_OLIMEX_ESP32_SBC_FABGL)
    remoteCapabilities.screenCapture = true;
#endif
    remoteDebugger = std::make_unique<ql32::platform::esp32::RemoteDebugProtocol>(
        std::move(remoteCapabilities));
    replTask = scheduler->launchInteractive(serialOutput, std::move(startupProgram));
}

void loop() {
    while (Serial.available() > 0) {
        if (!acceptsInput() && !protocolInputLine && Serial.peek() != '@') break;
        receiveSerialCharacter(static_cast<char>(Serial.read()));
    }
    if (uiPresenter) uiPresenter->flushSerialEscape(millis());

    if (keyboardInput) {
        const auto event = keyboardInput->poll();
        const bool shortcut = event && handleKeyboardShortcut(*event);
        if (event && !shortcut && uiPresenter &&
            uiPresenter->graphicActive()) {
            (void)uiPresenter->handleKey(*event);
        } else if (event && !shortcut && event->pressed && scheduler &&
            !scheduler->executionPaused() && acceptsInput()) {
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
    advanceScheduler();
    delay(1);
}
