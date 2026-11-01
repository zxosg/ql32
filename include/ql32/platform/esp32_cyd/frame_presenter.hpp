#pragma once

#include "ql32/graphics/frame_presenter.hpp"
#include "ql32/platform/esp32_cyd/graphics.hpp"

#include <functional>
#include <vector>

namespace ql32::platform::esp32_cyd {

class Ili9341FramePresenter final : public graphics::FramePresenter {
public:
    Ili9341FramePresenter(CydIli9341Display& display,
                          std::function<void()> restoreOverlay,
                          std::function<void()> rotationChanged);

    void present(const graphics::Frame& frame) override;
    bool presentRows(std::uint32_t width, std::uint32_t height,
                    const std::array<graphics::RgbColor, 256>& palette,
                    const graphics::FramePresenter::RowProvider& rowProvider) override;
    graphics::WindowInfo setRotation(std::uint16_t degrees) override;

private:
    CydIli9341Display& display_;
    std::function<void()> restoreOverlay_;
    std::function<void()> rotationChanged_;
    std::vector<std::uint16_t> row_;
    std::vector<graphics::ColorIndex> indexedRow_;
    std::uint16_t rotationDegrees_ = 0;
};

} // namespace ql32::platform::esp32_cyd
