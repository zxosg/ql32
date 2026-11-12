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
    explicit FabglFramePresenter(fabgl::VGAController& display,
        fabgl::Canvas& canvas,
        std::function<void(graphics::Rect)> restoreOverlay = {})
        : display_(display), canvas_(canvas),
          restoreOverlay_(std::move(restoreOverlay)) {}
    void present(const graphics::Frame& frame) override;
    bool presentRows(std::uint32_t width, std::uint32_t height,
                     const std::array<graphics::RgbColor, 256>& palette,
                     const RowProvider& rowProvider) override;
    bool presentRowsRegion(std::uint32_t width, std::uint32_t height,
                           const std::array<graphics::RgbColor, 256>& palette,
                           graphics::Rect region,
                           const RowProvider& rowProvider) override;

private:
    bool doubleBuffered() const noexcept;
    bool presentDirect(std::uint32_t width, std::uint32_t height,
                       const std::array<graphics::RgbColor, 256>& palette,
                       const RowProvider& rowProvider);

    fabgl::VGAController& display_;
    fabgl::Canvas& canvas_;
    std::function<void(graphics::Rect)> restoreOverlay_;
    std::vector<graphics::ColorIndex> indexedRow_;
    std::vector<fabgl::RGBA8888> rgbaBand_;
    std::array<graphics::RgbColor, 256> cachedPalette_{};
    std::array<fabgl::RGBA8888, 256> rgbaPalette_{};
    std::array<std::uint8_t, 256> rawPalette_{};
    bool paletteCacheValid_ = false;
    bool rawPaletteCacheValid_ = false;
};

} // namespace ql32::platform::fabgl_vga
