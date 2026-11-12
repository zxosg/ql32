#include "ql32/platform/fabgl_vga/frame_presenter.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace ql32::platform::fabgl_vga {

namespace {

constexpr std::uint32_t kRowsPerBitmap = 8;

} // namespace

bool FabglFramePresenter::doubleBuffered() const noexcept {
    return display_.isDoubleBufferedEnabled();
}

bool FabglFramePresenter::presentDirect(
    std::uint32_t width, std::uint32_t height,
    const std::array<graphics::RgbColor, 256>& palette,
    const RowProvider& rowProvider) {
    if (width != static_cast<std::uint32_t>(canvas_.getWidth()) ||
        height == 0 || height > static_cast<std::uint32_t>(canvas_.getHeight()))
        throw std::invalid_argument("composed rows do not match VGA framebuffer geometry");
    if ((width & 3u) != 0)
        throw std::invalid_argument("FabGL VGA viewport width must be four-pixel aligned");

    bool paletteChanged = !rawPaletteCacheValid_;
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

    if (indexedRow_.size() < width) indexedRow_.resize(width);
    for (std::uint32_t y = 0; y < height; ++y) {
        rowProvider(y, indexedRow_);
        if (indexedRow_.size() != width)
            throw std::invalid_argument("composed row does not match VGA framebuffer width");
        auto* destination = display_.getScanline(static_cast<int>(y));
        if (!destination)
            throw std::runtime_error("FabGL VGA drawing framebuffer is unavailable");
        for (std::uint32_t x = 0; x < width; x += 4) {
            destination[x] = rawPalette_[indexedRow_[x + 2]];
            destination[x + 1] = rawPalette_[indexedRow_[x + 3]];
            destination[x + 2] = rawPalette_[indexedRow_[x]];
            destination[x + 3] = rawPalette_[indexedRow_[x + 1]];
        }
    }
    if (restoreOverlay_) restoreOverlay_({0, 0, width, height});
    canvas_.swapBuffers();
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
    if (doubleBuffered()) {
        if (width <= 400)
            return presentDirect(width, height, palette, rowProvider);
        if (!presentRowsRegion(width, height, palette, {0, 0, width, height},
                               rowProvider))
            return false;
        canvas_.swapBuffers();
        return true;
    }
    return presentRowsRegion(width, height, palette, {0, 0, width, height}, rowProvider);
}

bool FabglFramePresenter::presentRowsRegion(
    std::uint32_t width, std::uint32_t height,
    const std::array<graphics::RgbColor, 256>& palette, graphics::Rect region,
    const RowProvider& rowProvider) {
    if (doubleBuffered() &&
        (region.x != 0 || region.y != 0 || region.width != width ||
         region.height != height))
        return false;
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
