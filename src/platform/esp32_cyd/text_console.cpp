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
    : display_(display),
      visibleRows_(static_cast<std::size_t>(std::max<std::int16_t>(
          1, std::min<std::int16_t>(consoleHeight, display.height()) / characterHeight))),
      columns_(static_cast<std::size_t>(std::max<std::int16_t>(1,
          display.width() / characterWidth))),
      firstRowY_(static_cast<std::int16_t>(std::max<std::int16_t>(
          0, display.height() - consoleHeight))) {
    lines_.emplace_back();
    display_.fillRect(0, firstRowY_, display_.width(),
                      static_cast<std::int16_t>(display_.height() - firstRowY_),
                      consoleBackground);
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
    display_.fillRect(0, firstRowY_, display_.width(),
                      static_cast<std::int16_t>(display_.height() - firstRowY_),
                      consoleBackground);
    display_.setTextSize(1);
    display_.setTextColor(consoleForeground, consoleBackground);
    std::size_t row = 0;
    for (const auto& line : lines_) {
        display_.setCursor(0, static_cast<std::int16_t>(firstRowY_ + row * characterHeight));
        display_.print(line.c_str());
        ++row;
    }
}

} // namespace ql32::platform::esp32_cyd
