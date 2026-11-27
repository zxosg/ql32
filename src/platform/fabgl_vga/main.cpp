#include "ql32/version.hpp"
#include "ql32/platform/esp32/application_input.hpp"
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
#include <unordered_map>
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
#include "ql32/ui/line_damage.hpp"
#include "ql32/graphics/windowed_service.hpp"
#include "ql32/platform/fabgl_vga/graphics.hpp"
#include "ql32/platform/fabgl_vga/frame_presenter.hpp"
#include "ql32/platform/fabgl_vga/display_modes.hpp"
#include "ql32/platform/fabgl_vga/keyboard.hpp"
#if defined(QL32_BOARD_OLIMEX_ESP32_SBC_FABGL)
#include "ql32/platform/olimex_vga/canvas_raster_ops.hpp"
#include "ql32/platform/olimex_vga/music.hpp"
#endif
#if defined(QL32_OLIMEX_DMA_DISPLAY)
#include "ql32/platform/olimex_vga/fixed_display_modes.hpp"
#include "ql32/platform/olimex_vga/frame_presenter.hpp"
#if defined(QL32_OLIMEX_DMA_DYNAMIC_MODE_TEST)
#include "ql32/platform/olimex_vga/switchable_display_modes.hpp"
#endif
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

    void setScreenSize(std::uint32_t width, std::uint32_t height) {
        if (width == 0 || height == 0) return;
        screenWidth_ = width;
        screenHeight_ = height;
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
        std::string nextLine = line.substr(0, std::min(inputColumn_, line.size()));

        const auto available = columns_ - std::min(inputColumn_, columns_ - 1);
        const auto maxCursorOffset = available - 1;
        cursor = std::min(cursor, text.size());
        const auto viewOffset = cursor > maxCursorOffset ? cursor - maxCursorOffset : 0;
        const auto visibleCount = std::min(available, text.size() - viewOffset);
        if (visibleCount != 0)
            nextLine.append(text.data() + viewOffset, visibleCount);

        const auto nextCaret = inputColumn_ + std::min(cursor - viewOffset, maxCursorOffset);
        const auto damage = ql32::ui::lineDamage(line, nextLine, columns_,
            caretVisible_ && caretRow_ == inputRow_, caretColumn_,
            caretVisible, nextCaret);
        line.swap(nextLine);
        caretRow_ = inputRow_;
        caretColumn_ = nextCaret;
        caretVisible_ = caretVisible;
        if (!damage.empty()) invalidateInputSpan(damage.first, damage.last);
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
        const auto left = static_cast<std::uint32_t>(bounds_.x);
        const auto right = left + bounds_.width;
        const std::uint32_t first = std::max(xOrigin, left);
        const std::uint32_t last = std::min<std::uint32_t>(xOrigin + pixels.size(), right);
        if (first >= last) return;
        // Walk cell by cell: one glyph lookup per character column instead of
        // per pixel, and no per-pixel division.
        auto localX = first - left;
        auto column = localX / consoleCharacterWidth;
        auto glyphColumn = localX % consoleCharacterWidth;
        auto i = first - xOrigin;
        const auto end = last - xOrigin;
        while (i < end) {
            std::uint8_t columns[consoleCharacterWidth] = {};
            if (line && glyphRow < 7 && column < line->size()) {
                const auto glyph = ql32::graphics::classicGlyphColumns((*line)[column]);
                for (std::uint32_t g = 0; g < 5 && g < consoleCharacterWidth; ++g)
                    columns[g] = (glyph[g] & (1u << glyphRow)) ? 1 : 0;
            }
            const bool caretCell = inputLineActive_ && caretVisible_ &&
                textRow == caretRow_ && column == caretColumn_;
            for (; glyphColumn < consoleCharacterWidth && i < end; ++glyphColumn, ++i) {
                const bool foreground = columns[glyphColumn] != 0;
                pixels[i] = caretCell ? (foreground ? paper_ : ink_)
                                      : (foreground ? ink_ : paper_);
            }
            glyphColumn = 0;
            ++column;
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

    void invalidateInputSpan(std::size_t first, std::size_t last) {
        if (inputRow_ >= rows_ || first >= last || first >= columns_) return;
        last = std::min(last, columns_);
        const ql32::graphics::Rect region{
            bounds_.x + static_cast<std::int32_t>(first * consoleCharacterWidth),
            bounds_.y + static_cast<std::int32_t>(inputRow_ * consoleCharacterHeight),
            static_cast<std::uint32_t>((last - first) * consoleCharacterWidth),
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
#if defined(QL32_OLIMEX_PRESENT_PROFILE)
ql32::platform::olimex_vga::PsramFramePresenter* psramPresenterDiag = nullptr;
#endif
#endif
fabgl::Canvas canvas(&display);
#if defined(QL32_OLIMEX_DMA_DISPLAY)
#if defined(QL32_OLIMEX_DMA_DYNAMIC_MODE_TEST)
void applyDmaDisplayMode(std::uint32_t requested, std::uint32_t previous) {
    struct Preset { std::uint32_t width, height; const char* modeline; };
    static const std::array<Preset, 5> presets{{
        {320, 200, VGA_320x200_70Hz}, {320, 240, QVGA_320x240_60Hz},
        {400, 300, VGA_400x300_60Hz}, {640, 400, VGA_640x400_70Hz},
        {640, 480, VGA_640x480_60Hz},
    }};
    // Ids outside the build's mode list are never requested by the service.
    const auto start = [&](std::uint32_t id) {
        const auto& preset = presets.at(id);
        return psramDmaScanout.configureGeometry(preset.width, preset.height) &&
            psramDmaScanout.begin(preset.modeline,
                ql32_board::vgaRed, ql32_board::vgaGreen, ql32_board::vgaBlue,
                ql32_board::vgaHSync, ql32_board::vgaVSync, false);
    };
    if (!psramDmaScanout.end())
        throw std::runtime_error("unable to stop Olimex DMA scanout");
    if (!start(requested)) {
        if (!start(previous))
            throw std::runtime_error("DMA mode change failed and previous mode could not be restored");
        throw std::runtime_error("DMA mode change failed; previous mode restored");
    }
}
ql32::platform::olimex_vga::SwitchableDisplayModes displayModes(
    ql32::platform::olimex_vga::FixedDisplayModes::kModeId, applyDmaDisplayMode);
#else
ql32::platform::olimex_vga::FixedDisplayModes displayModes;
#endif
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
bool sdCardSpace(std::uint64_t& total, std::uint64_t& used) {
    total = SD.totalBytes();
    used = SD.usedBytes();
    return total != 0;
}
ql32::platform::esp32::SdStorage sdStorage(SD, "SDC", sdCardSpace, "/sd");
ql32::platform::esp32::SdChannels sdChannels(SD);
#if defined(QL32_OLIMEX_USE_SPIFFS)
bool spiffsSpace(std::uint64_t& total, std::uint64_t& used) {
    total = SPIFFS.totalBytes();
    used = SPIFFS.usedBytes();
    return total != 0;
}
ql32::platform::esp32::SdStorage flashStorage(SPIFFS, "FFS", spiffsSpace, "/spiffs");
ql32::platform::esp32::SdChannels flashChannels(SPIFFS, "FFS");
#if defined(QL32_OLIMEX_DMA_DISPLAY)
// Internal flash writes stall both cores' cache, so FFS writes hold the last
// good picture on screen. The notice is the regular UI info toast (the IPRINT
// machinery), rendered into the framebuffer before the snapshot is taken.
// SDC never holds.
void flashHoldBegin(void*);
void flashHoldEnd(void*);
ql32::runtime::CountedBulkWriteGate flashWriteGate(flashHoldBegin, flashHoldEnd, nullptr);
const bool flashWriteGateAttached = [] {
    flashStorage.setBulkWriteGate(&flashWriteGate);
    flashChannels.setBulkWriteGate(&flashWriteGate);
    return true;
}();
#endif
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
std::shared_ptr<ql32::input::KeyRouter> applicationKeys = std::make_shared<ql32::input::KeyRouter>();
ql32::input::SerialDecoder applicationSerialDecoder;
ql32::platform::esp32::ApplicationProtocolGate applicationProtocol;
std::uint64_t applicationInputOwner = 0;

#if defined(QL32_BOARD_OLIMEX_ESP32_SBC_FABGL)
std::shared_ptr<ql32::runtime::MusicService> boardMusicService;
#endif
std::unique_ptr<ql32::ui::GraphicPresenter> uiPresenter;
#if defined(QL32_OLIMEX_USE_SPIFFS) && defined(QL32_OLIMEX_DMA_DISPLAY)
// The info toast is composed by the UI/graphics objects owned by the main
// loop task; other tasks use the scanout's built-in notice instead.
TaskHandle_t mainLoopTask = nullptr;
bool flashHoldToastShown = false;
constexpr ql32::ui::SessionId kFlashHoldToastSession = ~ql32::ui::SessionId{0};
constexpr int kDlgPosRight = 1;
constexpr int kDlgPosTop = -1;
constexpr std::uint8_t kHoldToastInk = 6;    // yellow text
constexpr std::uint8_t kHoldToastPaper = 0;  // black background
constexpr std::int16_t kHoldToastFrame = 6;  // yellow border

void flashHoldBegin(void*) {
    bool toast = false;
    if (uiPresenter && graphics && mainLoopTask &&
        xTaskGetCurrentTaskHandle() == mainLoopTask && !psramDmaScanout.holdPending()) {
        uiService->showInfo(kFlashHoldToastSession, "Busy, accessing disk", 600000,
                            kDlgPosRight, kDlgPosTop, kHoldToastInk, kHoldToastPaper, 0,
                            kHoldToastFrame);
        uiPresenter->refresh();
        graphics->flushPendingPresentation();
        flashHoldToastShown = toast = true;
    }
    // The interrupt blinks the yellow text (yellow -> black) over the toast region.
    ql32::platform::olimex_vga::PsramDmaScanout::HoldBlink blink;
    const auto info = toast ? uiPresenter->infoBounds() : ql32::graphics::Rect{};
    // Inset past the 1 px frame and 8 px side padding so only the text blinks.
    blink.x = static_cast<std::uint32_t>(info.x) + 8;
    blink.y = static_cast<std::uint32_t>(info.y) + 1;
    blink.width = info.width > 16 ? info.width - 16 : 0;
    blink.height = info.height > 2 ? info.height - 2 : 0;
    blink.from = kHoldToastInk;
    blink.to = kHoldToastPaper;
    psramDmaScanout.holdBegin(!toast, toast ? &blink : nullptr);
}

void flashHoldEnd(void*) { psramDmaScanout.holdEnd(); }

// Called from the main loop: remove the toast once the hold has been released.
void flashHoldPoll() {
    if (!flashHoldToastShown || psramDmaScanout.holdPending() || !uiPresenter) return;
    flashHoldToastShown = false;
    uiService->cancelSession(kFlashHoldToastSession);
    uiPresenter->refresh();
}
#endif
std::unique_ptr<ql32::platform::esp32::RemoteDebugProtocol> remoteDebugger;
ql32::ui::LineEditor lineEditor;
std::string inputPrompt;
struct SavedTaskInput {
    std::string text;
    int editingLineNumber = 0;
};
std::unordered_map<ql32::vm::TaskId, SavedTaskInput> savedTaskInputs;
ql32::vm::TaskId inputEditorOwner = 0;
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
    if (editingLineNumber > 0 &&
        (event.key == ql32::input::Key::Up || event.key == ql32::input::Key::Down)) {
        if (scheduler) {
            const int direction = event.key == ql32::input::Key::Up ? -1 : 1;
            if (const auto edit = scheduler->navigateProgramEdit(
                    scheduler->focusedTask(), editingLineNumber, direction)) {
                editingLineNumber = edit->lineNumber;
                lineEditor.setText(edit->text);
                redrawInputLine();
                return;
            }
        }
        return;
    }
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
    case ql32::platform::esp32::SerialKey::Down: event.key = ql32::input::Key::Down; break;
    case ql32::platform::esp32::SerialKey::Left: event.key = ql32::input::Key::Left; break;
    case ql32::platform::esp32::SerialKey::Right: event.key = ql32::input::Key::Right; break;
    case ql32::platform::esp32::SerialKey::Home: event.key = ql32::input::Key::Home; break;
    case ql32::platform::esp32::SerialKey::End: event.key = ql32::input::Key::End; break;
    }
    event.control = serialKey.control;
    return event;
}

void submitInputLine() {
    const auto submittedTask = scheduler ? scheduler->focusedTask() : 0;
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
#if defined(QL32_GRAPHICS_PROFILE)
        // Exclude REPL typing and echo from the next command's presentation
        // measurements. The profile then covers the submitted workload only.
        if (profilePresenter && submittedTask == replTask)
            profilePresenter->resetPresentationProfile();
#endif
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
#if defined(QL32_OLIMEX_DMA_DISPLAY) && \
    defined(QL32_OLIMEX_DMA_STATUS_TEST)
    } else if (submittedLine == "DMADIAG") {
        // Diagnostic profiles retain counters in RAM while workloads run.
        // The user requests one snapshot after the test to avoid adding UART
        // output to the measured scanout and REPL workload.
        psramDmaScanout.reportStatus();
#endif
#if defined(QL32_GRAPHICS_PROFILE)
    } else if (submittedLine == "COALDIAG") {
        if (graphics) {
            const auto& c = graphics->coalesceStats();
            Serial.printf("QL32 coalesce: requests=%llu, deferred=%llu, presented=%llu, present-us=%llu\n",
                          static_cast<unsigned long long>(c.requests),
                          static_cast<unsigned long long>(c.deferred),
                          static_cast<unsigned long long>(c.presented),
                          static_cast<unsigned long long>(c.presentUs));
            graphics->resetCoalesceStats();
        }
    } else if (submittedLine.rfind("COALRATE", 0) == 0) {
        unsigned minUs = 0, ratio = 0;
        std::sscanf(submittedLine.c_str() + 8, "%u %u", &minUs, &ratio);
        if (graphics) graphics->setPresentCoalescing(minUs, ratio,
            [] { return static_cast<std::uint64_t>(micros()); });
#endif
#if defined(QL32_OLIMEX_DMA_DISPLAY) && defined(QL32_OLIMEX_DMA_REFILL_PROFILE)
    } else if (submittedLine == "ENCBENCH") {
        psramDmaScanout.runEncoderBenchmark();
#endif
#if defined(QL32_OLIMEX_DMA_DISPLAY) && defined(QL32_OLIMEX_PRESENT_PROFILE)
    } else if (submittedLine == "PRESDIAG") {
        // Snapshot of presenter counters; PRESRESET zeroes them between runs.
        if (psramPresenterDiag) psramPresenterDiag->reportStats();
#if defined(QL32_OLIMEX_PRESENT_COALESCE)
        if (graphics) {
            const auto& c = graphics->coalesceStats();
            Serial.printf("QL32 present coalesce: requests=%llu, deferred=%llu, presented=%llu, present-us=%llu, pending=%d\n",
                          static_cast<unsigned long long>(c.requests),
                          static_cast<unsigned long long>(c.deferred),
                          static_cast<unsigned long long>(c.presented),
                          static_cast<unsigned long long>(c.presentUs),
                          graphics->hasPendingPresentation() ? 1 : 0);
        }
#endif
    } else if (submittedLine.rfind("PRESBUDGET", 0) == 0) {
        // PRESBUDGET <bytes per video frame>; 0 disables the throttle.
        if (psramPresenterDiag) {
            const auto value = std::atol(submittedLine.c_str() + 10);
            psramPresenterDiag->setFrameByteBudget(
                value > 0 ? static_cast<std::uint32_t>(value) : 0u);
            Serial.printf("QL32 present budget=%u bytes/frame\n",
                          static_cast<unsigned>(psramPresenterDiag->frameByteBudget()));
        }
#if defined(QL32_OLIMEX_PRESENT_COALESCE)
    } else if (submittedLine.rfind("PRESRATE", 0) == 0) {
        // PRESRATE <min interval us> <cost ratio>; "PRESRATE 0 0" disables.
        unsigned minUs = 0, ratio = 0;
        std::sscanf(submittedLine.c_str() + 8, "%u %u", &minUs, &ratio);
        if (graphics) {
            graphics->setPresentCoalescing(minUs, ratio,
                [] { return static_cast<std::uint64_t>(micros()); });
            Serial.printf("QL32 present rate min-us=%u cost-ratio=%u\n", minUs, ratio);
        }
#endif
    } else if (submittedLine.rfind("PRESWRITE", 0) == 0) {
        // PRESWRITE 0|1: per-byte (0) or chunked bulk (1) presenter row writes.
        if (psramPresenterDiag) {
            psramPresenterDiag->setChunkedWrite(std::atoi(submittedLine.c_str() + 9) != 0);
            Serial.printf("QL32 present chunked-write=%d\n",
                          psramPresenterDiag->chunkedWrite() ? 1 : 0);
        }
    } else if (submittedLine == "PRESRESET") {
        if (psramPresenterDiag) psramPresenterDiag->resetStats();
#if defined(QL32_OLIMEX_PRESENT_COALESCE)
        if (graphics) graphics->resetCoalesceStats();
#endif
        Serial.println("QL32 present stats reset");
#endif
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
    savedTaskInputs.erase(submittedTask);
    inputEditorOwner = 0;
    inputPrompt.clear();
    serialKeyDecoder.reset();
    editingLineNumber = 0;
    protocolInputLine = false;
    replPromptShown = false;
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
    // Debugger frames use a raw ASCII line beginning with '@'. Recognize that
    // prefix before either the graphical terminal adapter or the keyboard
    // escape decoder can echo or transform it into REPL keystrokes.
    if (!protocolInputLine && character == '@' && lineEditor.text().empty()) {
        protocolInputLine = true;
        lineEditor.insertRawCharacter(character);
        return;
    }
    if (character == '\n' && serialSawCarriageReturn) {
        serialSawCarriageReturn = false;
        return;
    }
    if (applicationInputOwner && !protocolInputLine) {
        serialSawCarriageReturn = false;
        applicationProtocol.feed(static_cast<unsigned char>(character), millis(), applicationSerialDecoder,
            *applicationKeys, [](std::string_view prefix) {
                protocolInputLine = true;
                for (char byte : prefix) lineEditor.insertRawCharacter(byte, true);
            });
        return;
    }
    if (uiPresenter && uiPresenter->graphicActive() && !protocolInputLine) {
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
    char line[64]{};
    std::snprintf(line, sizeof(line), "FREE:%lluK MAX:%lluK BLK:%llu TASK:%u/%u %s",
        static_cast<unsigned long long>(freeBytes / 1024),
        static_cast<unsigned long long>(heap ? heap->largestFreeBlockBytes / 1024 : 0),
        static_cast<unsigned long long>(heap ? heap->allocatedBlockCount : 0),
        static_cast<unsigned>(scheduler ? scheduler->focusedTaskNumber() : 0),
        static_cast<unsigned>(activeTasks), clockText);
    graphics->setDebugOverlayText(line);
}

void advanceScheduler() {
#if defined(QL32_OLIMEX_USE_SPIFFS) && defined(QL32_OLIMEX_DMA_DISPLAY)
    flashHoldPoll();
#endif
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

    refreshApplicationInput();

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
            if (inputEditorOwner != focused) {
                lineEditor.clear();
                editingLineNumber = 0;
                const auto saved = savedTaskInputs.find(focused);
                if (saved != savedTaskInputs.end()) {
                    lineEditor.setText(saved->second.text);
                    editingLineNumber = saved->second.editingLineNumber;
                    savedTaskInputs.erase(saved);
                }
                inputEditorOwner = focused;
            }
#if defined(QL32_GRAPHICS_PROFILE)
            if (focused == replTask && profilePresenter) {
                if (!profilePromptPrimed) {
                    profilePromptPrimed = true;
                } else {
                    profilePresenter->finalizePresentationProfile();
                    const auto& stats = profilePresenter->presentationProfile();
                    if (stats.regionRequests != 0) {
                        Serial.printf(
                            "GFXPROFILE requests=%llu full=%llu partial=%llu fallback=%llu rows=%llu composed_px=%llu written_px=%llu compose_us=%llu convert_us=%llu write_us=%llu update_us=%llu update_samples=%u update_p50_us=%u update_p95_us=%u update_p99_us=%u swap_requested=%llu swap_queued=%llu swap_executed=%llu swap_skipped=%llu swap_us=%llu swap_calls=%llu swap_avg_us=%llu swap_min_us=%llu swap_max_us=%llu swap_samples=%u swap_p50_us=%u swap_p95_us=%u swap_p99_us=%u\n",
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
                            static_cast<unsigned long long>(stats.updateMicros),
                            static_cast<unsigned>(stats.updateSampleCount),
                            static_cast<unsigned>(stats.updateP50Micros),
                            static_cast<unsigned>(stats.updateP95Micros),
                            static_cast<unsigned>(stats.updateP99Micros),
                            static_cast<unsigned long long>(stats.swapRequested),
                            static_cast<unsigned long long>(stats.swapQueued),
                            static_cast<unsigned long long>(stats.swapExecuted),
                            static_cast<unsigned long long>(stats.swapSkipped),
                            static_cast<unsigned long long>(stats.swapMicros),
                            static_cast<unsigned long long>(stats.swapCalls),
                            static_cast<unsigned long long>(stats.swapCalls
                                ? stats.swapMicros / stats.swapCalls : 0),
                            static_cast<unsigned long long>(stats.swapMinMicros),
                            static_cast<unsigned long long>(stats.swapMaxMicros),
                            static_cast<unsigned>(stats.swapSampleCount),
                            static_cast<unsigned>(stats.swapP50Micros),
                            static_cast<unsigned>(stats.swapP95Micros),
                            static_cast<unsigned>(stats.swapP99Micros));
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
    if (event.key == ql32::input::Key::Tab && event.alt) {
        const auto current = scheduler->focusedTask();
        const auto status = scheduler->taskStatus(current);
        if (!protocolInputLine && status &&
            *status == ql32::vm::TaskStatus::WaitingInput &&
            (!uiPresenter || !uiPresenter->active())) {
            savedTaskInputs[current] = {lineEditor.text(), editingLineNumber};
        }
        if (scheduler->focusNextTask()) {
            inputEditorOwner = 0;
            replPromptShown = false;
            lastDebugOverlayRefresh = millis() - 1000;
            refreshDebugOverlay();
        }
        return true;
    }
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
    uiService->setKeyInput(applicationKeys);
#if defined(QL32_OLIMEX_USE_SPIFFS) && defined(QL32_OLIMEX_DMA_DISPLAY)
    mainLoopTask = xTaskGetCurrentTaskHandle();
#endif
#if defined(QL32_OLIMEX_DMA_DISPLAY)
    // YMODEM sends 1 KiB packets. Allow a complete packet to queue while the
    // DMA refill worker preempts the serial consumer during high-resolution
    // scanout; the Arduino default receive buffer is smaller than one block.
    Serial.setRxBufferSize(2048);
#endif
    Serial.begin(115200);
#if defined(QL32_OLIMEX_DMA_TEST_SCANOUT)
#if defined(QL32_OLIMEX_DMA_TEST_MODE_400X300)
    constexpr auto dmaTestModeline = VGA_400x300_60Hz;
#elif defined(QL32_OLIMEX_DMA_TEST_MODE_320X240)
    constexpr auto dmaTestModeline = QVGA_320x240_60Hz;
#elif defined(QL32_OLIMEX_DMA_TEST_MODE_640X400)
    constexpr auto dmaTestModeline = VGA_640x400_70Hz;
#elif defined(QL32_OLIMEX_DMA_TEST_MODE_640X480)
    constexpr auto dmaTestModeline = VGA_640x480_60Hz;
#else
    constexpr auto dmaTestModeline = VGA_320x200_70Hz;
#endif
    if (!psramFound()) {
        Serial.println("Fatal: PSRAM VGA scanout requires initialized PSRAM");
        return;
    }
    if (!psramDmaScanout.begin(dmaTestModeline,
            ql32_board::vgaRed, ql32_board::vgaGreen, ql32_board::vgaBlue,
            ql32_board::vgaHSync, ql32_board::vgaVSync)) {
        Serial.println("Fatal: PSRAM VGA scanout initialization failed");
        return;
    }
#if defined(QL32_OLIMEX_DMA_RESTART_TEST)
    if (psramDmaScanout.configureGeometry(400, 300)) {
        Serial.println("PSRAM VGA restart: accepted geometry while running");
        return;
    }
    struct RestartMode {
        std::uint32_t width;
        std::uint32_t height;
        const char* modeline;
    };
    const RestartMode modes[] = {
        {400, 300, VGA_400x300_60Hz}, {640, 480, VGA_640x480_60Hz},
        {320, 240, QVGA_320x240_60Hz}, {320, 200, VGA_320x200_70Hz},
        {640, 400, VGA_640x400_70Hz}, {640, 480, VGA_640x480_60Hz},
    };
    for (unsigned cycle = 1; cycle <= sizeof(modes) / sizeof(modes[0]); ++cycle) {
        delay(4000);
        if (!psramDmaScanout.requestStop()) {
            Serial.println("PSRAM VGA restart: stop failed");
            return;
        }
        unsigned polls = 0;
        while (!psramDmaScanout.pollStop()) {
            ++polls;
            delay(1);
        }
        Serial.printf("PSRAM VGA restart: stopped cycle=%u polls=%u\n", cycle, polls);
        const auto& mode = modes[cycle - 1];
        if (psramDmaScanout.configureGeometry(800, 600) ||
            !psramDmaScanout.configureGeometry(mode.width, mode.height)) {
            Serial.println("PSRAM VGA restart: invalid geometry selection");
            return;
        }
        if (!psramDmaScanout.begin(mode.modeline,
                ql32_board::vgaRed, ql32_board::vgaGreen, ql32_board::vgaBlue,
                ql32_board::vgaHSync, ql32_board::vgaVSync)) {
            Serial.println("PSRAM VGA restart: start failed");
            return;
        }
        Serial.printf("PSRAM VGA restart: restarted cycle=%u\n", cycle);
    }
#endif
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
    constexpr std::uint32_t screenWidth =
        ql32::platform::olimex_vga::PsramDmaScanout::kOutputWidth;
    constexpr std::uint32_t screenHeight =
        ql32::platform::olimex_vga::PsramDmaScanout::kOutputHeight;
    screenConsole.begin(screenWidth, screenHeight);
    auto framePresenter = std::make_shared<ql32::platform::olimex_vga::PsramFramePresenter>(
        psramDmaScanout);
#if defined(QL32_OLIMEX_PRESENT_PROFILE)
    psramPresenterDiag = framePresenter.get();
#endif
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
#if defined(QL32_OLIMEX_PRESENT_COALESCE) || \
    (defined(QL32_BOARD_OLIMEX_ESP32_SBC_FABGL) && !defined(QL32_OLIMEX_DMA_DISPLAY))
    // FabGL modes: console output from one task must not starve the others
    // with synchronous presents. Default: at most ~25 presents/s and presenting never takes more than
    // half of the VM's time (cost ratio 1). Tunable at runtime with PRESRATE.
    graphics->setPresentCoalescing(40000, 1,
        [] { return static_cast<std::uint64_t>(micros()); });
#endif
    graphics->setDefaultWindowBounds(0,
        {0, static_cast<std::int32_t>(screenHeight - consoleHeight), screenWidth, consoleHeight});
    graphics->setDefaultWindowBounds(1,
        {0, 0, screenWidth, screenHeight - consoleHeight});
    graphics->setConsoleBoundsCallback([](ql32::graphics::Rect bounds) {
        const auto [width, height] = graphics->displaySize();
        screenConsole.setScreenSize(width, height);
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

    Serial.printf("QL/32 %s bring-up ready, version %s\n", kBoardName, QL32_VERSION_STRING QL32_VERSION_FEATURES);
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
    {
        ql32::runtime::SystemInfoProfile profile;
        profile.values = {"0.1.0", "unknown",
#if defined(QL32_BOARD_OLIMEX_ESP32_SBC_FABGL)
                          "olimex_esp32_sbc_fabgl_rev_b",
#else
                          "lilygo_vga32",
#endif
                          "fabgl-vga 320x200 indexed3", "ps2", "sd",
                          "vga,ps2,sd,network"};
#if defined(QL32_OLIMEX_USE_SPIFFS)
        profile.values[5] = "ffs,sd";
        profile.values[6] += ",ffs";
#endif
#if defined(QL32_OLIMEX_PSRAM)
        profile.values[6] += ",psram";
#endif
#if defined(QL32_BOARD_OLIMEX_ESP32_SBC_FABGL)
        profile.values[6] += ",audio";
#endif
        profile.capabilities = {{"display", true}, {"input", true},
                                {"storage", true}, {"audio",
#if defined(QL32_BOARD_OLIMEX_ESP32_SBC_FABGL)
                                true
#else
                                false
#endif
                                }, {"network", true},
                                {"wifi", true}, {"ps2", true}, {"serial", true},
                                {"sd", true}, {"ffs",
#if defined(QL32_OLIMEX_USE_SPIFFS)
                                true
#else
                                false
#endif
                                }, {"psram",
#if defined(QL32_OLIMEX_PSRAM)
                                true
#else
                                false
#endif
                                }, {"spi", true}, {"i2c", true},
                                {"gpio", true}, {"adc", true}, {"timer", true},
                                {"touch", false}, {"buttons", false},
                                {"usb-hid", false}};
        services.systemInfo = std::make_shared<ql32::runtime::StaticSystemInfoService>(
            std::move(profile));
    }
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
#if defined(QL32_BOARD_OLIMEX_ESP32_SBC_FABGL)
    boardMusicService = std::make_shared<ql32::platform::olimex_vga::MusicService>(
        services.storage);
    services.music = boardMusicService;
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
    services.clock = std::shared_ptr<ql32::runtime::ClockService>(
        &boardClock, [](ql32::runtime::ClockService*) {});
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
#if defined(QL32_OLIMEX_DMA_TEST_MODE_640X480)
    constexpr auto dmaDisplayModeline = VGA_640x480_60Hz;
#elif defined(QL32_OLIMEX_DMA_TEST_MODE_640X400)
    constexpr auto dmaDisplayModeline = VGA_640x400_70Hz;
#elif defined(QL32_OLIMEX_DMA_TEST_MODE_400X300)
    constexpr auto dmaDisplayModeline = VGA_400x300_60Hz;
#elif defined(QL32_OLIMEX_DMA_TEST_MODE_320X240)
    constexpr auto dmaDisplayModeline = QVGA_320x240_60Hz;
#else
    constexpr auto dmaDisplayModeline = VGA_320x200_70Hz;
#endif
    if (!psramDmaScanout.begin(dmaDisplayModeline,
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
    // Keep scanout counters in the PsramDmaScanout instance throughout the
    // run. UART output every second perturbs the workload and can make the
    // REPL appear stalled, so emit one accumulated snapshot after 30 seconds.
    static const std::uint32_t statusStart = millis();
    static bool statusReported = false;
#if defined(QL32_OLIMEX_DMA_TEST_PACKED4) && \
    !defined(QL32_OLIMEX_DMA_TEST_PIXEL_EDGES) && \
    (!defined(QL32_OLIMEX_DMA_TEST_CHECKER) || \
     defined(QL32_OLIMEX_DMA_TEST_ROTATE_PALETTE)) && \
    !defined(QL32_OLIMEX_DMA_TEST_NO_PALETTE_ROTATION) && \
    !defined(QL32_OLIMEX_DMA_TEST_TRIANGLE)
    static std::uint32_t lastPaletteRotation = millis();
    if (millis() - lastPaletteRotation >= 500) {
        lastPaletteRotation = millis();
        (void)psramDmaScanout.stagePaletteRotation(1);
    }
#endif
#if defined(QL32_SF_STATUS_PERIOD_MS)
    // Soak: periodic snapshots (the counters are cumulative).
    static std::uint32_t lastStatus = millis();
    if (millis() - lastStatus >= QL32_SF_STATUS_PERIOD_MS) {
        lastStatus = millis();
        psramDmaScanout.reportStatus();
    }
#else
    if (!statusReported && millis() - statusStart >= 30000) {
        statusReported = true;
        psramDmaScanout.reportStatus();
    }
#endif
    delay(1);
#else
#if defined(QL32_OLIMEX_DMA_DISPLAY) && \
    defined(QL32_OLIMEX_DMA_STATUS_TEST)
    // The counters remain in RAM until the user runs DMADIAG after a test.
#endif
    while (Serial.available() > 0) {
        if (!applicationInputOwner && !acceptsInput() && !protocolInputLine && Serial.peek() != '@') break;
        receiveSerialCharacter(static_cast<char>(Serial.read()));
    }
    if (uiPresenter) uiPresenter->flushSerialEscape(millis());

    refreshApplicationInput();
    if (keyboardInput) {
        const auto event = keyboardInput->poll();
        const bool shortcut = event && handleKeyboardShortcut(*event);
        if (event && !shortcut && applicationInputOwner) {
            applicationKeys->publish(*event);
        } else if (event && !shortcut && uiPresenter &&
            uiPresenter->graphicActive()) {
            (void)uiPresenter->handleKey(*event);
        } else if (event && !shortcut && event->pressed && scheduler &&
            !scheduler->executionPaused() && acceptsInput()) {
            handleEditorKey(*event);
        }
    }
    advanceScheduler();
#if defined(QL32_OLIMEX_PRESENT_COALESCE) || \
    (defined(QL32_BOARD_OLIMEX_ESP32_SBC_FABGL) && !defined(QL32_OLIMEX_DMA_DISPLAY))
    if (graphics) graphics->pollPendingPresentation();
#endif
    refreshInputCaretBlink();
    delay(1);
#endif
}
