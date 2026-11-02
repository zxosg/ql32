#include "ql32/platform/fabgl_vga/frame_presenter.hpp"

#include <stdexcept>

namespace ql32::platform::fabgl_vga {

void FabglFramePresenter::present(const graphics::Frame& frame) {
    if (frame.width != static_cast<std::uint32_t>(canvas_.getWidth()) ||
        frame.height != static_cast<std::uint32_t>(canvas_.getHeight()) ||
        frame.pixels.size() != static_cast<std::size_t>(frame.width) * frame.height)
        throw std::invalid_argument("composed frame does not match VGA canvas geometry");

    canvas_.beginUpdate();
    for (std::uint32_t y = 0; y < frame.height; ++y) {
        const auto offset = static_cast<std::size_t>(y) * frame.width;
        for (std::uint32_t x = 0; x < frame.width; ++x) {
            const auto& color = frame.palette[frame.pixels[offset + x]];
            canvas_.setPixel(static_cast<int>(x), static_cast<int>(y),
                             fabgl::RGB888(color.red, color.green, color.blue));
        }
        canvas_.waitCompletion(false);
    }
    canvas_.endUpdate();
    canvas_.waitCompletion(true);
}

bool FabglFramePresenter::presentRows(
    std::uint32_t width, std::uint32_t height,
    const std::array<graphics::RgbColor, 256>& palette,
    const RowProvider& rowProvider) {
    return presentRowsRegion(width, height, palette, {0, 0, width, height}, rowProvider);
}

bool FabglFramePresenter::presentRowsRegion(
    std::uint32_t width, std::uint32_t height,
    const std::array<graphics::RgbColor, 256>& palette, graphics::Rect region,
    const RowProvider& rowProvider) {
    if (width != static_cast<std::uint32_t>(canvas_.getWidth()) ||
        height != static_cast<std::uint32_t>(canvas_.getHeight()))
        throw std::invalid_argument("composed rows do not match VGA canvas geometry");
    if (region.x < 0 || region.y < 0 ||
        static_cast<std::uint64_t>(region.x) + region.width > width ||
        static_cast<std::uint64_t>(region.y) + region.height > height)
        throw std::invalid_argument("composed region exceeds VGA canvas geometry");
    if (region.width == 0 || region.height == 0) return true;

    if (indexedRow_.size() < region.width) indexedRow_.resize(region.width);
    canvas_.beginUpdate();
    for (std::uint32_t y = static_cast<std::uint32_t>(region.y);
         y < static_cast<std::uint32_t>(region.y) + region.height; ++y) {
        rowProvider(y, indexedRow_);
        if (indexedRow_.size() != region.width) {
            canvas_.endUpdate();
            throw std::invalid_argument("composed row does not match VGA region width");
        }
        for (std::uint32_t x = 0; x < region.width; ++x) {
            const auto& color = palette[indexedRow_[x]];
            canvas_.setPixel(region.x + static_cast<std::int32_t>(x),
                             static_cast<std::int32_t>(y),
                             fabgl::RGB888(color.red, color.green, color.blue));
        }
        canvas_.waitCompletion(false);
    }
    canvas_.endUpdate();
    canvas_.waitCompletion(true);
    return true;
}

} // namespace ql32::platform::fabgl_vga
