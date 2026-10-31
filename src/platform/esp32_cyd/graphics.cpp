#include "ql32/platform/esp32_cyd/graphics.hpp"

#include <array>
#include <limits>
#include <stdexcept>

namespace ql32::platform::esp32_cyd {
namespace {

constexpr std::array<std::uint16_t, 8> qlPalette{{
    0x0000, // black
    0x001F, // blue
    0xF800, // red
    0xF81F, // magenta
    0x07E0, // green
    0x07FF, // cyan
    0xFFE0, // yellow
    0xFFFF  // white
}};

std::int16_t coordinate(std::int32_t value) {
    if (value < std::numeric_limits<std::int16_t>::min() ||
        value > std::numeric_limits<std::int16_t>::max())
        throw std::out_of_range("coordinate exceeds ILI9341 drawing range");
    return static_cast<std::int16_t>(value);
}

} // namespace

void Ili9341Graphics::claim(graphics::SessionId session) {
    if (hasActiveSession_ && activeSession_ != session)
        throw std::runtime_error("ILI9341 adapter supports one active graphics session");
    if (hasActiveSession_) return;
    activeSession_ = session;
    hasActiveSession_ = true;
    ink_ = 7;
    paper_ = 0;
    display_.fillScreen(color(paper_));
}

std::uint16_t Ili9341Graphics::color(graphics::ColorIndex index) const {
    if (index >= qlPalette.size())
        throw std::out_of_range("color index must be from 0 to 7");
    return qlPalette[index];
}

void Ili9341Graphics::setInk(graphics::SessionId session, graphics::ColorIndex index) {
    claim(session);
    (void)color(index);
    ink_ = index;
}

void Ili9341Graphics::setPaper(graphics::SessionId session, graphics::ColorIndex index) {
    claim(session);
    (void)color(index);
    paper_ = index;
}

void Ili9341Graphics::line(graphics::SessionId session, graphics::Point from,
                           graphics::Point to) {
    claim(session);
    display_.drawLine(coordinate(from.x), coordinate(from.y),
                      coordinate(to.x), coordinate(to.y), color(ink_));
}

void Ili9341Graphics::rectangle(graphics::SessionId session, graphics::Point topLeft,
                                std::int32_t width, std::int32_t height) {
    claim(session);
    if (width <= 0 || height <= 0)
        throw std::invalid_argument("rectangle size must be positive");
    const std::int64_t left = topLeft.x;
    const std::int64_t top = topLeft.y;
    const std::int64_t right = left + width - 1;
    const std::int64_t bottom = top + height - 1;
    const std::int64_t clippedLeft = left < 0 ? 0 : left;
    const std::int64_t clippedTop = top < 0 ? 0 : top;
    const std::int64_t clippedRight = right >= display_.width() ? display_.width() - 1 : right;
    const std::int64_t clippedBottom = bottom >= display_.height() ? display_.height() - 1 : bottom;
    if (clippedLeft > clippedRight || clippedTop > clippedBottom) return;
    display_.drawRect(static_cast<std::int16_t>(clippedLeft),
                      static_cast<std::int16_t>(clippedTop),
                      static_cast<std::int16_t>(clippedRight - clippedLeft + 1),
                      static_cast<std::int16_t>(clippedBottom - clippedTop + 1),
                      color(ink_));
}

void Ili9341Graphics::circle(graphics::SessionId session, graphics::Point center,
                             std::int32_t radius) {
    claim(session);
    if (radius < 0) throw std::invalid_argument("circle radius must be nonnegative");
    if (radius > std::numeric_limits<std::int16_t>::max())
        throw std::out_of_range("circle radius exceeds ILI9341 coordinate range");
    display_.drawCircle(coordinate(center.x), coordinate(center.y),
                        static_cast<std::int16_t>(radius), color(ink_));
}

void Ili9341Graphics::text(graphics::SessionId session, graphics::Point topLeft,
                           const std::string& value) {
    claim(session);
    display_.setCursor(coordinate(topLeft.x), coordinate(topLeft.y));
    display_.setTextColor(color(ink_), color(paper_));
    display_.setTextSize(1);
    display_.print(value.c_str());
}

void Ili9341Graphics::clear(graphics::SessionId session) {
    claim(session);
    display_.fillScreen(color(paper_));
    if (clearText_) clearText_();
}

void Ili9341Graphics::closeSession(graphics::SessionId session) {
    if (!hasActiveSession_ || activeSession_ != session) return;
    display_.fillScreen(color(paper_));
    if (clearText_) clearText_();
    activeSession_ = 0;
    hasActiveSession_ = false;
}

graphics::WindowInfo Ili9341Graphics::windowInfo(graphics::SessionId session) {
    if (hasActiveSession_ && activeSession_ != session)
        throw std::runtime_error("ILI9341 adapter supports one active graphics session");
    return {static_cast<std::uint32_t>(display_.width()),
            static_cast<std::uint32_t>(display_.height()), 16,
            static_cast<std::uint16_t>(qlPalette.size()), rotationDegrees_};
}

graphics::RgbColor Ili9341Graphics::paletteColor(graphics::SessionId session,
                                                  std::uint16_t index) {
    if (hasActiveSession_ && activeSession_ != session)
        throw std::runtime_error("ILI9341 adapter supports one active graphics session");
    if (index >= qlPalette.size())
        throw std::out_of_range("color index must be from 0 to 7");
    const std::uint16_t value = color(static_cast<graphics::ColorIndex>(index));
    return {static_cast<std::uint8_t>(((value >> 11) & 0x1F) * 255 / 31),
            static_cast<std::uint8_t>(((value >> 5) & 0x3F) * 255 / 63),
            static_cast<std::uint8_t>((value & 0x1F) * 255 / 31)};
}

void Ili9341Graphics::setRotation(graphics::SessionId session, std::uint16_t degrees) {
    if (degrees != 0 && degrees != 90 && degrees != 180 && degrees != 270)
        throw std::invalid_argument("rotation must be 0, 90, 180, or 270 degrees");
    claim(session);
    display_.setRotationDegrees(degrees);
    rotationDegrees_ = degrees;
    display_.fillScreen(color(paper_));
    if (clearText_) clearText_();
}

} // namespace ql32::platform::esp32_cyd
