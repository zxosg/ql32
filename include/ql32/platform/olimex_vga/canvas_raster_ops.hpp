#pragma once

#include "ql32/graphics/window_manager.hpp"

namespace ql32::platform::olimex_vga {

// Uses the packed framebuffer primitives on QL/32's retained 4-bit canvases.
// Operations outside the supported depth/over-mode cases fall back to the
// portable IndexedCanvas implementation.
class CanvasRasterOps final : public graphics::CanvasRasterOps {
public:
    bool clear(graphics::CanvasBufferView canvas) const noexcept override;
    bool setPixel(graphics::CanvasBufferView canvas, std::int32_t x,
                  std::int32_t y, graphics::ColorIndex color) const noexcept override;
    bool plotPixel(graphics::CanvasBufferView canvas, std::int32_t x,
                   std::int32_t y) const noexcept override;
    bool line(graphics::CanvasBufferView canvas, graphics::Point from,
              graphics::Point to) const noexcept override;
    bool fillRectangle(graphics::CanvasBufferView canvas, graphics::Point topLeft,
                       std::int32_t width, std::int32_t height) const noexcept override;
    bool circle(graphics::CanvasBufferView canvas, graphics::Point center,
                std::int32_t radius, bool filled) const noexcept override;
    bool scrollUp(graphics::CanvasBufferView canvas,
                  std::uint32_t pixelRows) const noexcept override;
};

} // namespace ql32::platform::olimex_vga
