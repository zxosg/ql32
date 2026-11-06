#include "ql32/platform/tenstar_esp32s3/text_console.hpp"
#include "ql32/graphics/window_manager.hpp"

#if defined(QL32_TFT_CONSOLE_SERIAL_MIRROR)
#include <Arduino.h>
#endif

#include <algorithm>
#include <array>
#include <cstdint>

namespace ql32::platform::tenstar_esp32s3 {
namespace {

constexpr std::int16_t characterWidth = 6;
constexpr std::int16_t characterHeight = 8;
constexpr std::int16_t consoleHeight = 40;

std::uint16_t color565(graphics::ColorIndex index) {
    static constexpr std::array<graphics::RgbColor, 16> palette{{
        {0, 0, 0}, {0, 0, 255}, {255, 0, 0}, {255, 0, 255},
        {0, 255, 0}, {0, 255, 255}, {255, 255, 0}, {255, 255, 255},
        {85, 85, 85}, {85, 85, 255}, {255, 85, 85}, {255, 85, 255},
        {85, 255, 85}, {85, 255, 255}, {255, 255, 85}, {192, 192, 192}
    }};
    const auto& color = palette[index % palette.size()];
    return static_cast<std::uint16_t>(((color.red & 0xF8u) << 8u) |
        ((color.green & 0xFCu) << 3u) | (color.blue >> 3u));
}

} // namespace

TftTextConsole::TftTextConsole(Adafruit_ST7789& display)
    : display_(display), visibleRows_(1), columns_(1), firstRowY_(0),
      firstColumnX_(0) {
    updateGeometry();
    lines_.emplace_back();
    display_.fillRect(0, firstRowY_, display_.width(),
                      static_cast<std::int16_t>(display_.height() - firstRowY_),
                      backgroundColor_);
}

void TftTextConsole::updateGeometry() {
    const auto width = static_cast<std::uint32_t>(std::max<std::int16_t>(0, display_.width()));
    const auto height = static_cast<std::uint32_t>(std::max<std::int16_t>(0, display_.height()));
    if (bounds_.width == 0 || bounds_.height == 0)
        bounds_ = {0, static_cast<std::int32_t>(height > consoleHeight ?
            height - consoleHeight : 0), width, std::min<std::uint32_t>(height, consoleHeight)};
    bounds_.x = std::clamp<std::int32_t>(bounds_.x, 0, static_cast<std::int32_t>(width));
    bounds_.y = std::clamp<std::int32_t>(bounds_.y, 0, static_cast<std::int32_t>(height));
    bounds_.width = std::min(bounds_.width, width - static_cast<std::uint32_t>(bounds_.x));
    bounds_.height = std::min(bounds_.height, height - static_cast<std::uint32_t>(bounds_.y));
    visibleRows_ = static_cast<std::size_t>(std::max<std::int16_t>(
        1, static_cast<std::int16_t>(bounds_.height) / characterHeight));
    columns_ = static_cast<std::size_t>(std::max<std::int16_t>(1,
        static_cast<std::int16_t>(bounds_.width) / characterWidth));
    firstRowY_ = static_cast<std::int16_t>(bounds_.y);
    firstColumnX_ = static_cast<std::int16_t>(bounds_.x);
}

void TftTextConsole::reconfigure() {
    bounds_ = {};
    updateGeometry();
    clear();
}

void TftTextConsole::setBounds(graphics::Rect bounds) {
    if (bounds.width == 0 || bounds.height == 0) return;
    bounds_ = bounds;
    updateGeometry();
    clear();
}

void TftTextConsole::setColors(graphics::ColorIndex ink, graphics::ColorIndex paper) {
    foregroundIndex_ = ink;
    backgroundIndex_ = paper;
    foregroundColor_ = color565(ink);
    backgroundColor_ = color565(paper);
    clear();
}

void TftTextConsole::restoreOverlay(graphics::Rect region) {
    if (invalidate_) return;
    redraw(region);
}

void TftTextConsole::composePaletteRow(std::uint32_t y,
                                       graphics::ColorIndex* destination,
                                       std::uint32_t width) const {
    if (!destination) return;
    std::vector<graphics::ColorIndex> row(destination, destination + width);
    composePaletteRegionRow(y, 0, row);
    std::copy(row.begin(), row.end(), destination);
}

void TftTextConsole::composePaletteRegionRow(
    std::uint32_t y, std::uint32_t xOrigin,
    std::vector<graphics::ColorIndex>& row) const {
    if (y >= static_cast<std::uint32_t>(display_.height())) return;
    const auto left = static_cast<std::uint32_t>(std::max<std::int32_t>(0, bounds_.x));
    const auto right = left + bounds_.width;
    if (y < static_cast<std::uint32_t>(std::max<std::int32_t>(0, bounds_.y)) ||
        y >= static_cast<std::uint32_t>(bounds_.y + bounds_.height) || left >= right)
        return;

    const auto localY = y - static_cast<std::uint32_t>(bounds_.y);
    const auto textRow = localY / characterHeight;
    const auto glyphRow = localY % characterHeight;
    const std::string* line = textRow < lines_.size() ? &lines_[textRow] : nullptr;
    for (std::uint32_t i = 0; i < row.size(); ++i) {
        const auto x = xOrigin + i;
        if (x < left || x >= right) continue;
        bool foreground = false;
        if (line && glyphRow < 7 && x >= left) {
            const auto localX = x - left;
            const auto column = localX / characterWidth;
            const auto glyphColumn = localX % characterWidth;
            if (column < line->size() && glyphColumn < 5) {
                const auto glyph = graphics::classicGlyphColumns((*line)[column]);
                foreground = (glyph[glyphColumn] & (1u << glyphRow)) != 0;
            }
        }
        row[i] = foreground ? foregroundIndex_ : backgroundIndex_;
    }
}

void TftTextConsole::clear() {
    lines_.clear();
    lines_.emplace_back();
    if (invalidate_) { invalidate_(bounds_); return; }
    display_.fillRect(firstColumnX_, firstRowY_,
                      static_cast<std::int16_t>(bounds_.width),
                      static_cast<std::int16_t>(bounds_.height),
                      backgroundColor_);
}

TftTextConsole::int_type TftTextConsole::overflow(int_type character) {
    if (traits_type::eq_int_type(character, traits_type::eof()))
        return traits_type::not_eof(character);
    put(traits_type::to_char_type(character));
    return character;
}

std::streamsize TftTextConsole::xsputn(const char* data, std::streamsize size) {
    for (std::streamsize i = 0; i < size; ++i) put(data[i]);
    return size;
}

void TftTextConsole::put(char character) {
#if defined(QL32_TFT_CONSOLE_SERIAL_MIRROR)
    Serial.write(static_cast<std::uint8_t>(character));
#endif
    if (character == '\r') return;
    if (character == '\n') {
        advanceLine();
        return;
    }
    if (static_cast<unsigned char>(character) < 32 ||
        static_cast<unsigned char>(character) >= 127)
        character = '?';
    if (lines_.back().size() >= columns_) advanceLine();
    lines_.back().push_back(character);

    const auto row = lines_.size() - 1;
    const auto column = lines_.back().size() - 1;
    if (invalidate_) {
        invalidate_({firstColumnX_ + static_cast<std::int32_t>(column * characterWidth),
                     firstRowY_ + static_cast<std::int32_t>(row * characterHeight),
                     characterWidth, characterHeight});
        return;
    }
    display_.setCursor(static_cast<std::int16_t>(firstColumnX_ + column * characterWidth),
                       static_cast<std::int16_t>(firstRowY_ + row * characterHeight));
    display_.setTextSize(1);
    display_.setTextColor(foregroundColor_, backgroundColor_);
    display_.print(character);
}

void TftTextConsole::advanceLine() {
    lines_.emplace_back();
    if (lines_.size() > visibleRows_) {
        lines_.pop_front();
        redraw();
    }
}

void TftTextConsole::redraw() {
    redraw(bounds_);
}

void TftTextConsole::redraw(graphics::Rect region) {
    if (invalidate_) { invalidate_(region); return; }
    const auto left = std::max<std::int32_t>(bounds_.x, region.x);
    const auto top = std::max<std::int32_t>(bounds_.y, region.y);
    const auto right = std::min<std::int32_t>(bounds_.x + static_cast<std::int32_t>(bounds_.width),
        static_cast<std::int32_t>(static_cast<std::int64_t>(region.x) + region.width));
    const auto bottom = std::min<std::int32_t>(bounds_.y + static_cast<std::int32_t>(bounds_.height),
        static_cast<std::int32_t>(static_cast<std::int64_t>(region.y) + region.height));
    if (left >= right || top >= bottom) return;

    const auto firstColumn = (left - bounds_.x) / characterWidth;
    const auto lastColumn = std::min<std::int32_t>(columns_,
        (right - bounds_.x + characterWidth - 1) / characterWidth);
    const auto alignedLeft = bounds_.x + firstColumn * characterWidth;
    const auto alignedRight = std::min<std::int32_t>(bounds_.x + bounds_.width,
        bounds_.x + lastColumn * characterWidth);
    const auto firstRow = (top - bounds_.y) / characterHeight;
    const auto lastRow = std::min<std::int32_t>(visibleRows_,
        (bottom - bounds_.y + characterHeight - 1) / characterHeight);
    const auto alignedTop = bounds_.y + firstRow * characterHeight;
    const auto alignedBottom = std::min<std::int32_t>(bounds_.y + bounds_.height,
        bounds_.y + lastRow * characterHeight);
    if (alignedLeft < alignedRight && alignedTop < alignedBottom)
        display_.fillRect(static_cast<std::int16_t>(alignedLeft),
                          static_cast<std::int16_t>(alignedTop),
                          static_cast<std::int16_t>(alignedRight - alignedLeft),
                          static_cast<std::int16_t>(alignedBottom - alignedTop),
                          backgroundColor_);
    display_.setTextSize(1);
    display_.setTextColor(foregroundColor_, backgroundColor_);
    for (std::int32_t row = firstRow; row < lastRow; ++row) {
        if (static_cast<std::size_t>(row) >= lines_.size()) break;
        const auto& line = lines_[static_cast<std::size_t>(row)];
        const auto start = std::min<std::size_t>(
            static_cast<std::size_t>(firstColumn), line.size());
        const auto end = std::min<std::size_t>(
            static_cast<std::size_t>(lastColumn), line.size());
        if (start >= end) continue;
        display_.setCursor(static_cast<std::int16_t>(bounds_.x + firstColumn * characterWidth),
            static_cast<std::int16_t>(bounds_.y + row * characterHeight));
        const auto clipped = line.substr(start, end - start);
        display_.print(clipped.c_str());
    }
}

} // namespace ql32::platform::tenstar_esp32s3
