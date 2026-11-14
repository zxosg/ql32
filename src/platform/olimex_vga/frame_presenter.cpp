#include "ql32/platform/olimex_vga/frame_presenter.hpp"
#include "ql32/platform/olimex_vga/color_mapping.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace ql32::platform::olimex_vga {

void PsramFramePresenter::present(const graphics::Frame& frame) {
    if (frame.width != scanout_.width() || frame.height != scanout_.height() ||
        frame.pixels.size() != static_cast<std::size_t>(frame.width) * frame.height)
        throw std::invalid_argument("PSRAM presenter frame does not match scanout geometry");
    const auto accepted = presentRows(frame.width, frame.height, frame.palette,
        [&frame](std::uint32_t y, std::vector<graphics::ColorIndex>& row) {
            const auto offset = static_cast<std::size_t>(y) * frame.width;
            row.assign(frame.pixels.begin() + offset,
                       frame.pixels.begin() + offset + frame.width);
        });
    if (!accepted)
        throw std::runtime_error("PSRAM presenter could not queue a frame update");
}

bool PsramFramePresenter::presentRows(
    std::uint32_t width, std::uint32_t height,
    const std::array<graphics::RgbColor, 256>& palette,
    const RowProvider& rowProvider) {
    return presentRegion(width, height, palette,
        {0, 0, width, height}, rowProvider);
}

bool PsramFramePresenter::presentRowsRegion(
    std::uint32_t width, std::uint32_t height,
    const std::array<graphics::RgbColor, 256>& palette,
    graphics::Rect region, const RowProvider& rowProvider) {
    return presentRegion(width, height, palette, region, rowProvider);
}

bool PsramFramePresenter::presentRegion(
    std::uint32_t width, std::uint32_t height,
    const std::array<graphics::RgbColor, 256>& palette,
    graphics::Rect region, const RowProvider& rowProvider) {
    if (!rowProvider || width != scanout_.width() || height != scanout_.height() ||
        region.x < 0 || region.y < 0 ||
        static_cast<std::uint64_t>(region.x) + region.width > width ||
        static_cast<std::uint64_t>(region.y) + region.height > height)
        return false;
    if (region.width == 0 || region.height == 0) return true;

    auto* frame = scanout_.beginFrameUpdate();
    if (!frame) return false;
    const bool indexed = frame->format == PixelFormat::Indexed4;
    bool paletteChanged = !paletteCacheValid_;
    for (std::size_t index = 0; !paletteChanged && index < palette.size(); ++index) {
        paletteChanged = cachedPalette_[index].red != palette[index].red ||
            cachedPalette_[index].green != palette[index].green ||
            cachedPalette_[index].blue != palette[index].blue;
    }
    if (paletteChanged) {
        if (indexed && !scanout_.stagePalette(makeHardwarePalette(palette))) {
            scanout_.cancelFrameUpdate(frame);
            return false;
        }
        cachedPalette_ = palette;
        paletteCacheValid_ = true;
        buildIndexMap(palette);
    }

    try {
        if (row_.size() < region.width) row_.resize(region.width);
        const auto bottom = static_cast<std::uint32_t>(region.y) + region.height;
        for (auto y = static_cast<std::uint32_t>(region.y); y < bottom; ++y) {
            rowProvider(y, row_);
            if (row_.size() != region.width)
                throw std::invalid_argument("PSRAM presenter row has the wrong width");
            for (std::uint32_t x = 0; x < region.width; ++x) {
                const auto sourceIndex = row_[x];
                (void)writePixel(*frame,
                    static_cast<std::uint32_t>(region.x) + x, y,
                    indexMap_[sourceIndex]);
            }
        }
    } catch (...) {
        scanout_.cancelFrameUpdate(frame);
        throw;
    }
    if (!scanout_.publishFrameUpdate(frame)) {
        scanout_.cancelFrameUpdate(frame);
        return false;
    }
    return true;
}

Palette::Entries PsramFramePresenter::makeHardwarePalette(
    const std::array<graphics::RgbColor, 256>& palette) noexcept {
    Palette::Entries result{};
    for (std::size_t index = 0; index < result.size(); ++index)
        result[index] = colorToRgb222(palette[index]);
    return result;
}

void PsramFramePresenter::buildIndexMap(
    const std::array<graphics::RgbColor, 256>& palette) noexcept {
    if (scanout_.framebufferFormat() == PixelFormat::Rgb222) {
        for (std::size_t index = 0; index < indexMap_.size(); ++index)
            indexMap_[index] = colorToRgb222(palette[index]);
        return;
    }
    for (std::size_t source = 0; source < indexMap_.size(); ++source) {
        std::size_t nearest = 0;
        auto distance = std::numeric_limits<std::uint32_t>::max();
        for (std::size_t target = 0; target < 16; ++target) {
            const auto candidate = colorDistance(palette[source], palette[target]);
            if (candidate < distance) {
                distance = candidate;
                nearest = target;
            }
        }
        indexMap_[source] = static_cast<std::uint8_t>(nearest);
    }
}

} // namespace ql32::platform::olimex_vga
