#pragma once

#include "ql32/graphics/service.hpp"

#include <algorithm>
#include <cstdint>

namespace ql32::platform::esp32_cyd {

inline graphics::Point mapRawTouchPoint(std::uint16_t rawX, std::uint16_t rawY,
                                        std::uint32_t width,
                                        std::uint32_t height,
                                        std::uint16_t rotationDegrees) {
    if (width == 0 || height == 0) return {};
    constexpr int RawMinimum = 250;
    constexpr int RawMaximum = 3850;
    constexpr std::uint32_t NativeWidth = 320;
    constexpr std::uint32_t NativeHeight = 240;
    const auto scale = [=](std::uint16_t value, std::uint32_t extent) {
        const auto bounded = std::clamp<int>(value, RawMinimum, RawMaximum);
        return static_cast<std::int32_t>(
            static_cast<std::int64_t>(bounded - RawMinimum) * (extent - 1) /
            (RawMaximum - RawMinimum));
    };

    // Corner measurements on this CYD show that X already follows the
    // horizontal screen axis. Y follows the vertical axis but is inverted.
    const std::int32_t nativeX = scale(rawX, NativeWidth);
    const std::int32_t nativeY = static_cast<std::int32_t>(NativeHeight - 1) -
                                 scale(rawY, NativeHeight);
    graphics::Point point{};
    switch (rotationDegrees % 360) {
    case 90:
        point = {static_cast<std::int32_t>(NativeHeight - 1) - nativeY, nativeX};
        break;
    case 180:
        point = {static_cast<std::int32_t>(NativeWidth - 1) - nativeX,
                 static_cast<std::int32_t>(NativeHeight - 1) - nativeY};
        break;
    case 270:
        point = {nativeY, static_cast<std::int32_t>(NativeWidth - 1) - nativeX};
        break;
    default:
        point = {nativeX, nativeY};
        break;
    }
    point.x = std::clamp<std::int32_t>(point.x, 0,
                                      static_cast<std::int32_t>(width - 1));
    point.y = std::clamp<std::int32_t>(point.y, 0,
                                      static_cast<std::int32_t>(height - 1));
    return point;
}

} // namespace ql32::platform::esp32_cyd
