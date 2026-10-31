#include "ql32/platform/fabgl_vga/graphics.hpp"

#include <array>
#include <limits>
#include <stdexcept>

namespace ql32::platform::fabgl_vga {
namespace {

struct Rgb {
    std::uint8_t red;
    std::uint8_t green;
    std::uint8_t blue;
};

// QL mode-8 order: black, blue, red, magenta, green, cyan, yellow, white.
constexpr std::array<Rgb, 8> palette{{
    {0, 0, 0}, {0, 0, 1}, {1, 0, 0}, {1, 0, 1},
    {0, 1, 0}, {0, 1, 1}, {1, 1, 0}, {1, 1, 1}
}};

} // namespace

void FabglGraphics::claim(graphics::SessionId session) {
    if (hasActiveSession_ && activeSession_ != session)
        throw std::runtime_error("FabGL VGA adapter supports one active graphics session");
    if (hasActiveSession_) return;

    activeSession_ = session;
    hasActiveSession_ = true;
    ink_ = 7;
    paper_ = 0;
    applyInk();
    applyPaper();
    canvas_.clear();
}

void FabglGraphics::applyInk() {
    const Rgb color = palette[ink_];
    canvas_.setPenColor(color.red, color.green, color.blue);
}

void FabglGraphics::applyPaper() {
    const Rgb color = palette[paper_];
    canvas_.setBrushColor(color.red, color.green, color.blue);
}

void FabglGraphics::setInk(graphics::SessionId session, graphics::ColorIndex color) {
    claim(session);
    if (color >= palette.size()) throw std::out_of_range("ink color index must be from 0 to 7");
    ink_ = color;
    applyInk();
}

void FabglGraphics::setPaper(graphics::SessionId session, graphics::ColorIndex color) {
    claim(session);
    if (color >= palette.size()) throw std::out_of_range("paper color index must be from 0 to 7");
    paper_ = color;
    applyPaper();
}

void FabglGraphics::line(graphics::SessionId session, graphics::Point from,
                         graphics::Point to) {
    claim(session);
    applyInk();
    canvas_.drawLine(from.x, from.y, to.x, to.y);
}

void FabglGraphics::rectangle(graphics::SessionId session, graphics::Point topLeft,
                              std::int32_t width, std::int32_t height) {
    claim(session);
    if (width <= 0 || height <= 0) throw std::invalid_argument("rectangle size must be positive");
    const std::int64_t right = static_cast<std::int64_t>(topLeft.x) + width - 1;
    const std::int64_t bottom = static_cast<std::int64_t>(topLeft.y) + height - 1;
    const std::int64_t left = topLeft.x;
    const std::int64_t upper = topLeft.y;
    const std::int64_t clippedLeft = left < 0 ? 0 : left;
    const std::int64_t clippedTop = upper < 0 ? 0 : upper;
    const std::int64_t clippedRight = right >= canvas_.getWidth() ?
        canvas_.getWidth() - 1 : right;
    const std::int64_t clippedBottom = bottom >= canvas_.getHeight() ?
        canvas_.getHeight() - 1 : bottom;
    if (clippedLeft > clippedRight || clippedTop > clippedBottom) return;
    applyInk();
    canvas_.drawRectangle(static_cast<int>(clippedLeft), static_cast<int>(clippedTop),
                          static_cast<int>(clippedRight), static_cast<int>(clippedBottom));
}

void FabglGraphics::circle(graphics::SessionId session, graphics::Point center,
                           std::int32_t radius) {
    claim(session);
    if (radius < 0) throw std::invalid_argument("circle radius must be nonnegative");
    if (radius > std::numeric_limits<int>::max() / 2)
        throw std::out_of_range("circle diameter exceeds FabGL coordinate range");
    applyInk();
    const int diameter = static_cast<int>(radius) * 2;
    canvas_.drawEllipse(center.x, center.y, diameter, diameter);
}

void FabglGraphics::text(graphics::SessionId session, graphics::Point topLeft,
                         const std::string& value) {
    claim(session);
    applyInk();
    canvas_.drawText(topLeft.x, topLeft.y, value.c_str());
}

void FabglGraphics::clear(graphics::SessionId session) {
    claim(session);
    applyPaper();
    canvas_.clear();
}

void FabglGraphics::closeSession(graphics::SessionId session) {
    if (!hasActiveSession_ || activeSession_ != session) return;
    applyPaper();
    canvas_.clear();
    hasActiveSession_ = false;
    activeSession_ = 0;
}

} // namespace ql32::platform::fabgl_vga
