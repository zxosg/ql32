#include "ql32/platform/esp32_cyd/frame_presenter.hpp"

#include <stdexcept>
#include <utility>

namespace ql32::platform::esp32_cyd {

Ili9341FramePresenter::Ili9341FramePresenter(
    CydIli9341Display& display, std::function<void(graphics::Rect)> restoreOverlay,
    std::function<void()> rotationChanged)
    : display_(display), restoreOverlay_(std::move(restoreOverlay)),
      rotationChanged_(std::move(rotationChanged)) {}

void Ili9341FramePresenter::present(const graphics::Frame& frame) {
    if (frame.width != static_cast<std::uint32_t>(display_.width()) ||
        frame.height != static_cast<std::uint32_t>(display_.height()) ||
        frame.pixels.size() != static_cast<std::size_t>(frame.width) * frame.height)
        throw std::invalid_argument("composed frame does not match CYD display geometry");
    if (row_.size() < frame.width) row_.resize(frame.width);
    for (std::uint32_t y = 0; y < frame.height; ++y) {
        const auto offset = static_cast<std::size_t>(y) * frame.width;
        for (std::uint32_t x = 0; x < frame.width; ++x) {
            const auto& color = frame.palette[frame.pixels[offset + x]];
            row_[x] = static_cast<std::uint16_t>(
                ((color.red & 0xF8u) << 8u) |
                ((color.green & 0xFCu) << 3u) |
                (color.blue >> 3u));
        }
        display_.drawRGBBitmap(0, static_cast<std::int16_t>(y), row_.data(),
                               static_cast<std::int16_t>(frame.width), 1);
    }
    if (restoreOverlay_)
        restoreOverlay_({0, 0, frame.width, frame.height});
}

bool Ili9341FramePresenter::presentRows(
    std::uint32_t width, std::uint32_t height,
    const std::array<graphics::RgbColor, 256>& palette,
    const graphics::FramePresenter::RowProvider& rowProvider) {
    return presentRowsRegion(width, height, palette,
        {0, 0, width, height}, rowProvider);
}

bool Ili9341FramePresenter::presentRowsRegion(
    std::uint32_t width, std::uint32_t height,
    const std::array<graphics::RgbColor, 256>& palette,
    graphics::Rect region,
    const graphics::FramePresenter::RowProvider& rowProvider) {
    if (width != static_cast<std::uint32_t>(display_.width()) ||
        height != static_cast<std::uint32_t>(display_.height()))
        throw std::invalid_argument("composed rows do not match CYD display geometry");
    if (region.x < 0 || region.y < 0 ||
        static_cast<std::uint64_t>(region.x) + region.width > width ||
        static_cast<std::uint64_t>(region.y) + region.height > height)
        throw std::invalid_argument("composed region exceeds CYD display geometry");
    if (region.width == 0 || region.height == 0) return true;
    if (row_.size() < width) row_.resize(width);
    if (indexedRow_.size() < region.width) indexedRow_.resize(region.width);
    for (std::uint32_t y = static_cast<std::uint32_t>(region.y);
         y < static_cast<std::uint32_t>(region.y) + region.height; ++y) {
        rowProvider(y, indexedRow_);
        if (indexedRow_.size() != region.width)
            throw std::invalid_argument("composed row does not match CYD region width");
        for (std::uint32_t x = 0; x < region.width; ++x) {
            const auto& color = palette[indexedRow_[x]];
            row_[x] = static_cast<std::uint16_t>(
                ((color.red & 0xF8u) << 8u) |
                ((color.green & 0xFCu) << 3u) |
                (color.blue >> 3u));
        }
        display_.drawRGBBitmap(static_cast<std::int16_t>(region.x),
                               static_cast<std::int16_t>(y), row_.data(),
                               static_cast<std::int16_t>(region.width), 1);
    }
    if (restoreOverlay_) restoreOverlay_(region);
    return true;
}

graphics::WindowInfo Ili9341FramePresenter::setRotation(std::uint16_t degrees) {
    if (degrees != 0 && degrees != 90 && degrees != 180 && degrees != 270)
        throw std::invalid_argument("rotation must be 0, 90, 180, or 270 degrees");
    display_.setRotationDegrees(degrees);
    rotationDegrees_ = degrees;
    if (rotationChanged_) rotationChanged_();
    return {static_cast<std::uint32_t>(display_.width()),
            static_cast<std::uint32_t>(display_.height()), 16, 256, rotationDegrees_};
}

} // namespace ql32::platform::esp32_cyd
