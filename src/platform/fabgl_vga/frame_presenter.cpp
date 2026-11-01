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

} // namespace ql32::platform::fabgl_vga
