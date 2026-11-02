#pragma once

#include "ql32/graphics/frame_presenter.hpp"

#include <fabgl.h>

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace ql32::platform::fabgl_vga {

class FabglFramePresenter final : public graphics::FramePresenter {
public:
    explicit FabglFramePresenter(fabgl::Canvas& canvas,
        std::function<void(graphics::Rect)> restoreOverlay = {})
        : canvas_(canvas), restoreOverlay_(std::move(restoreOverlay)) {}
    void present(const graphics::Frame& frame) override;
    bool presentRows(std::uint32_t width, std::uint32_t height,
                     const std::array<graphics::RgbColor, 256>& palette,
                     const RowProvider& rowProvider) override;
    bool presentRowsRegion(std::uint32_t width, std::uint32_t height,
                           const std::array<graphics::RgbColor, 256>& palette,
                           graphics::Rect region,
                           const RowProvider& rowProvider) override;

private:
    fabgl::Canvas& canvas_;
    std::function<void(graphics::Rect)> restoreOverlay_;
    std::vector<graphics::ColorIndex> indexedRow_;
};

} // namespace ql32::platform::fabgl_vga
