#pragma once

#include "ql32/graphics/frame_presenter.hpp"

#include <fabgl.h>

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace ql32::platform::fabgl_vga {

struct PresentationProfile {
    std::uint64_t fullFrames = 0;
    std::uint64_t partialFrames = 0;
    std::uint64_t regionRequests = 0;
    std::uint64_t fullFallbacks = 0;
    std::uint64_t rowsComposed = 0;
    std::uint64_t pixelsComposed = 0;
    std::uint64_t pixelsWritten = 0;
    std::uint64_t composeMicros = 0;
    std::uint64_t convertMicros = 0;
    std::uint64_t writeMicros = 0;
    std::uint64_t swapMicros = 0;
    std::uint64_t swapCalls = 0;
    std::uint64_t swapMinMicros = 0;
    std::uint64_t swapMaxMicros = 0;
};

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
    PresentationProfile presentationProfile() const noexcept { return profile_; }
    void resetPresentationProfile() noexcept { profile_ = {}; }

private:
    bool doubleBuffered() const noexcept;
    bool presentDirect(std::uint32_t width, std::uint32_t height,
                       const std::array<graphics::RgbColor, 256>& palette,
                       const RowProvider& rowProvider);
    bool presentDirectRegion(std::uint32_t width, std::uint32_t height,
                             const std::array<graphics::RgbColor, 256>& palette,
                             graphics::Rect region,
                             const RowProvider& rowProvider);

    fabgl::VGAController& display_;
    fabgl::Canvas& canvas_;
    std::function<void(graphics::Rect)> restoreOverlay_;
    std::vector<graphics::ColorIndex> indexedRow_;
    std::vector<fabgl::RGBA8888> rgbaBand_;
    std::array<graphics::RgbColor, 256> cachedPalette_{};
    std::array<fabgl::RGBA8888, 256> rgbaPalette_{};
    std::array<std::uint8_t, 256> rawPalette_{};
    std::vector<std::uint8_t> rawRegion_;
    bool paletteCacheValid_ = false;
    bool rawPaletteCacheValid_ = false;
    bool doubleBuffersSynchronized_ = false;
    bool synchronizeOnNextFullPresentation_ = false;
    PresentationProfile profile_{};
};

} // namespace ql32::platform::fabgl_vga
