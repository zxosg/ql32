#include <Arduino.h>
#include <SD.h>
#if defined(QL32_OLIMEX_USE_SPIFFS)
#include <SPIFFS.h>
#endif
#include <SPI.h>
#include <driver/gpio.h>
#include <esp_heap_caps.h>
#include <fabgl.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
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
#include "ql32/platform/esp32/serial_key_decoder.hpp"
#include "ql32/ui/line_editor.hpp"
#include "ql32/graphics/windowed_service.hpp"
#include "ql32/platform/fabgl_vga/graphics.hpp"
#include "ql32/platform/fabgl_vga/frame_presenter.hpp"
#include "ql32/platform/fabgl_vga/display_modes.hpp"
#include "ql32/platform/fabgl_vga/keyboard.hpp"
#if defined(QL32_BOARD_OLIMEX_ESP32_SBC_FABGL)
#include "ql32/platform/olimex_vga/canvas_raster_ops.hpp"
#endif
#if defined(QL32_OLIMEX_DMA_DISPLAY)
#include "ql32/platform/olimex_vga/fixed_display_modes.hpp"
#include "ql32/platform/olimex_vga/frame_presenter.hpp"
#endif
#if defined(QL32_OLIMEX_RENDER_BENCHMARK) || \
    defined(QL32_OLIMEX_DMA_FRAMEBUFFER_BENCHMARK)
#include "ql32/platform/olimex_vga/framebuffer.hpp"
#include "ql32/platform/olimex_vga/palette.hpp"
#endif
#if defined(QL32_OLIMEX_DMA_TEST_SCANOUT) || defined(QL32_OLIMEX_DMA_DISPLAY)
#include "ql32/platform/olimex_vga/psram_dma_scanout.hpp"
#endif

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
    VgaConsole() {
        lines_.emplace_back();
    }

    void begin(std::uint32_t width, std::uint32_t height) {
        screenWidth_ = width;
        screenHeight_ = height;
        bounds_ = {0, std::max(0, static_cast<int>(height) -
                   static_cast<int>(consoleHeight)), width, consoleHeight};
        updateGeometry();
        redraw(true);
    }

    void setBounds(ql32::graphics::Rect bounds) {
        if (bounds.width == 0 || bounds.height == 0) return;
        bounds.x = std::clamp<std::int32_t>(bounds.x, 0, screenWidth_);
        bounds.y = std::clamp<std::int32_t>(bounds.y, 0, screenHeight_);
        bounds.width = std::min<std::uint32_t>(bounds.width,
            static_cast<std::uint32_t>(screenWidth_ - bounds.x));
        bounds.height = std::min<std::uint32_t>(bounds.height,
            static_cast<std::uint32_t>(screenHeight_ - bounds.y));
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
        cursorRow_ = cursorColumn_ = 0;
        redraw(true);
    }

    void setTextPosition(std::uint32_t row, std::uint32_t column) {
        cursorRow_ = std::min<std::size_t>(row, rows_ - 1);
        cursorColumn_ = std::min<std::size_t>(column, columns_ - 1);
        while (lines_.size() <= cursorRow_) lines_.emplace_back();
    }

    void beginInputLine(std::string_view prompt) {
        if (inputLineActive_) {
            updateInputLine({}, 0, true);
            return;
        }
        write(prompt.data(), prompt.size());
        inputLineActive_ = true;
        inputRow_ = cursorRow_;
        inputColumn_ = cursorColumn_;
        updateInputLine({}, 0, true);
    }

    void updateInputLine(std::string_view text, std::size_t cursor,
                         bool caretVisible = true) {
        if (!inputLineActive_ || inputRow_ >= lines_.size()) return;
        auto& line = lines_[inputRow_];
        line.resize(std::min(inputColumn_, line.size()));

        const auto available = columns_ - std::min(inputColumn_, columns_ - 1);
        const auto maxCursorOffset = available - 1;
        const auto viewOffset = cursor > maxCursorOffset ? cursor - maxCursorOffset : 0;
        const auto visibleCount = std::min(available, text.size() - viewOffset);
        if (visibleCount != 0)
            line.append(text.data() + viewOffset, visibleCount);

        caretRow_ = inputRow_;
        caretColumn_ = inputColumn_ + std::min(cursor - viewOffset, maxCursorOffset);
        caretVisible_ = caretVisible;
        invalidateInputRow();
    }

    void setInputCaretVisible(bool visible) {
        if (!inputLineActive_ || caretVisible_ == visible) return;
        caretVisible_ = visible;
        invalidateCaretCell();
    }

    bool inputLineActive() const noexcept { return inputLineActive_; }

    void finishInputLine(std::string_view text) {
        if (!inputLineActive_) return;
        caretVisible_ = false;
        if (inputRow_ < lines_.size())
            lines_[inputRow_].resize(std::min(inputColumn_, lines_[inputRow_].size()));
        cursorRow_ = inputRow_;
        cursorColumn_ = inputColumn_;
        inputLineActive_ = false;
        write(text.data(), text.size());
    }

    void setInvalidateCallback(std::function<void(ql32::graphics::Rect)> callback) {
        invalidate_ = std::move(callback);
        if (invalidate_ && bounds_.width && bounds_.height) invalidate_(bounds_);
    }

    void beginOutputBatch() { ++outputBatchDepth_; }

    void endOutputBatch() {
        if (outputBatchDepth_ == 0 || --outputBatchDepth_ != 0) return;
        const auto dirty = outputBatchDirty_;
        outputBatchDirty_ = {};
        if (dirty.width && dirty.height && invalidate_) invalidate_(dirty);
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
            const bool caretCell = inputLineActive_ && caretVisible_ &&
                textRow == caretRow_ && column == caretColumn_;
            pixels[i] = caretCell ? (foreground ? paper_ : ink_)
                                  : (foreground ? ink_ : paper_);
        }
    }

    void write(const char* data, std::size_t size) {
        bool changed = false;
        for (std::size_t index = 0; index < size; ++index) {
            const unsigned char character = static_cast<unsigned char>(data[index]);
            if (character == '\r') continue;
            if (character == '\n') {
                cursorColumn_ = 0;
                if (++cursorRow_ >= rows_) {
                    cursorRow_ = rows_ - 1;
                    lines_.pop_front();
                    lines_.emplace_back();
                } else if (lines_.size() <= cursorRow_) lines_.emplace_back();
                changed = true;
                continue;
            }
            if (character == '\b' || character == 127) {
                if (cursorColumn_ > 0) {
                    --cursorColumn_;
                    if (cursorRow_ < lines_.size() &&
                        cursorColumn_ < lines_[cursorRow_].size())
                        lines_[cursorRow_][cursorColumn_] = ' ';
                    changed = true;
                }
                continue;
            }
            if (character < 32 || character >= 127) continue;
            if (cursorColumn_ >= columns_) {
                cursorColumn_ = 0;
                if (++cursorRow_ >= rows_) {
                    cursorRow_ = rows_ - 1;
                    lines_.pop_front();
                    lines_.emplace_back();
                } else if (lines_.size() <= cursorRow_) lines_.emplace_back();
            }
            while (lines_.size() <= cursorRow_) lines_.emplace_back();
            auto& line = lines_[cursorRow_];
            while (line.size() <= cursorColumn_) line.push_back(' ');
            line[cursorColumn_++] = static_cast<char>(character);
            changed = true;
        }
        if (changed) redraw(false);
    }

