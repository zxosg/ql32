#pragma once

#include "ql32/graphics/frame_presenter.hpp"
#include "ql32/platform/olimex_vga/psram_dma_scanout.hpp"

#include <array>
#include <vector>

namespace ql32::platform::olimex_vga {

// Converts QL/32's composed palette-index rows to the scanout framebuffer.
// The scanout owns a three-buffer PSRAM exchange so regions can be composed
// without exposing a partially updated frame to the VGA DMA worker.
class PsramFramePresenter final : public graphics::FramePresenter {
public:
    explicit PsramFramePresenter(PsramDmaScanout& scanout) : scanout_(scanout) {}

    void present(const graphics::Frame& frame) override;
    bool presentRows(std::uint32_t width, std::uint32_t height,
                     const std::array<graphics::RgbColor, 256>& palette,
                     const RowProvider& rowProvider) override;
    bool presentRowsRegion(std::uint32_t width, std::uint32_t height,
                           const std::array<graphics::RgbColor, 256>& palette,
                           graphics::Rect region,
                           const RowProvider& rowProvider) override;

private:
    bool presentRegion(std::uint32_t width, std::uint32_t height,
                       const std::array<graphics::RgbColor, 256>& palette,
                       graphics::Rect region, const RowProvider& rowProvider);
    static Palette::Entries makeHardwarePalette(
        const std::array<graphics::RgbColor, 256>& palette) noexcept;
    void buildIndexMap(const std::array<graphics::RgbColor, 256>& palette) noexcept;

    PsramDmaScanout& scanout_;
    std::vector<graphics::ColorIndex> row_;
    std::array<graphics::RgbColor, 256> cachedPalette_{};
    std::array<std::uint8_t, 256> indexMap_{};
    bool paletteCacheValid_ = false;
};

} // namespace ql32::platform::olimex_vga
