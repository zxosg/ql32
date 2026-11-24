#include "ql32/platform/fabgl_vga/frame_presenter.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#if defined(QL32_GRAPHICS_PROFILE)
#include <Arduino.h>
#endif

namespace ql32::platform::fabgl_vga {

namespace {

constexpr std::uint32_t kRowsPerBitmap = 8;

} // namespace

bool FabglFramePresenter::doubleBuffered() const noexcept {
    return display_.isDoubleBufferedEnabled();
}

void FabglFramePresenter::swapBuffers() {
#if defined(QL32_GRAPHICS_PROFILE)
    ++profile_.swapRequested;
    // Canvas::swapBuffers queues exactly one SwapBuffers primitive and waits
    // for the executor's task notification. The local FabGL source confirms
    // this call path; the post-return count is therefore an executed swap.
    ++profile_.swapQueued;
    const auto swapStart = micros();
#endif
    canvas_.swapBuffers();
#if defined(QL32_GRAPHICS_PROFILE)
    const auto duration = static_cast<std::uint32_t>(micros() - swapStart);
    profile_.swapMicros += duration;
    if (profile_.swapCalls == 0 || duration < profile_.swapMinMicros)
        profile_.swapMinMicros = duration;
    if (duration > profile_.swapMaxMicros)
        profile_.swapMaxMicros = duration;
    ++profile_.swapCalls;
    ++profile_.swapExecuted;
    if (profile_.swapSampleCount < PresentationProfile::kSampleCapacity)
        profile_.swapSamples[profile_.swapSampleCount++] = duration;
#endif
}

#if defined(QL32_GRAPHICS_PROFILE)
void FabglFramePresenter::finalizePresentationProfile() noexcept {
    const auto summarize = [](auto& samples, std::size_t count,
                              std::uint32_t& p50, std::uint32_t& p95,
                              std::uint32_t& p99) {
        if (count == 0) return;
        std::sort(samples.begin(), samples.begin() + count);
        const auto atPercentile = [count](std::size_t percentile) {
            return ((count - 1) * percentile) / 100;
        };
        p50 = samples[atPercentile(50)];
        p95 = samples[atPercentile(95)];
        p99 = samples[atPercentile(99)];
    };
    summarize(profile_.swapSamples, profile_.swapSampleCount,
              profile_.swapP50Micros, profile_.swapP95Micros,
              profile_.swapP99Micros);
    summarize(profile_.updateSamples, profile_.updateSampleCount,
              profile_.updateP50Micros, profile_.updateP95Micros,
              profile_.updateP99Micros);
}
#endif

bool FabglFramePresenter::presentDirect(
    std::uint32_t width, std::uint32_t height,
    const std::array<graphics::RgbColor, 256>& palette,
    const RowProvider& rowProvider) {
    return presentDirectRegion(width, height, palette,
                               {0, 0, width, height}, rowProvider);
}

bool FabglFramePresenter::presentDirectRegion(
    std::uint32_t width, std::uint32_t height,
    const std::array<graphics::RgbColor, 256>& palette,
    graphics::Rect region, const RowProvider& rowProvider) {
    if (width != static_cast<std::uint32_t>(canvas_.getWidth()) ||
        height == 0 || height > static_cast<std::uint32_t>(canvas_.getHeight()))
        throw std::invalid_argument("composed rows do not match VGA framebuffer geometry");
    if ((width & 3u) != 0)
        throw std::invalid_argument("FabGL VGA viewport width must be four-pixel aligned");
    if (region.x < 0 || region.y < 0 ||
        static_cast<std::uint64_t>(region.x) + region.width > width ||
        static_cast<std::uint64_t>(region.y) + region.height > height)
        throw std::invalid_argument("composed region exceeds VGA framebuffer geometry");
    if (region.width == 0 || region.height == 0) return true;
    const bool useDoubleBuffer = doubleBuffered();
    const bool fullRegion = region.x == 0 && region.y == 0 &&
        region.width == width && region.height == height;
    if (useDoubleBuffer && !fullRegion && !doubleBuffersSynchronized_) {
        synchronizeOnNextFullPresentation_ = true;
#if defined(QL32_GRAPHICS_PROFILE)
        ++profile_.swapRequested;
        ++profile_.swapSkipped;
#endif
        return false;
    }

#if defined(QL32_GRAPHICS_PROFILE)
    const auto updateStart = micros();
    const auto recordUpdate = [&]() {
        const auto duration = static_cast<std::uint32_t>(micros() - updateStart);
        profile_.updateMicros += duration;
        if (profile_.updateSampleCount < PresentationProfile::kSampleCapacity)
            profile_.updateSamples[profile_.updateSampleCount++] = duration;
    };
#endif

    bool paletteChanged = !rawPaletteCacheValid_;
#if defined(QL32_GRAPHICS_PROFILE)
    const auto paletteStart = micros();
#endif
    for (std::size_t index = 0; !paletteChanged && index < palette.size(); ++index) {
        const auto& oldColor = cachedPalette_[index];
        const auto& newColor = palette[index];
        paletteChanged = oldColor.red != newColor.red ||
                         oldColor.green != newColor.green ||
                         oldColor.blue != newColor.blue;
    }
    if (paletteChanged) {
        for (std::size_t index = 0; index < palette.size(); ++index) {
            const auto& color = palette[index];
            const fabgl::RGB888 rgb(color.red, color.green, color.blue);
            rawPalette_[index] = display_.createRawPixel(fabgl::RGB222(rgb));
        }
        cachedPalette_ = palette;
        rawPaletteCacheValid_ = true;
    }
#if defined(QL32_GRAPHICS_PROFILE)
    profile_.convertMicros += static_cast<std::uint32_t>(micros() - paletteStart);
#endif

    if (indexedRow_.size() < region.width) indexedRow_.resize(region.width);
    constexpr std::size_t kMaxStagedRegionBytes = 16 * 1024;
    const auto stagedBytes = static_cast<std::uint64_t>(region.width) * region.height;
    bool stageRegion = useDoubleBuffer && !fullRegion &&
        stagedBytes <= kMaxStagedRegionBytes;
    const bool stageContiguousRegion = stageRegion &&
        (region.x & 3) == 0 && (region.width & 3u) == 0;
    if (stageRegion) {
        try {
            rawRegion_.resize(static_cast<std::size_t>(stagedBytes));
        } catch (const std::bad_alloc&) {
            stageRegion = false;
        }
    }
    const auto composeRow = [&](std::uint32_t y) {
#if defined(QL32_GRAPHICS_PROFILE)
        const auto start = micros();
#endif
        rowProvider(y, indexedRow_);
#if defined(QL32_GRAPHICS_PROFILE)
        profile_.composeMicros += static_cast<std::uint32_t>(micros() - start);
        ++profile_.rowsComposed;
        profile_.pixelsComposed += indexedRow_.size();
#endif
    };
    const auto prepareStagedRegion = [&]() {
        const auto bottom = static_cast<std::uint32_t>(region.y) + region.height;
        for (auto y = static_cast<std::uint32_t>(region.y); y < bottom; ++y) {
            composeRow(y);
            if (indexedRow_.size() != region.width)
                throw std::invalid_argument("composed row does not match VGA region width");
            const auto rowOffset = static_cast<std::size_t>(y - region.y) * region.width;
#if defined(QL32_GRAPHICS_PROFILE)
            const auto convertStart = micros();
#endif
            for (std::uint32_t x = 0; x < region.width; ++x) {
                if (stageContiguousRegion) {
                    const auto screenX = static_cast<std::uint32_t>(region.x) + x;
                    const auto scanlineOffset = (screenX & ~3u) | ((screenX + 2u) & 3u);
                    rawRegion_[rowOffset + scanlineOffset - region.x] =
                        rawPalette_[indexedRow_[x]];
                } else {
                    rawRegion_[rowOffset + x] = rawPalette_[indexedRow_[x]];
                }
            }
#if defined(QL32_GRAPHICS_PROFILE)
            profile_.convertMicros += static_cast<std::uint32_t>(micros() - convertStart);
#endif
        }
    };
    const auto writeRegion = [&]() {
        const auto bottom = static_cast<std::uint32_t>(region.y) + region.height;
        for (auto y = static_cast<std::uint32_t>(region.y); y < bottom; ++y) {
            auto* destination = display_.getScanline(static_cast<int>(y));
            if (!destination)
                throw std::runtime_error("FabGL VGA drawing framebuffer is unavailable");
            if (!stageRegion) {
                composeRow(y);
                if (indexedRow_.size() != region.width)
                    throw std::invalid_argument("composed row does not match VGA region width");
            }
            const auto rowOffset = static_cast<std::size_t>(y - region.y) * region.width;
#if defined(QL32_GRAPHICS_PROFILE)
            const auto writeStart = micros();
#endif
            if (stageContiguousRegion) {
                std::memcpy(destination + region.x, rawRegion_.data() + rowOffset,
                            region.width);
#if defined(QL32_GRAPHICS_PROFILE)
                profile_.writeMicros += static_cast<std::uint32_t>(micros() - writeStart);
                profile_.pixelsWritten += region.width;
#endif
                continue;
            }
            for (std::uint32_t x = 0; x < region.width; ++x) {
                const auto screenX = static_cast<std::uint32_t>(region.x) + x;
                const auto scanlineOffset = (screenX & ~3u) | ((screenX + 2u) & 3u);
                destination[scanlineOffset] = stageRegion
                    ? rawRegion_[rowOffset + x]
                    : rawPalette_[indexedRow_[x]];
            }
#if defined(QL32_GRAPHICS_PROFILE)
            profile_.writeMicros += static_cast<std::uint32_t>(micros() - writeStart);
            profile_.pixelsWritten += region.width;
#endif
        }
    };

    try {
        if (stageRegion) prepareStagedRegion();
        writeRegion();
        if (restoreOverlay_) restoreOverlay_(region);
        if (useDoubleBuffer) {
            swapBuffers();
            const bool syncBuffers = fullRegion
                ? synchronizeOnNextFullPresentation_
                : doubleBuffersSynchronized_;
            if (syncBuffers) {
                // Apply the region to the newly hidden buffer as well. For a
                // full frame this synchronizes both buffers after a partial
                // update requested recovery; for a partial frame it keeps the
                // buffers synchronized for subsequent partial updates.
                writeRegion();
                if (restoreOverlay_) restoreOverlay_(region);
                doubleBuffersSynchronized_ = true;
                synchronizeOnNextFullPresentation_ = false;
            } else {
                // A full frame is already the next complete image. Avoid a
                // second full pass until a later partial update needs sync.
                doubleBuffersSynchronized_ = false;
            }
        }
#if defined(QL32_GRAPHICS_PROFILE)
        if (fullRegion) ++profile_.fullFrames;
        else ++profile_.partialFrames;
        recordUpdate();
#endif
    } catch (...) {
#if defined(QL32_GRAPHICS_PROFILE)
        recordUpdate();
#endif
        if (useDoubleBuffer) doubleBuffersSynchronized_ = false;
        throw;
    }
    return true;
}

void FabglFramePresenter::present(const graphics::Frame& frame) {
    if (frame.width != static_cast<std::uint32_t>(canvas_.getWidth()) ||
        frame.height == 0 ||
        frame.height > static_cast<std::uint32_t>(canvas_.getHeight()) ||
        frame.pixels.size() != static_cast<std::size_t>(frame.width) * frame.height)
        throw std::invalid_argument("composed frame does not match VGA canvas geometry");

    (void)presentRows(frame.width, frame.height, frame.palette,
        [&frame](std::uint32_t y, std::vector<graphics::ColorIndex>& row) {
            row.resize(frame.width);
            const auto offset = static_cast<std::size_t>(y) * frame.width;
            std::copy_n(frame.pixels.data() + offset, frame.width, row.data());
        });
}

bool FabglFramePresenter::presentRows(
    std::uint32_t width, std::uint32_t height,
    const std::array<graphics::RgbColor, 256>& palette,
    const RowProvider& rowProvider) {
#if defined(QL32_GRAPHICS_PROFILE)
    ++profile_.regionRequests;
#endif
    if (doubleBuffered()) {
        if (width <= 400)
            return presentDirect(width, height, palette, rowProvider);
        if (!presentRowsRegion(width, height, palette, {0, 0, width, height},
                               rowProvider))
            return false;
        swapBuffers();
        return true;
    }
    return presentRowsRegion(width, height, palette, {0, 0, width, height}, rowProvider);
}

bool FabglFramePresenter::presentRowsRegion(
    std::uint32_t width, std::uint32_t height,
    const std::array<graphics::RgbColor, 256>& palette, graphics::Rect region,
    const RowProvider& rowProvider) {
#if defined(QL32_GRAPHICS_PROFILE)
    ++profile_.regionRequests;
#endif
    if (doubleBuffered() && width <= 400) {
        if (region.x < 0 || region.y < 0 ||
            static_cast<std::uint64_t>(region.x) + region.width > width ||
            static_cast<std::uint64_t>(region.y) + region.height > height)
            throw std::invalid_argument("composed region exceeds VGA canvas geometry");
        const auto regionPixels = static_cast<std::uint64_t>(region.width) *
                                  region.height;
        const auto framePixels = static_cast<std::uint64_t>(width) * height;
        // A synchronized partial update writes the region to both buffers.
        // For large dirty areas, a single full-frame present does less pixel
        // traffic; the next partial request will resynchronize as needed.
        if (regionPixels * 2 > framePixels) {
#if defined(QL32_GRAPHICS_PROFILE)
            ++profile_.fullFallbacks;
#endif
            return false;
        }
        const auto accepted = presentDirectRegion(width, height, palette,
                                                  region, rowProvider);
#if defined(QL32_GRAPHICS_PROFILE)
        if (!accepted) ++profile_.fullFallbacks;
#endif
        return accepted;
    }
    if (doubleBuffered() &&
        (region.x != 0 || region.y != 0 || region.width != width ||
         region.height != height)) {
#if defined(QL32_GRAPHICS_PROFILE)
        ++profile_.fullFallbacks;
        ++profile_.swapRequested;
        ++profile_.swapSkipped;
#endif
        return false;
    }
    if (width != static_cast<std::uint32_t>(canvas_.getWidth()) || height == 0 ||
        height > static_cast<std::uint32_t>(canvas_.getHeight()))
        throw std::invalid_argument("composed rows do not match VGA canvas geometry");
    if (region.x < 0 || region.y < 0 ||
        static_cast<std::uint64_t>(region.x) + region.width > width ||
        static_cast<std::uint64_t>(region.y) + region.height > height)
        throw std::invalid_argument("composed region exceeds VGA canvas geometry");
    if (region.width == 0 || region.height == 0) return true;

    if (region.width > static_cast<std::uint32_t>(std::numeric_limits<std::int16_t>::max()) ||
        region.height > static_cast<std::uint32_t>(std::numeric_limits<std::int16_t>::max()))
        throw std::invalid_argument("composed VGA bitmap exceeds FabGL dimensions");

    if (indexedRow_.size() < region.width) indexedRow_.resize(region.width);
    bool paletteChanged = !paletteCacheValid_;
    for (std::size_t index = 0; !paletteChanged && index < palette.size(); ++index) {
        const auto& incoming = palette[index];
        const auto& cached = cachedPalette_[index];
        paletteChanged = incoming.red != cached.red || incoming.green != cached.green ||
                         incoming.blue != cached.blue;
    }
    if (paletteChanged) {
        for (std::size_t index = 0; index < palette.size(); ++index) {
            const auto& color = palette[index];
            rgbaPalette_[index] = fabgl::RGBA8888(
                color.red, color.green, color.blue, 255);
        }
        cachedPalette_ = palette;
        paletteCacheValid_ = true;
    }
    canvas_.beginUpdate();
    try {
        const auto regionBottom = static_cast<std::uint32_t>(region.y) + region.height;
        for (std::uint32_t bandTop = static_cast<std::uint32_t>(region.y);
             bandTop < regionBottom;) {
            const auto bandHeight = std::min(kRowsPerBitmap, regionBottom - bandTop);
            rgbaBand_.resize(static_cast<std::size_t>(region.width) * bandHeight);
            for (std::uint32_t bandRow = 0; bandRow < bandHeight; ++bandRow) {
                rowProvider(bandTop + bandRow, indexedRow_);
                if (indexedRow_.size() != region.width)
                    throw std::invalid_argument("composed row does not match VGA region width");
                auto* destination = rgbaBand_.data() +
                    static_cast<std::size_t>(bandRow) * region.width;
                for (std::uint32_t x = 0; x < region.width; ++x)
                    destination[x] = rgbaPalette_[indexedRow_[x]];
            }

            // One FabGL bitmap command updates the band; drain it before the
            // scratch pixels or Bitmap wrapper are reused or destroyed.
            fabgl::Bitmap bitmap(static_cast<int>(region.width),
                                 static_cast<int>(bandHeight),
                                 rgbaBand_.data(), fabgl::PixelFormat::RGBA8888);
            canvas_.drawBitmap(region.x, static_cast<int>(bandTop), &bitmap);
            canvas_.waitCompletion(false);
            bandTop += bandHeight;
        }
    } catch (...) {
        canvas_.endUpdate();
        throw;
    }
    canvas_.endUpdate();
    canvas_.waitCompletion(true);
    if (restoreOverlay_) restoreOverlay_(region);
    return true;
}

} // namespace ql32::platform::fabgl_vga
