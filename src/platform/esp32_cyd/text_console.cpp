#include "ql32/platform/esp32_cyd/text_console.hpp"

#if defined(QL32_TFT_CONSOLE_SERIAL_MIRROR)
#include <Arduino.h>
#endif

#include <algorithm>
#include <cstdint>

namespace ql32::platform::esp32_cyd {
namespace {

constexpr std::int16_t characterWidth = 6;
constexpr std::int16_t characterHeight = 8;
constexpr std::int16_t consoleHeight = 64;
constexpr std::uint16_t consoleBackground = 0x0000;
constexpr std::uint16_t consoleForeground = 0xFFFF;

} // namespace

TftTextConsole::TftTextConsole(Adafruit_ILI9341& display)
    : display_(display), visibleRows_(1), columns_(1), firstRowY_(0) {
    updateGeometry();
    lines_.emplace_back();
    display_.fillRect(0, firstRowY_, display_.width(),
                      static_cast<std::int16_t>(display_.height() - firstRowY_),
                      consoleBackground);
}

void TftTextConsole::updateGeometry() {
    visibleRows_ = static_cast<std::size_t>(std::max<std::int16_t>(
        1, std::min<std::int16_t>(consoleHeight, display_.height()) / characterHeight));
    columns_ = static_cast<std::size_t>(std::max<std::int16_t>(1,
        display_.width() / characterWidth));
    firstRowY_ = static_cast<std::int16_t>(std::max<std::int16_t>(
        0, display_.height() - consoleHeight));
}

void TftTextConsole::reconfigure() {
    updateGeometry();
    clear();
}

void TftTextConsole::restoreOverlay(graphics::Rect region) {
    redraw(region);
}

void TftTextConsole::clear() {
    lines_.clear();
    lines_.emplace_back();
    display_.fillRect(0, firstRowY_, display_.width(),
                      static_cast<std::int16_t>(display_.height() - firstRowY_),
                      consoleBackground);
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
    display_.setCursor(static_cast<std::int16_t>(column * characterWidth),
                       static_cast<std::int16_t>(firstRowY_ + row * characterHeight));
    display_.setTextSize(1);
    display_.setTextColor(consoleForeground, consoleBackground);
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
    redraw({0, firstRowY_, static_cast<std::uint32_t>(display_.width()),
            static_cast<std::uint32_t>(display_.height() - firstRowY_)});
}

void TftTextConsole::redraw(graphics::Rect region) {
    const auto left = std::max<std::int32_t>(0, region.x);
    const auto top = std::max<std::int32_t>(firstRowY_, region.y);
    const auto right = std::min<std::int32_t>(display_.width(),
        static_cast<std::int32_t>(static_cast<std::int64_t>(region.x) + region.width));
    const auto bottom = std::min<std::int32_t>(display_.height(),
        static_cast<std::int32_t>(static_cast<std::int64_t>(region.y) + region.height));
    if (left >= right || top >= bottom) return;

    const auto firstColumn = left / characterWidth;
    const auto lastColumn = std::min<std::int32_t>(columns_,
        (right + characterWidth - 1) / characterWidth);
    const auto alignedLeft = firstColumn * characterWidth;
    const auto alignedRight = std::min<std::int32_t>(display_.width(),
        lastColumn * characterWidth);
    const auto firstRow = (top - firstRowY_) / characterHeight;
    const auto lastRow = std::min<std::int32_t>(visibleRows_,
        (bottom - firstRowY_ + characterHeight - 1) / characterHeight);
    const auto alignedTop = firstRowY_ + firstRow * characterHeight;
    const auto alignedBottom = std::min<std::int32_t>(display_.height(),
        firstRowY_ + lastRow * characterHeight);
    display_.fillRect(static_cast<std::int16_t>(alignedLeft),
                      static_cast<std::int16_t>(alignedTop),
                      static_cast<std::int16_t>(alignedRight - alignedLeft),
                      static_cast<std::int16_t>(alignedBottom - alignedTop),
                      consoleBackground);

    display_.setTextSize(1);
    display_.setTextColor(consoleForeground, consoleBackground);
    for (std::int32_t row = firstRow; row < lastRow; ++row) {
        if (static_cast<std::size_t>(row) >= lines_.size()) break;
        const auto& line = lines_[static_cast<std::size_t>(row)];
        const auto start = std::min<std::size_t>(
            static_cast<std::size_t>(firstColumn), line.size());
        const auto end = std::min<std::size_t>(
            static_cast<std::size_t>(lastColumn), line.size());
        if (start >= end) continue;
        display_.setCursor(static_cast<std::int16_t>(firstColumn * characterWidth),
            static_cast<std::int16_t>(firstRowY_ + row * characterHeight));
        const auto clipped = line.substr(start, end - start);
        display_.print(clipped.c_str());
    }
}

} // namespace ql32::platform::esp32_cyd