private:
    void invalidateRegion(ql32::graphics::Rect region) {
        if (!invalidate_ || region.width == 0 || region.height == 0) return;
        if (outputBatchDepth_ == 0) {
            invalidate_(region);
            return;
        }
        if (outputBatchDirty_.width == 0 || outputBatchDirty_.height == 0) {
            outputBatchDirty_ = region;
            return;
        }
        const auto left = std::min(outputBatchDirty_.x, region.x);
        const auto top = std::min(outputBatchDirty_.y, region.y);
        const auto right = std::max<std::int64_t>(
            static_cast<std::int64_t>(outputBatchDirty_.x) + outputBatchDirty_.width,
            static_cast<std::int64_t>(region.x) + region.width);
        const auto bottom = std::max<std::int64_t>(
            static_cast<std::int64_t>(outputBatchDirty_.y) + outputBatchDirty_.height,
            static_cast<std::int64_t>(region.y) + region.height);
        outputBatchDirty_ = {left, top, static_cast<std::uint32_t>(right - left),
                             static_cast<std::uint32_t>(bottom - top)};
    }

    void invalidateInputRow() {
        if (inputRow_ >= rows_ || inputColumn_ >= columns_) return;
        const ql32::graphics::Rect region{
            bounds_.x + static_cast<std::int32_t>(inputColumn_ * consoleCharacterWidth),
            bounds_.y + static_cast<std::int32_t>(inputRow_ * consoleCharacterHeight),
            static_cast<std::uint32_t>((columns_ - inputColumn_) * consoleCharacterWidth),
            consoleCharacterHeight};
        if (invalidate_) invalidateRegion(region);
        else redraw(false, region);
    }

    void invalidateCaretCell() {
        if (caretRow_ >= rows_ || caretColumn_ >= columns_) return;
        const ql32::graphics::Rect region{
            bounds_.x + static_cast<std::int32_t>(caretColumn_ * consoleCharacterWidth),
            bounds_.y + static_cast<std::int32_t>(caretRow_ * consoleCharacterHeight),
            consoleCharacterWidth, consoleCharacterHeight};
        if (invalidate_) invalidateRegion(region);
        else redraw(false, region);
    }

    void updateGeometry() {
        columns_ = static_cast<std::size_t>(std::max<std::uint32_t>(1,
            bounds_.width / consoleCharacterWidth));
        rows_ = static_cast<std::size_t>(std::max<std::uint32_t>(1,
            bounds_.height / consoleCharacterHeight));
        firstRowY_ = bounds_.y;
        while (lines_.size() > rows_) lines_.pop_front();
    }

    void redraw(bool clearBackground, ql32::graphics::Rect region = {}) {
        (void)clearBackground;
        if (invalidate_) {
            invalidateRegion(region.width ? region : bounds_);
            return;
        }
    }

    std::deque<std::string> lines_;
    std::size_t columns_ = 1;
    std::size_t rows_ = consoleRows;
    std::size_t cursorRow_ = 0;
    std::size_t cursorColumn_ = 0;
    std::size_t inputRow_ = 0;
    std::size_t inputColumn_ = 0;
    std::size_t caretRow_ = 0;
    std::size_t caretColumn_ = 0;
    bool inputLineActive_ = false;
    bool caretVisible_ = false;
    int firstRowY_ = 0;
    std::uint32_t screenWidth_ = 320;
    std::uint32_t screenHeight_ = 200;
    ql32::graphics::Rect bounds_{};
    ql32::graphics::Rect outputBatchDirty_{};
    std::size_t outputBatchDepth_ = 0;
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
#if defined(QL32_OLIMEX_DMA_TEST_SCANOUT) || defined(QL32_OLIMEX_DMA_DISPLAY)
ql32::platform::olimex_vga::PsramDmaScanout psramDmaScanout;
#endif
fabgl::Canvas canvas(&display);
#if defined(QL32_OLIMEX_DMA_DISPLAY)
ql32::platform::olimex_vga::FixedDisplayModes displayModes;
#else
ql32::platform::fabgl_vga::DisplayModes displayModes(display, canvas);
#endif
VgaConsole screenConsole;
fabgl::PS2Controller ps2;
std::shared_ptr<ql32::graphics::WindowedService> graphics;
#if defined(QL32_GRAPHICS_PROFILE)
std::shared_ptr<ql32::platform::fabgl_vga::FabglFramePresenter> profilePresenter;
bool profilePromptPrimed = false;
#endif
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
ql32::ui::LineEditor lineEditor;
std::string inputPrompt;
ql32::platform::esp32::SerialKeyDecoder serialKeyDecoder;
int editingLineNumber = 0;
bool protocolInputLine = false;
bool serialSawCarriageReturn = false;
bool replReadyMessageShown = false;
bool replPromptShown = false;
bool inputCaretVisible = true;
std::uint32_t lastInputCaretToggle = 0;
std::uint32_t lastDebugOverlayRefresh = 0;

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

void submitInputLine();

void redrawInputLine() {
    if (protocolInputLine) {
        screenConsole.updateInputLine({}, 0, false);
        return;
    }
    screenConsole.updateInputLine(lineEditor.text(), lineEditor.cursor(), true);
    inputCaretVisible = true;
    lastInputCaretToggle = millis();
}

void echoTerminalEditorKey(const ql32::input::KeyEvent& event,
                           std::size_t oldSize, std::size_t oldCursor) {
    if (protocolInputLine) return;
    switch (event.key) {
    case ql32::input::Key::Character:
        if (oldCursor == oldSize) Serial.write(static_cast<std::uint8_t>(event.character));
        else {
            Serial.print("\x1b[@");
            Serial.write(static_cast<std::uint8_t>(event.character));
        }
        break;
    case ql32::input::Key::Backspace:
        if (oldCursor == oldSize) Serial.print("\b \b");
        else {
            Serial.write('\b');
            Serial.print("\x1b[P");
        }
        break;
    case ql32::input::Key::Delete:
        Serial.print("\x1b[P");
        break;
    case ql32::input::Key::Left:
        Serial.print("\x1b[D");
        break;
    case ql32::input::Key::Right:
        Serial.print("\x1b[C");
        break;
    case ql32::input::Key::Home: {
        Serial.print("\r");
        Serial.print(inputPrompt.c_str());
        Serial.print(lineEditor.text().c_str());
        const auto suffix = lineEditor.text().size() - lineEditor.cursor();
        if (suffix != 0) {
            char movement[24];
            std::snprintf(movement, sizeof(movement), "\x1b[%uD",
                          static_cast<unsigned>(suffix));
            Serial.print(movement);
        }
        break;
    }
    case ql32::input::Key::End: {
        const auto movement = lineEditor.text().size() - oldCursor;
        if (movement != 0) {
            char sequence[24];
            std::snprintf(sequence, sizeof(sequence), "\x1b[%uC",
                          static_cast<unsigned>(movement));
            Serial.print(sequence);
        }
        break;
    }
    case ql32::input::Key::Up:
        if (oldSize == 0) Serial.print(lineEditor.text().c_str());
        break;
    default:
        break;
    }
}

void handleEditorKey(const ql32::input::KeyEvent& event) {
    if (event.key == ql32::input::Key::Character && lineEditor.text().empty() &&
        event.character == '@')
        protocolInputLine = true;

    const auto oldSize = lineEditor.text().size();
    const auto oldCursor = lineEditor.cursor();
    const auto action = lineEditor.handleKey(event);
    if (action == ql32::ui::LineEditorAction::Submit) {
        submitInputLine();
    } else if (action == ql32::ui::LineEditorAction::Changed) {
        echoTerminalEditorKey(event, oldSize, oldCursor);
        redrawInputLine();
    }
}

