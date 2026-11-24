#include "ql32/platform/olimex_vga/canvas_raster_ops.hpp"

#include "ql32/platform/olimex_vga/framebuffer.hpp"

#include <cstdint>

namespace ql32::platform::olimex_vga {
namespace {

bool supported(graphics::CanvasBufferView canvas) noexcept {
    return canvas.pixels && canvas.depth == graphics::PixelDepth::Four &&
        canvas.width != 0 && (canvas.width & 1u) == 0 && canvas.height != 0 &&
        canvas.stride >= (static_cast<std::size_t>(canvas.width) + 1) / 2;
}

FramebufferView view(graphics::CanvasBufferView canvas) noexcept {
    return {canvas.pixels, canvas.width, canvas.height, canvas.stride,
            PixelFormat::Indexed4};
}

bool invertMode(graphics::CanvasBufferView canvas) noexcept {
    return canvas.overMode == graphics::OverMode::Invert;
}

std::uint8_t drawColor(graphics::CanvasBufferView canvas) noexcept {
    return canvas.overMode == graphics::OverMode::Clear ? canvas.paper : canvas.ink;
}

bool fullyInside(graphics::CanvasBufferView canvas, graphics::Point from,
                 graphics::Point to) noexcept {
    return from.x >= 0 && from.y >= 0 && to.x >= 0 && to.y >= 0 &&
        static_cast<std::uint32_t>(from.x) < canvas.width &&
        static_cast<std::uint32_t>(to.x) < canvas.width &&
        static_cast<std::uint32_t>(from.y) < canvas.height &&
        static_cast<std::uint32_t>(to.y) < canvas.height;
}

} // namespace

bool CanvasRasterOps::clear(graphics::CanvasBufferView canvas) const noexcept {
    if (!supported(canvas)) return false;
    ql32::platform::olimex_vga::clear(view(canvas), canvas.paper);
    return true;
}

bool CanvasRasterOps::setPixel(graphics::CanvasBufferView canvas, std::int32_t x,
                               std::int32_t y,
                               graphics::ColorIndex color) const noexcept {
    if (!supported(canvas)) return false;
    return writePixel(view(canvas), static_cast<std::uint32_t>(x),
                      static_cast<std::uint32_t>(y), color);
}

bool CanvasRasterOps::plotPixel(graphics::CanvasBufferView canvas, std::int32_t x,
                                std::int32_t y) const noexcept {
    if (!supported(canvas)) return false;
    const auto px = static_cast<std::uint32_t>(x);
    const auto py = static_cast<std::uint32_t>(y);
    if (canvas.overMode == graphics::OverMode::Invert) {
        const auto value = static_cast<std::uint8_t>(
            (readPixel(view(canvas), px, py) ^ canvas.ink) & 0x0Fu);
        return writePixel(view(canvas), px, py, value);
    }
    return writePixel(view(canvas), px, py, drawColor(canvas));
}

bool CanvasRasterOps::line(graphics::CanvasBufferView canvas, graphics::Point from,
                           graphics::Point to) const noexcept {
    if (!supported(canvas) || invertMode(canvas) ||
        !fullyInside(canvas, from, to)) return false;
    drawLine(view(canvas), from.x, from.y, to.x, to.y, drawColor(canvas));
    return true;
}

bool CanvasRasterOps::fillRectangle(graphics::CanvasBufferView canvas,
                                    graphics::Point topLeft, std::int32_t width,
                                    std::int32_t height) const noexcept {
    if (!supported(canvas) || invertMode(canvas)) return false;
    fillRect(view(canvas), topLeft.x, topLeft.y, width, height, drawColor(canvas));
    return true;
}

bool CanvasRasterOps::circle(graphics::CanvasBufferView canvas,
                             graphics::Point center, std::int32_t radius,
                             bool filled) const noexcept {
    if (!supported(canvas) || invertMode(canvas) || radius < 0)
        return false;
    const auto cx = static_cast<std::int64_t>(center.x);
    const auto cy = static_cast<std::int64_t>(center.y);
    const auto r = static_cast<std::int64_t>(radius);
    if (!filled && (cx - r < 0 || cy - r < 0 || cx + r >= canvas.width ||
        cy + r >= canvas.height)) return false;
    drawCircle(view(canvas), center.x, center.y, radius, drawColor(canvas), filled);
    return true;
}

bool CanvasRasterOps::scrollUp(graphics::CanvasBufferView canvas,
                               std::uint32_t pixelRows) const noexcept {
    if (!supported(canvas)) return false;
    const auto distance = pixelRows > static_cast<std::uint32_t>(INT32_MAX)
        ? INT32_MAX : static_cast<std::int32_t>(pixelRows);
    scrollVertical(view(canvas), distance, canvas.paper);
    return true;
}

} // namespace ql32::platform::olimex_vga
