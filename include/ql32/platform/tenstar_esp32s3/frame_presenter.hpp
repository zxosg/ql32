#pragma once

#include "ql32/graphics/frame_presenter.hpp"
#include "ql32/platform/tenstar_esp32s3/graphics.hpp"

#include <functional>
#include <vector>

namespace ql32::platform::tenstar_esp32s3 {

class St7789FramePresenter final : public graphics::FramePresenter {
public:
    St7789FramePresenter(TenstarSt7789Display& display,
                         std::function<void(graphics::Rect)> restoreOverlay,
                         std::function<void()> rotationChanged);
    void present(const graphics::Frame&) override;
    bool presentRows(std::uint32_t, std::uint32_t,
                     const std::array<graphics::RgbColor, 256>&,
                     const RowProvider&) override;
    bool presentRowsRegion(std::uint32_t, std::uint32_t,
                           const std::array<graphics::RgbColor, 256>&,
                           graphics::Rect, const RowProvider&) override;
    graphics::WindowInfo setRotation(std::uint16_t degrees) override;

private:
    TenstarSt7789Display& display_;
    std::function<void(graphics::Rect)> restoreOverlay_;
    std::function<void()> rotationChanged_;
    std::vector<std::uint16_t> row_;
    std::vector<graphics::ColorIndex> indexedRow_;
    std::uint16_t rotationDegrees_ = 0;
};

} // namespace ql32::platform::tenstar_esp32s3