void refreshInputCaretBlink() {
    if (!screenConsole.inputLineActive() || protocolInputLine) return;
    const auto now = millis();
    if (static_cast<std::uint32_t>(now - lastInputCaretToggle) < 500) return;
    lastInputCaretToggle = now;
    inputCaretVisible = !inputCaretVisible;
    screenConsole.setInputCaretVisible(inputCaretVisible);
}

ql32::input::KeyEvent toInputKey(
    const ql32::platform::esp32::SerialKeyEvent& serialKey) {
    ql32::input::KeyEvent event;
    switch (serialKey.key) {
    case ql32::platform::esp32::SerialKey::Character:
        event.key = ql32::input::Key::Character;
        event.character = static_cast<unsigned char>(serialKey.character);
        break;
    case ql32::platform::esp32::SerialKey::Enter: event.key = ql32::input::Key::Enter; break;
    case ql32::platform::esp32::SerialKey::Backspace: event.key = ql32::input::Key::Backspace; break;
    case ql32::platform::esp32::SerialKey::Delete: event.key = ql32::input::Key::Delete; break;
    case ql32::platform::esp32::SerialKey::Up: event.key = ql32::input::Key::Up; break;
    case ql32::platform::esp32::SerialKey::Left: event.key = ql32::input::Key::Left; break;
    case ql32::platform::esp32::SerialKey::Right: event.key = ql32::input::Key::Right; break;
    case ql32::platform::esp32::SerialKey::Home: event.key = ql32::input::Key::Home; break;
    case ql32::platform::esp32::SerialKey::End: event.key = ql32::input::Key::End; break;
    }
    return event;
}

void submitInputLine() {
    if (lineEditor.text().empty() && editingLineNumber > 0)
        lineEditor.setText(std::to_string(editingLineNumber));
    const std::string submittedLine = lineEditor.text();
    if (!protocolInputLine && !submittedLine.empty() &&
        (!uiPresenter || !uiPresenter->active()))
        lineEditor.rememberCurrentLine();
    if (!protocolInputLine) {
        Serial.print("\r");
        Serial.print(inputPrompt.c_str());
        Serial.print(submittedLine.c_str());
        Serial.println();
        screenConsole.finishInputLine(submittedLine);
        screenConsole.write("\n", 1);
    } else {
        Serial.println();
        screenConsole.updateInputLine({}, 0, false);
    }
    if (remoteDebugger && remoteDebugger->handles(submittedLine)) {
#if defined(QL32_OLIMEX_USE_SPIFFS) && defined(QL32_BOARD_OLIMEX_ESP32_SBC_FABGL)
        auto& protocolStorage = static_cast<ql32::runtime::StorageService&>(driveStorage);
#elif defined(QL32_OLIMEX_USE_SPIFFS)
        auto& protocolStorage = static_cast<ql32::runtime::StorageService&>(flashStorage);
#elif defined(QL32_BOARD_OLIMEX_ESP32_SBC_FABGL)
        auto& protocolStorage = static_cast<ql32::runtime::StorageService&>(idfSdStorage);
#else
        auto& protocolStorage = static_cast<ql32::runtime::StorageService&>(sdStorage);
#endif
        remoteDebugger->handle(submittedLine, *scheduler, protocolStorage, serialOutput,
            [](std::string_view bytes) {
                Serial.write(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
                Serial.flush();
            });
    } else if (uiPresenter && uiPresenter->active()) {
        (void)uiPresenter->consumeLine(submittedLine);
    } else if (ql32::platform::esp32::handleMonitorTransferCommand(
#if defined(QL32_OLIMEX_USE_SPIFFS) && defined(QL32_BOARD_OLIMEX_ESP32_SBC_FABGL)
                   submittedLine, driveStorage, driveChannels, Serial,
                   [] { return screenCaptureSource(); })) {
#elif defined(QL32_OLIMEX_USE_SPIFFS)
                   submittedLine, flashStorage, flashChannels, Serial
#if defined(QL32_BOARD_OLIMEX_ESP32_SBC_FABGL)
                   , [] { return screenCaptureSource(); }
#endif
                   )) {
#elif defined(QL32_BOARD_OLIMEX_ESP32_SBC_FABGL)
                   submittedLine, idfSdStorage, idfSdChannels, Serial,
                   [] { return screenCaptureSource(); })) {
#else
                   submittedLine, sdStorage, sdChannels, Serial)) {
#endif
    } else if (scheduler && !submittedLine.empty() &&
               !scheduler->submit(scheduler->focusedTask(), submittedLine + "\n")) {
        Serial.println("BASIC session is busy");
    }
    lineEditor.clear();
    inputPrompt.clear();
    serialKeyDecoder.reset();
    editingLineNumber = 0;
    protocolInputLine = false;
    replPromptShown = false;
}

void receiveSerialCharacter(char character) {
    if (character == '\n' && serialSawCarriageReturn) {
        serialSawCarriageReturn = false;
        return;
    }
    if (uiPresenter && uiPresenter->graphicActive() && !protocolInputLine) {
        if (character == '@' && lineEditor.text().empty()) {
            protocolInputLine = true;
            lineEditor.insertRawCharacter(character);
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
        if (character == '\b' || character == 127) {
            lineEditor.handleKey({ql32::input::Key::Backspace});
        } else if ((character >= 32 && character <= 126) || character == '\t') {
            lineEditor.insertRawCharacter(character, true);
        }
        return;
    }
    const auto key = serialKeyDecoder.feed(character);
    if (!key) return;
    handleEditorKey(toInputKey(*key));
}

bool acceptsInput() {
    if (uiPresenter && uiPresenter->active()) return true;
    if (!scheduler) return false;
    const auto status = scheduler->taskStatus(scheduler->focusedTask());
    return status && *status == ql32::vm::TaskStatus::WaitingInput;
}

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
    char line[64]{};
    std::snprintf(line, sizeof(line), "FREE:%lluK MAX:%lluK ALLOC:%llu TASK:%u %s",
        static_cast<unsigned long long>(freeBytes / 1024),
        static_cast<unsigned long long>(heap ? heap->largestFreeBlockBytes / 1024 : 0),
        static_cast<unsigned long long>(heap ? heap->allocatedBlockCount : 0),
        static_cast<unsigned>(activeTasks), clockText);
    graphics->setDebugOverlayText(line);
}

