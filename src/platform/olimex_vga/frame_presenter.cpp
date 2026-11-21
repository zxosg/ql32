#include "ql32/platform/olimex_vga/frame_presenter.hpp"
#include "ql32/platform/olimex_vga/color_mapping.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

#if defined(QL32_OLIMEX_PRESENT_PROFILE)
#include <Arduino.h>
#define QL32_PRESENT_NOW() static_cast<std::uint32_t>(micros())
#else
#define QL32_PRESENT_NOW() 0u
#endif
#if defined(ARDUINO)
#include <Arduino.h>
#endif

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

// Waits (yielding the CPU) until the scanout starts a new frame, bounded so a
// stalled scanout can never hang the session.
void PsramFramePresenter::throttle(std::uint32_t rowBytes, std::uint32_t& waits,
                                   std::uint32_t& waitedUs) {
#if defined(ARDUINO)
    if (!budget_.charge(scanout_.frameCount(), rowBytes)) return;
    const auto start = static_cast<std::uint32_t>(micros());
    ++waits;
    constexpr std::uint32_t kMaxWaitUs = 70000;
    while (budget_.exhausted(scanout_.frameCount()) &&
           static_cast<std::uint32_t>(micros()) - start < kMaxWaitUs)
        vTaskDelay(1);
    waitedUs += static_cast<std::uint32_t>(micros()) - start;
#else
    (void)rowBytes; (void)waits; (void)waitedUs;
#endif
}

bool PsramFramePresenter::presentRegion(
    std::uint32_t width, std::uint32_t height,
    const std::array<graphics::RgbColor, 256>& palette,
    graphics::Rect region, const RowProvider& rowProvider) {
    if (!rowProvider || width != scanout_.width() || height != scanout_.height() ||
        region.x < 0 || region.y < 0 ||
        static_cast<std::uint64_t>(region.x) + region.width > width ||
        static_cast<std::uint64_t>(region.y) + region.height > height) {
#if defined(QL32_OLIMEX_PRESENT_PROFILE)
        stats_.recordRejected();
#endif
        return false;
    }
    if (region.width == 0 || region.height == 0) return true;

    const PixelRect damage{static_cast<std::uint32_t>(region.x),
                           static_cast<std::uint32_t>(region.y),
                           region.width, region.height};
    const auto t0 = QL32_PRESENT_NOW();
    auto* frame = scanout_.beginFrameUpdate(damage);
    const auto t1 = QL32_PRESENT_NOW();
    if (!frame) {
#if defined(QL32_OLIMEX_PRESENT_PROFILE)
        stats_.recordRejected();
#endif
        return false;
    }
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
#if defined(QL32_OLIMEX_PRESENT_PROFILE)
        ++stats_.paletteChanges;
#endif
        cachedPalette_ = palette;
        paletteCacheValid_ = true;
        buildIndexMap(palette);
    }

    std::uint32_t providerUs = 0;
    std::uint32_t writeUs = 0;
    std::uint32_t throttleWaits = 0;
    std::uint32_t throttleUs = 0;
    try {
        if (row_.size() < region.width) row_.resize(region.width);
        const auto bottom = static_cast<std::uint32_t>(region.y) + region.height;
        for (auto y = static_cast<std::uint32_t>(region.y); y < bottom; ++y) {
            const auto r0 = QL32_PRESENT_NOW();
            rowProvider(y, row_);
            if (row_.size() != region.width)
                throw std::invalid_argument("PSRAM presenter row has the wrong width");
            const auto r1 = QL32_PRESENT_NOW();
            (void)(chunkedWrite_ ? writeMappedRowChunked : writeMappedRow)(*frame,
                static_cast<std::uint32_t>(region.x), y, row_.data(),
                region.width, indexMap_.data());
            providerUs += r1 - r0;
            writeUs += QL32_PRESENT_NOW() - r1;
            if (budget_.limit() != 0) throttle(region.width, throttleWaits, throttleUs);
        }
    } catch (...) {
        scanout_.cancelFrameUpdate(frame);
        throw;
    }
    const auto t2 = QL32_PRESENT_NOW();
    if (!scanout_.publishFrameUpdate(frame, damage)) {
        scanout_.cancelFrameUpdate(frame);
#if defined(QL32_OLIMEX_PRESENT_PROFILE)
        stats_.recordRejected();
#endif
        return false;
    }
#if defined(QL32_OLIMEX_PRESENT_PROFILE)
    const auto t3 = QL32_PRESENT_NOW();
    stats_.recordAccepted(width, height, region.width, region.height,
                          t1 - t0, t2 - t1, t3 - t2);
    stats_.providerUs += providerUs;
    stats_.writeUs += writeUs;
    stats_.throttleWaits += throttleWaits;
    stats_.throttleUs += throttleUs;
#endif
    return true;
}

#if defined(QL32_OLIMEX_PRESENT_PROFILE)
void PsramFramePresenter::reportStats() const {
    const auto& s = stats_;
    const unsigned avg = s.calls > s.rejectedCalls
        ? static_cast<unsigned>(s.totalUs / (s.calls - s.rejectedCalls)) : 0;
    Serial.printf("QL32 present stats: calls=%u, full=%u, partial=%u, rejected=%u, rows=%u, kpixels=%u, total-us=%u, begin-us=%u, rows-us=%u, publish-us=%u, provider-us=%u, write-us=%u, budget-bytes=%u, throttle-waits=%u, throttle-us=%u, avg-us=%u, max-us=%u, max-rows=%u, palette-changes=%u\n",
        static_cast<unsigned>(s.calls), static_cast<unsigned>(s.fullWindowCalls),
        static_cast<unsigned>(s.partialCalls), static_cast<unsigned>(s.rejectedCalls),
        static_cast<unsigned>(s.rows), static_cast<unsigned>(s.pixels / 1000),
        static_cast<unsigned>(s.totalUs), static_cast<unsigned>(s.beginUs),
        static_cast<unsigned>(s.rowsUs), static_cast<unsigned>(s.publishUs),
        static_cast<unsigned>(s.providerUs), static_cast<unsigned>(s.writeUs),
        static_cast<unsigned>(budget_.limit()), static_cast<unsigned>(s.throttleWaits),
        static_cast<unsigned>(s.throttleUs),
        avg, static_cast<unsigned>(s.maxUs), static_cast<unsigned>(s.maxRows),
        static_cast<unsigned>(s.paletteChanges));
}
#endif

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