void advanceScheduler() {
    if (!scheduler) return;
    if (!scheduler->executionPaused()) (void)scheduler->tick();
    if (uiPresenter) uiPresenter->refresh();
    refreshDebugOverlay();

    scheduler->visitTaskErrors([](ql32::vm::TaskId id, const char* error, void*) {
        Serial.printf("BASIC session %llu VM error: %s\n",
                      static_cast<unsigned long long>(id), error);
        char consoleError[256];
        const int length = std::snprintf(consoleError, sizeof(consoleError),
            "BASIC session %llu VM error: %s\n",
            static_cast<unsigned long long>(id), error);
        if (length > 0)
            screenConsole.write(consoleError,
                std::min<std::size_t>(static_cast<std::size_t>(length),
                                      sizeof(consoleError) - 1));
    }, nullptr);

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
            Serial.println("BASIC REPL ready (USB serial and PS/2 keyboard)");
            screenConsole.write("BASIC REPL ready\n", 17);
            replReadyMessageShown = true;
        }
        if (!replPromptShown) {
#if defined(QL32_GRAPHICS_PROFILE)
            if (focused == replTask && profilePresenter) {
                if (!profilePromptPrimed) {
                    profilePromptPrimed = true;
                } else {
                    const auto stats = profilePresenter->presentationProfile();
                    if (stats.regionRequests != 0) {
                        Serial.printf(
                            "GFXPROFILE requests=%llu full=%llu partial=%llu fallback=%llu rows=%llu composed_px=%llu written_px=%llu compose_us=%llu convert_us=%llu write_us=%llu swap_us=%llu swap_calls=%llu swap_avg_us=%llu swap_min_us=%llu swap_max_us=%llu\n",
                            static_cast<unsigned long long>(stats.regionRequests),
                            static_cast<unsigned long long>(stats.fullFrames),
                            static_cast<unsigned long long>(stats.partialFrames),
                            static_cast<unsigned long long>(stats.fullFallbacks),
                            static_cast<unsigned long long>(stats.rowsComposed),
                            static_cast<unsigned long long>(stats.pixelsComposed),
                            static_cast<unsigned long long>(stats.pixelsWritten),
                            static_cast<unsigned long long>(stats.composeMicros),
                            static_cast<unsigned long long>(stats.convertMicros),
                            static_cast<unsigned long long>(stats.writeMicros),
                            static_cast<unsigned long long>(stats.swapMicros),
                            static_cast<unsigned long long>(stats.swapCalls),
                            static_cast<unsigned long long>(stats.swapCalls
                                ? stats.swapMicros / stats.swapCalls : 0),
                            static_cast<unsigned long long>(stats.swapMinMicros),
                            static_cast<unsigned long long>(stats.swapMaxMicros));
                    }
                }
                profilePresenter->resetPresentationProfile();
            }
#endif
            const auto edit = scheduler->takePendingLineEdit(focused);
            if (edit) {
                lineEditor.setText(std::move(edit->text));
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
            if (edit) Serial.print(lineEditor.text().c_str());
            screenConsole.beginInputLine(inputPrompt);
            redrawInputLine();
#if defined(QL32_GRAPHICS_PROFILE)
            if (focused == replTask && profilePresenter)
                profilePresenter->resetPresentationProfile();
#endif
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

#if defined(QL32_OLIMEX_RENDER_BENCHMARK) || \
    defined(QL32_OLIMEX_DMA_FRAMEBUFFER_BENCHMARK)
void clearRgb222Words(std::uint8_t* pixels, std::size_t bytes,
                      std::uint8_t value) {
    const auto pattern = static_cast<std::uint32_t>(value & 0x3Fu) * 0x01010101u;
    auto* words = reinterpret_cast<volatile std::uint32_t*>(pixels);
    const auto wordCount = bytes / sizeof(std::uint32_t);
    for (std::size_t i = 0; i < wordCount; ++i) words[i] = pattern;
    for (std::size_t i = wordCount * sizeof(std::uint32_t); i < bytes; ++i)
        pixels[i] = value & 0x3Fu;
}

void runOlimexRenderBenchmark() {
    using namespace ql32::platform::olimex_vga;
    constexpr std::uint32_t width = 320;
    constexpr std::uint32_t height = 200;
    constexpr std::uint32_t clearIterations = 50;
    constexpr std::uint32_t fillIterations = 1000;
    constexpr std::uint32_t pixelIterations = 100000;
    constexpr std::uint32_t lineIterations = 10000;
    constexpr std::uint32_t circleIterations = 500;
    constexpr std::uint32_t scrollIterations = 100;
    constexpr std::uint32_t translationIterations = 1000;
    constexpr std::uint32_t blitIterations = 1000;
    constexpr std::uint32_t bitmapSize = 64;
    constexpr std::uint32_t paletteIterations = 1000;

    auto* memory = static_cast<std::uint8_t*>(heap_caps_malloc(
        static_cast<std::size_t>(width) * height,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    auto* packedMemory = static_cast<std::uint8_t*>(heap_caps_malloc(
        (static_cast<std::size_t>(width) + 1) / 2 * height,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    auto* bitmapRgbMemory = static_cast<std::uint8_t*>(heap_caps_malloc(
        static_cast<std::size_t>(bitmapSize) * bitmapSize,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    auto* bitmapIndexedMemory = static_cast<std::uint8_t*>(heap_caps_malloc(
        static_cast<std::size_t>(bitmapSize) * bitmapSize / 2,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    auto* bitmapFabglOpaqueMemory = static_cast<std::uint8_t*>(heap_caps_malloc(
        static_cast<std::size_t>(bitmapSize) * bitmapSize,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    auto* bitmapFabglTransparentMemory = static_cast<std::uint8_t*>(heap_caps_malloc(
        static_cast<std::size_t>(bitmapSize) * bitmapSize,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!memory || !packedMemory || !bitmapRgbMemory || !bitmapIndexedMemory ||
        !bitmapFabglOpaqueMemory || !bitmapFabglTransparentMemory) {
        Serial.println("Render benchmark: unable to allocate RGB222 PSRAM framebuffer");
        heap_caps_free(memory);
        heap_caps_free(packedMemory);
        heap_caps_free(bitmapRgbMemory);
        heap_caps_free(bitmapIndexedMemory);
        heap_caps_free(bitmapFabglOpaqueMemory);
        heap_caps_free(bitmapFabglTransparentMemory);
        return;
    }
    FramebufferView framebuffer{memory, width, height, width,
                                ql32::platform::olimex_vga::PixelFormat::Rgb222};
    FramebufferView packedFramebuffer{packedMemory, width, height, width / 2,
                                      ql32::platform::olimex_vga::PixelFormat::Indexed4};
    FramebufferView bitmapIndexedView{bitmapIndexedMemory, bitmapSize, bitmapSize,
        bitmapSize / 2, ql32::platform::olimex_vga::PixelFormat::Indexed4};
    for (std::uint32_t y = 0; y < bitmapSize; ++y) {
        for (std::uint32_t x = 0; x < bitmapSize; ++x) {
            const auto value = static_cast<std::uint8_t>((x + y) & 0x3Fu);
            const bool transparent = ((x + y) & 3u) == 0;
            bitmapRgbMemory[static_cast<std::size_t>(y) * bitmapSize + x] =
                transparent ? 0 : value;
            (void)writePixel(bitmapIndexedView, x, y,
                transparent ? 0 : static_cast<std::uint8_t>(value & 0x0Fu));
            bitmapFabglOpaqueMemory[static_cast<std::size_t>(y) * bitmapSize + x] =
                static_cast<std::uint8_t>(0xC0u | value);
            bitmapFabglTransparentMemory[static_cast<std::size_t>(y) * bitmapSize + x] =
                transparent ? value : static_cast<std::uint8_t>(0xC0u | value);
        }
    }
    BitmapView bitmapRgb{bitmapRgbMemory, bitmapSize, bitmapSize, bitmapSize,
        ql32::platform::olimex_vga::PixelFormat::Rgb222};
    BitmapView bitmapIndexed{bitmapIndexedMemory, bitmapSize, bitmapSize,
        bitmapSize / 2, ql32::platform::olimex_vga::PixelFormat::Indexed4};
    fabgl::Bitmap fabglOpaque(bitmapSize, bitmapSize, bitmapFabglOpaqueMemory,
                              fabgl::PixelFormat::RGBA2222);
    fabgl::Bitmap fabglTransparent(bitmapSize, bitmapSize,
                                   bitmapFabglTransparentMemory,
                                   fabgl::PixelFormat::RGBA2222);
    volatile std::uint8_t sink = 0;

    canvas.clear();
    canvas.waitCompletion(false);
    clear(framebuffer, 0);
    clear(packedFramebuffer, 0);
    auto start = micros();
    for (std::uint32_t i = 0; i < clearIterations; ++i) canvas.clear();
    canvas.waitCompletion(false);
    const auto canvasClearUs = static_cast<std::uint32_t>(micros() - start);
    start = micros();
    for (std::uint32_t i = 0; i < clearIterations; ++i) clear(framebuffer, i & 0x3Fu);
    const auto directClearUs = static_cast<std::uint32_t>(micros() - start);
    start = micros();
    for (std::uint32_t i = 0; i < clearIterations; ++i)
        clearRgb222Words(memory, framebuffer.storageBytes(), i & 0x3Fu);
    const auto wordClearUs = static_cast<std::uint32_t>(micros() - start);
    start = micros();
    for (std::uint32_t i = 0; i < clearIterations; ++i)
        clear(packedFramebuffer, i & 0x0Fu);
    const auto packedClearUs = static_cast<std::uint32_t>(micros() - start);

    canvas.setBrushColor(255, 255, 255);
    start = micros();
    for (std::uint32_t i = 0; i < fillIterations; ++i)
        canvas.fillRectangle(17, 23, 96, 62);
    canvas.waitCompletion(false);
    const auto canvasFillUs = static_cast<std::uint32_t>(micros() - start);
    start = micros();
    for (std::uint32_t i = 0; i < fillIterations; ++i)
        fillRect(framebuffer, 17, 23, 80, 40, i & 0x3Fu);
    const auto directFillUs = static_cast<std::uint32_t>(micros() - start);
    start = micros();
    for (std::uint32_t i = 0; i < fillIterations; ++i)
        fillRect(packedFramebuffer, 17, 23, 80, 40, i & 0x0Fu);
    const auto packedFillUs = static_cast<std::uint32_t>(micros() - start);
    start = micros();
    for (std::uint32_t i = 0; i < fillIterations; ++i)
        fillRect(packedFramebuffer, 16, 23, 80, 40, i & 0x0Fu);
    const auto packedAlignedFillUs = static_cast<std::uint32_t>(micros() - start);

    canvas.setPenColor(255, 255, 255);
    start = micros();
    for (std::uint32_t i = 0; i < pixelIterations; ++i)
        canvas.setPixel((i * 73u) % width, (i * 29u) % height);
    canvas.waitCompletion(false);
    const auto canvasPixelUs = static_cast<std::uint32_t>(micros() - start);
    start = micros();
    for (std::uint32_t i = 0; i < pixelIterations; ++i)
        (void)writePixel(framebuffer, (i * 73u) % width, (i * 29u) % height,
                         static_cast<std::uint8_t>(i & 0x3Fu));
    const auto directPixelUs = static_cast<std::uint32_t>(micros() - start);

    canvas.setPenColor(255, 255, 255);
    start = micros();
    for (std::uint32_t i = 0; i < lineIterations; ++i) {
        const auto x = (i * 37u) % width;
        const auto y = (i * 19u) % height;
        canvas.drawLine(x, y, (x + 53u) % width, (y + 31u) % height);
    }
    canvas.waitCompletion(false);
    const auto canvasLineUs = static_cast<std::uint32_t>(micros() - start);
    start = micros();
    for (std::uint32_t i = 0; i < lineIterations; ++i) {
        const auto x = (i * 37u) % width;
        const auto y = (i * 19u) % height;
        drawLine(framebuffer, x, y, (x + 53u) % width,
                 (y + 31u) % height, i & 0x3Fu);
    }
    const auto directLineUs = static_cast<std::uint32_t>(micros() - start);

    canvas.setBrushColor(255, 255, 255);
    start = micros();
    for (std::uint32_t i = 0; i < circleIterations; ++i) {
        const auto x = (i * 41u) % width;
        const auto y = (i * 23u) % height;
        canvas.fillEllipse(x, y, 24, 24);
    }
    canvas.waitCompletion(false);
    const auto canvasCircleUs = static_cast<std::uint32_t>(micros() - start);
    start = micros();
    for (std::uint32_t i = 0; i < circleIterations; ++i) {
        const auto x = static_cast<std::int32_t>((i * 41u) % width + 12u);
        const auto y = static_cast<std::int32_t>((i * 23u) % height + 12u);
        drawCircle(framebuffer, x, y, 12, i & 0x3Fu, true);
    }
    const auto directCircleUs = static_cast<std::uint32_t>(micros() - start);
    start = micros();
    for (std::uint32_t i = 0; i < circleIterations; ++i) {
        const auto x = static_cast<std::int32_t>((i * 41u) % width + 12u);
        const auto y = static_cast<std::int32_t>((i * 23u) % height + 12u);
        drawCircle(packedFramebuffer, x, y, 12, i & 0x0Fu, true);
    }
    const auto packedCircleUs = static_cast<std::uint32_t>(micros() - start);

    start = micros();
    for (std::uint32_t i = 0; i < scrollIterations; ++i) {
        scrollVertical(framebuffer, 1, i & 0x3Fu);
        scrollVertical(framebuffer, -1, (i + 1) & 0x3Fu);
    }
    const auto scrollUs = static_cast<std::uint32_t>(micros() - start);
    start = micros();
    for (std::uint32_t i = 0; i < scrollIterations; ++i) {
        scrollVertical(packedFramebuffer, 1, i & 0x0Fu);
        scrollVertical(packedFramebuffer, -1, (i + 1) & 0x0Fu);
    }
    const auto packedScrollUs = static_cast<std::uint32_t>(micros() - start);
    std::array<std::uint8_t, 16> translation{};
    for (std::uint8_t i = 0; i < translation.size(); ++i)
        translation[i] = static_cast<std::uint8_t>(15 - i);
    start = micros();
    for (std::uint32_t i = 0; i < translationIterations; ++i)
        translateIndexed4(packedFramebuffer, translation);
    const auto translationUs = static_cast<std::uint32_t>(micros() - start);

    canvas.setPenColor(255, 255, 255);
    start = micros();
    for (std::uint32_t i = 0; i < blitIterations; ++i) {
        const auto x = ((i * 17u) % ((width - bitmapSize) / 2 + 1)) * 2;
        const auto y = (i * 31u) % (height - bitmapSize + 1);
        canvas.drawBitmap(x, y, &fabglOpaque);
    }
    canvas.waitCompletion(false);
    const auto canvasOpaqueBlitUs = static_cast<std::uint32_t>(micros() - start);
    start = micros();
    for (std::uint32_t i = 0; i < blitIterations; ++i) {
        const auto x = static_cast<std::int32_t>(
            ((i * 17u) % ((width - bitmapSize) / 2 + 1)) * 2);
        const auto y = static_cast<std::int32_t>((i * 31u) % (height - bitmapSize + 1));
        copyBitmapOpaque(framebuffer, x, y, bitmapRgb, 0, 0,
                         bitmapSize, bitmapSize);
    }
    const auto rgbOpaqueBlitUs = static_cast<std::uint32_t>(micros() - start);
    start = micros();
    for (std::uint32_t i = 0; i < blitIterations; ++i) {
        const auto x = static_cast<std::int32_t>(
            ((i * 17u) % ((width - bitmapSize) / 2 + 1)) * 2);
        const auto y = static_cast<std::int32_t>((i * 31u) % (height - bitmapSize + 1));
        copyBitmapOpaque(packedFramebuffer, x, y, bitmapIndexed, 0, 0,
                         bitmapSize, bitmapSize);
    }
    const auto indexedOpaqueBlitUs = static_cast<std::uint32_t>(micros() - start);

    start = micros();
    for (std::uint32_t i = 0; i < blitIterations; ++i) {
        const auto x = ((i * 17u) % ((width - bitmapSize) / 2 + 1)) * 2;
        const auto y = (i * 31u) % (height - bitmapSize + 1);
        canvas.drawBitmap(x, y, &fabglTransparent);
    }
    canvas.waitCompletion(false);
    const auto canvasTransparentBlitUs = static_cast<std::uint32_t>(micros() - start);
    start = micros();
    for (std::uint32_t i = 0; i < blitIterations; ++i) {
        const auto x = static_cast<std::int32_t>(
            ((i * 17u) % ((width - bitmapSize) / 2 + 1)) * 2);
        const auto y = static_cast<std::int32_t>((i * 31u) % (height - bitmapSize + 1));
        copyBitmapTransparent(framebuffer, x, y, bitmapRgb, 0, 0,
                              bitmapSize, bitmapSize, 0);
    }
    const auto rgbTransparentBlitUs = static_cast<std::uint32_t>(micros() - start);
    start = micros();
    for (std::uint32_t i = 0; i < blitIterations; ++i) {
        const auto x = static_cast<std::int32_t>((i * 17u) % (width - bitmapSize + 1));
        const auto y = static_cast<std::int32_t>((i * 31u) % (height - bitmapSize + 1));
        copyBitmapTransparent(packedFramebuffer, x, y, bitmapIndexed, 0, 0,
                              bitmapSize, bitmapSize, 0);
    }
    const auto indexedTransparentBlitUs = static_cast<std::uint32_t>(micros() - start);

    ql32::platform::olimex_vga::PaletteExchange paletteExchange;
    start = micros();
    for (std::uint32_t i = 0; i < paletteIterations; ++i) {
        auto* edit = paletteExchange.beginEdit();
        if (!edit) continue;
        edit->rotate(1);
        (void)paletteExchange.publish();
        (void)paletteExchange.applyAtFrameBoundary();
    }
    const auto paletteUpdateUs = static_cast<std::uint32_t>(micros() - start);
    sink = memory[0] ^ memory[static_cast<std::size_t>(width) * height - 1];
    Serial.printf("Render benchmark (320x200): clear x%u canvas RGB=%u us, PSRAM RGB memset=%u us, PSRAM RGB word=%u us, PSRAM indexed4=%u us; fill 80x40 x%u canvas=%u us direct RGB=%u us indexed4 odd=%u us even=%u us; pixel x%u canvas=%u us direct RGB=%u us; line x%u canvas=%u us direct=%u us; filled circle x%u canvas=%u us direct RGB=%u us indexed4=%u us; scroll up/down x%u direct RGB=%u us indexed4=%u us; indexed translation x%u direct=%u us; opaque 64x64 x-aligned blit x%u canvas=%u us RGB=%u us indexed4=%u us; transparent 64x64 x-aligned blit x%u canvas=%u us RGB=%u us indexed4=%u us; palette exchange x%u=%u us; sink=%u\n",
                  static_cast<unsigned>(clearIterations),
                  static_cast<unsigned>(canvasClearUs),
                  static_cast<unsigned>(directClearUs),
                  static_cast<unsigned>(wordClearUs),
                  static_cast<unsigned>(packedClearUs),
                  static_cast<unsigned>(fillIterations),
                  static_cast<unsigned>(canvasFillUs),
                  static_cast<unsigned>(directFillUs),
                  static_cast<unsigned>(packedFillUs),
                  static_cast<unsigned>(packedAlignedFillUs),
                  static_cast<unsigned>(pixelIterations),
                  static_cast<unsigned>(canvasPixelUs),
                  static_cast<unsigned>(directPixelUs),
                  static_cast<unsigned>(lineIterations),
                  static_cast<unsigned>(canvasLineUs),
                  static_cast<unsigned>(directLineUs),
                  static_cast<unsigned>(circleIterations),
                  static_cast<unsigned>(canvasCircleUs),
                  static_cast<unsigned>(directCircleUs),
                  static_cast<unsigned>(packedCircleUs),
                  static_cast<unsigned>(scrollIterations),
                  static_cast<unsigned>(scrollUs),
                  static_cast<unsigned>(packedScrollUs),
                  static_cast<unsigned>(translationIterations),
                  static_cast<unsigned>(translationUs),
                  static_cast<unsigned>(blitIterations),
                  static_cast<unsigned>(canvasOpaqueBlitUs),
                  static_cast<unsigned>(rgbOpaqueBlitUs),
                  static_cast<unsigned>(indexedOpaqueBlitUs),
                  static_cast<unsigned>(blitIterations),
                  static_cast<unsigned>(canvasTransparentBlitUs),
                  static_cast<unsigned>(rgbTransparentBlitUs),
                  static_cast<unsigned>(indexedTransparentBlitUs),
                  static_cast<unsigned>(paletteIterations),
                  static_cast<unsigned>(paletteUpdateUs),
                  static_cast<unsigned>(sink));
    heap_caps_free(memory);
    heap_caps_free(packedMemory);
    heap_caps_free(bitmapRgbMemory);
    heap_caps_free(bitmapIndexedMemory);
    heap_caps_free(bitmapFabglOpaqueMemory);
    heap_caps_free(bitmapFabglTransparentMemory);
}

#if defined(QL32_OLIMEX_DMA_FRAMEBUFFER_BENCHMARK)
void runDmaFramebufferBenchmark() {
    using namespace ql32::platform::olimex_vga;
    constexpr std::uint32_t width = 320;
    constexpr std::uint32_t height = 200;
    constexpr std::uint32_t bitmapSize = 64;
    constexpr std::uint32_t bitmapIterations = 100;
    constexpr std::uint32_t clearIterations = 10;
    constexpr std::uint32_t fillIterations = 200;
    constexpr std::uint32_t pixelIterations = 10000;
    constexpr std::uint32_t lineIterations = 1000;
    constexpr std::uint32_t circleIterations = 100;
    constexpr std::uint32_t scrollIterations = 20;

    auto* framebuffer = psramDmaScanout.beginFrameUpdate();
    if (!framebuffer || !framebuffer->valid()) {
        Serial.println("PSRAM active-scanout benchmark: no spare scanout frame");
        return;
    }
    const auto bitmapStride = framebuffer->format ==
            ql32::platform::olimex_vga::PixelFormat::Rgb222
        ? static_cast<std::size_t>(bitmapSize)
        : static_cast<std::size_t>(bitmapSize / 2);
    auto* bitmapPixels = static_cast<std::uint8_t*>(heap_caps_malloc(
        bitmapStride * bitmapSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!bitmapPixels) {
        Serial.println("PSRAM active-scanout benchmark: bitmap allocation failed");
        psramDmaScanout.cancelFrameUpdate(framebuffer);
        return;
    }
    Serial.printf("PSRAM_ACTIVE_SCANOUT_BENCH_BEGIN format=%s\n",
        framebuffer->format == ql32::platform::olimex_vga::PixelFormat::Rgb222
            ? "RGB222" : "indexed4");
    FramebufferView bitmapStorage{bitmapPixels, bitmapSize, bitmapSize,
                                  bitmapStride, framebuffer->format};
    for (std::uint32_t y = 0; y < bitmapSize; ++y) {
        for (std::uint32_t x = 0; x < bitmapSize; ++x) {
            const bool transparent = ((x + y) & 3u) == 0;
            (void)writePixel(bitmapStorage, x, y,
                transparent ? 0 : static_cast<std::uint8_t>((x + y) &
                    (framebuffer->format ==
                        ql32::platform::olimex_vga::PixelFormat::Rgb222
                            ? 0x3Fu : 0x0Fu)));
        }
    }
    const BitmapView bitmap{bitmapPixels, bitmapSize, bitmapSize, bitmapStride,
                            framebuffer->format};

    std::uint32_t start = micros();
    for (std::uint32_t i = 0; i < clearIterations; ++i)
        clear(*framebuffer, static_cast<std::uint8_t>(i & 0x3Fu));
    const auto clearUs = static_cast<std::uint32_t>(micros() - start);
    vTaskDelay(1);

    start = micros();
    for (std::uint32_t i = 0; i < fillIterations; ++i)
        fillRect(*framebuffer, 17, 23, 80, 40, static_cast<std::uint8_t>(i & 0x3Fu));
    const auto fillUs = static_cast<std::uint32_t>(micros() - start);
    vTaskDelay(1);

    start = micros();
    for (std::uint32_t i = 0; i < pixelIterations; ++i)
        (void)writePixel(*framebuffer, (i * 73u) % width, (i * 29u) % height,
                         static_cast<std::uint8_t>(i & 0x3Fu));
    const auto pixelUs = static_cast<std::uint32_t>(micros() - start);
    vTaskDelay(1);

    start = micros();
    for (std::uint32_t i = 0; i < lineIterations; ++i) {
        const auto x = (i * 37u) % width;
        const auto y = (i * 19u) % height;
        drawLine(*framebuffer, x, y, (x + 53u) % width,
                 (y + 31u) % height, static_cast<std::uint8_t>(i & 0x3Fu));
    }
    const auto lineUs = static_cast<std::uint32_t>(micros() - start);
    vTaskDelay(1);

    start = micros();
    for (std::uint32_t i = 0; i < circleIterations; ++i) {
        const auto x = static_cast<std::int32_t>((i * 41u) % width + 12u);
        const auto y = static_cast<std::int32_t>((i * 23u) % height + 12u);
        drawCircle(*framebuffer, x, y, 12,
                   static_cast<std::uint8_t>(i & 0x3Fu), true);
    }
    const auto circleUs = static_cast<std::uint32_t>(micros() - start);
    vTaskDelay(1);

    start = micros();
    for (std::uint32_t i = 0; i < scrollIterations; ++i) {
        scrollVertical(*framebuffer, 1, static_cast<std::uint8_t>(i & 0x3Fu));
        scrollVertical(*framebuffer, -1, static_cast<std::uint8_t>((i + 1) & 0x3Fu));
    }
    const auto scrollUs = static_cast<std::uint32_t>(micros() - start);
    vTaskDelay(1);

    start = micros();
    for (std::uint32_t i = 0; i < bitmapIterations; ++i) {
        const auto x = static_cast<std::int32_t>(
            ((i * 17u) % ((width - bitmapSize) / 2 + 1)) * 2);
        const auto y = static_cast<std::int32_t>(
            (i * 31u) % (height - bitmapSize + 1));
        copyBitmapOpaque(*framebuffer, x, y, bitmap, 0, 0,
                         bitmapSize, bitmapSize);
    }
    const auto opaqueBlitUs = static_cast<std::uint32_t>(micros() - start);
    vTaskDelay(1);

    start = micros();
    for (std::uint32_t i = 0; i < bitmapIterations; ++i) {
        const auto x = static_cast<std::int32_t>(
            ((i * 17u) % ((width - bitmapSize) / 2 + 1)) * 2);
        const auto y = static_cast<std::int32_t>(
            (i * 31u) % (height - bitmapSize + 1));
        copyBitmapTransparent(*framebuffer, x, y, bitmap, 0, 0,
                              bitmapSize, bitmapSize, 0);
    }
    const auto transparentBlitUs = static_cast<std::uint32_t>(micros() - start);
    vTaskDelay(1);

    std::uint32_t translateUs = 0;
    if (framebuffer->format ==
        ql32::platform::olimex_vga::PixelFormat::Indexed4) {
        std::array<std::uint8_t, 16> translation{};
        for (std::uint8_t i = 0; i < translation.size(); ++i)
            translation[i] = static_cast<std::uint8_t>(15 - i);
        start = micros();
        for (std::uint32_t i = 0; i < 100; ++i)
            translateIndexed4(*framebuffer, translation);
        translateUs = static_cast<std::uint32_t>(micros() - start);
        vTaskDelay(1);
    }

    const auto format = framebuffer->format ==
            ql32::platform::olimex_vga::PixelFormat::Rgb222
        ? "RGB222" : "indexed4";
    Serial.printf("PSRAM_ACTIVE_SCANOUT_BENCH format=%s clear10_us=%u fill200_us=%u pixel10000_us=%u line1000_us=%u filled_circle100_us=%u scroll20_pairs_us=%u opaque_blit100_us=%u transparent_blit100_us=%u translate100_us=%u\n",
        format, static_cast<unsigned>(clearUs), static_cast<unsigned>(fillUs),
        static_cast<unsigned>(pixelUs), static_cast<unsigned>(lineUs),
        static_cast<unsigned>(circleUs), static_cast<unsigned>(scrollUs),
        static_cast<unsigned>(opaqueBlitUs),
        static_cast<unsigned>(transparentBlitUs),
        static_cast<unsigned>(translateUs));
    heap_caps_free(bitmapPixels);

    // Exercise the full publish path after concurrent CPU writes to the spare
    // PSRAM frame. Each pattern is prepared off-screen and becomes active only
    // when the refill worker reaches the next frame boundary.
    for (std::uint32_t phase = 0; phase < 3; ++phase) {
        auto* published = phase == 0
            ? framebuffer : psramDmaScanout.beginFrameUpdate();
        if (!published || !published->valid()) {
            Serial.printf("PSRAM_ACTIVE_SCANOUT_PUBLISH_ERROR phase=%u acquire\n",
                          static_cast<unsigned>(phase));
            if (published) psramDmaScanout.cancelFrameUpdate(published);
            break;
        }
        clear(*published, 0);
        for (std::uint32_t band = 0; band < 8; ++band) {
            const auto x = (band * 40u + phase * 13u) % width;
            fillRect(*published, static_cast<std::int32_t>(x), 0, 24, height,
                     static_cast<std::uint8_t>((band + phase) & 0x0Fu));
        }
        drawLine(*published, 0, phase * 31u, width - 1,
                 (phase * 31u + 73u) % height,
                 static_cast<std::uint8_t>(15u - phase));
        if (!psramDmaScanout.publishFrameUpdate(published)) {
            Serial.printf("PSRAM_ACTIVE_SCANOUT_PUBLISH_ERROR phase=%u publish\n",
                          static_cast<unsigned>(phase));
            psramDmaScanout.cancelFrameUpdate(published);
            break;
        }
        Serial.printf("PSRAM_ACTIVE_SCANOUT_PUBLISHED phase=%u\n",
                      static_cast<unsigned>(phase));
        delay(500);
    }
    psramDmaScanout.reportStatus();
}
#endif
#endif

} // namespace

void setup() {
    Serial.begin(115200);
#if defined(QL32_OLIMEX_DMA_TEST_SCANOUT)
    if (!psramFound()) {
        Serial.println("Fatal: PSRAM VGA scanout requires initialized PSRAM");
        return;
    }
    if (!psramDmaScanout.begin(VGA_320x200_70Hz,
            ql32_board::vgaRed, ql32_board::vgaGreen, ql32_board::vgaBlue,
            ql32_board::vgaHSync, ql32_board::vgaVSync)) {
        Serial.println("Fatal: PSRAM VGA scanout initialization failed");
        return;
    }
#if defined(QL32_OLIMEX_DMA_FRAMEBUFFER_BENCHMARK)
    runDmaFramebufferBenchmark();
#endif
    return;
#endif
#if defined(QL32_OLIMEX_DMA_DISPLAY)
#if defined(QL32_OLIMEX_DMA_DISPLAY_DIAGNOSTIC)
    constexpr bool dmaDisplayDiagnostic = true;
#else
    constexpr bool dmaDisplayDiagnostic = false;
#endif
    if (!psramFound()) {
        Serial.println("Fatal: Olimex DMA display requires initialized PSRAM");
        return;
    }
#endif
#if defined(QL32_OLIMEX_PSRAM)
    if (!psramFound()) {
        Serial.println("Fatal: Olimex WROVER PSRAM was not initialized");
        return;
    }
    Serial.printf("PSRAM total: %u\n", static_cast<unsigned>(ESP.getPsramSize()));
    Serial.printf("PSRAM free: %u\n", static_cast<unsigned>(ESP.getFreePsram()));
    Serial.printf("Internal free: %u\n", static_cast<unsigned>(
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
#endif
    if (!boardMemory.initializeEmergencyReserve(
            ql32::platform::esp32::defaultEmergencyReserveBytes)) {
        Serial.println("Fatal: unable to reserve memory for recovery operations");
        return;
    }
    userSpiBus.begin();

    if (!ql32_board::capabilities().valid()) {
        Serial.printf("QL/32 %s capability profile invalid\n", kBoardName);
        return;
    }

#if !defined(QL32_OLIMEX_DMA_DISPLAY)
    display.begin(
        static_cast<gpio_num_t>(ql32_board::vgaRed),
        static_cast<gpio_num_t>(ql32_board::vgaGreen),
        static_cast<gpio_num_t>(ql32_board::vgaBlue),
        static_cast<gpio_num_t>(ql32_board::vgaHSync),
        static_cast<gpio_num_t>(ql32_board::vgaVSync));
#if defined(QL32_OLIMEX_FABGL_DOUBLE_BUFFER_TEST)
    // Experimental fallback: keep FabGL's validated scanout and render each
    // complete 320x200 frame into its hidden DMA-capable viewport before swap.
    display.setResolution(VGA_320x200_70Hz, -1, -1, true);
    constexpr auto internalCaps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    Serial.printf("FabGL double-buffer internal free: %u, largest block: %u\n",
        static_cast<unsigned>(heap_caps_get_free_size(internalCaps)),
        static_cast<unsigned>(heap_caps_get_largest_free_block(internalCaps)));
#else
    display.setResolution(VGA_320x200_70Hz);
#endif
#endif
#if defined(QL32_OLIMEX_RENDER_BENCHMARK)
    runOlimexRenderBenchmark();
#endif

#if defined(QL32_OLIMEX_DMA_DISPLAY)
    constexpr std::uint32_t screenWidth = 320;
    constexpr std::uint32_t screenHeight = 200;
    screenConsole.begin(screenWidth, screenHeight);
    auto framePresenter = std::make_shared<ql32::platform::olimex_vga::PsramFramePresenter>(
        psramDmaScanout);
#else
    screenConsole.begin(static_cast<std::uint32_t>(canvas.getWidth()),
                        static_cast<std::uint32_t>(canvas.getHeight()));
    auto framePresenter = std::make_shared<ql32::platform::fabgl_vga::FabglFramePresenter>(
        display, canvas);
#if defined(QL32_GRAPHICS_PROFILE)
    profilePresenter = framePresenter;
#endif
    const auto screenWidth = static_cast<std::uint32_t>(canvas.getWidth());
    const auto screenHeight = static_cast<std::uint32_t>(canvas.getHeight());
#endif
#if defined(QL32_BOARD_OLIMEX_ESP32_SBC_FABGL)
    auto canvasRasterOps = std::make_shared<
        ql32::platform::olimex_vga::CanvasRasterOps>();
    graphics = std::make_shared<ql32::graphics::WindowedService>(
        screenWidth, screenHeight, framePresenter, std::move(canvasRasterOps));
#else
    graphics = std::make_shared<ql32::graphics::WindowedService>(
        screenWidth, screenHeight, framePresenter);
#endif
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
    graphics->setConsoleTextPositionCallback([](std::uint32_t row,
                                                 std::uint32_t column) {
        screenConsole.setTextPosition(row, column);
    });
    graphics->setConsoleOutputBatchCallbacks(
        [] { screenConsole.beginOutputBatch(); },
        [] { screenConsole.endOutputBatch(); });

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
    services.displayModes = std::shared_ptr<ql32::runtime::DisplayModeService>(
        &displayModes, [](ql32::runtime::DisplayModeService*) {});
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
#if defined(QL32_OLIMEX_DMA_DISPLAY)
    // PS/2 and storage setup use platform peripheral initialization paths that
    // may briefly occupy the CPU or reconfigure shared ESP32 resources. Start
    // continuous VGA scanout after those services and immediately before the
    // first console invalidation queues a frame.
    if (!psramDmaScanout.begin(VGA_320x200_70Hz,
            ql32_board::vgaRed, ql32_board::vgaGreen, ql32_board::vgaBlue,
            ql32_board::vgaHSync, ql32_board::vgaVSync,
            dmaDisplayDiagnostic)) {
        Serial.println("Fatal: Olimex DMA display initialization failed");
        return;
    }
#endif
    graphics->setConsoleLayer([](std::uint32_t y, std::uint32_t x,
                                 std::vector<ql32::graphics::ColorIndex>& row) {
        screenConsole.composePaletteRegionRow(y, x, row);
    });
    screenConsole.setInvalidateCallback([](ql32::graphics::Rect region) {
        graphics->invalidateSystemRegion(region);
    });
    replTask = scheduler->launchInteractive(serialOutput, std::move(startupProgram));
}

void loop() {
#if defined(QL32_OLIMEX_DMA_TEST_SCANOUT)
    static std::uint32_t lastStatus = millis();
#if defined(QL32_OLIMEX_DMA_TEST_PACKED4) && \
    !defined(QL32_OLIMEX_DMA_TEST_PIXEL_EDGES) && \
    !defined(QL32_OLIMEX_DMA_TEST_TRIANGLE)
    static std::uint32_t lastPaletteRotation = millis();
    if (millis() - lastPaletteRotation >= 500) {
        lastPaletteRotation = millis();
        (void)psramDmaScanout.stagePaletteRotation(1);
    }
#endif
    if (millis() - lastStatus >= 1000) {
        lastStatus = millis();
        psramDmaScanout.reportStatus();
    }
    delay(1);
#else
#if defined(QL32_OLIMEX_DMA_DISPLAY)
    static std::uint32_t lastDmaStatus = millis();
    if (millis() - lastDmaStatus >= 1000) {
        lastDmaStatus = millis();
        psramDmaScanout.reportStatus();
    }
#endif
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
            handleEditorKey(*event);
        }
    }
    advanceScheduler();
    refreshInputCaretBlink();
    delay(1);
#endif
}
