#pragma once

#include <cstdint>

namespace ql32::platform::olimex_vga {

struct ScanoutGeometry {
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t ringRows;
    std::uint32_t refillRows;

    constexpr bool valid() const noexcept {
        return width != 0 && width % 8 == 0 && height != 0 && ringRows != 0 &&
               refillRows != 0 && ringRows <= 200 &&
               height % ringRows == 0 && ringRows % refillRows == 0;
    }
};

// Defaults retain the qualified high-resolution ring sizes. The 320x240
// setting needs its own hardware timing qualification before exposure.
constexpr ScanoutGeometry scanoutGeometry(std::uint32_t width,
                                           std::uint32_t height) noexcept {
    if (width == 320 && height == 200) return {320, 200, 100, 50};
    if (width == 320 && height == 240) return {320, 240, 120, 60};
    if (width == 400 && height == 300) return {400, 300, 150, 75};
    if (width == 640 && height == 400) return {640, 400, 100, 50};
    if (width == 640 && height == 480) return {640, 480, 120, 60};
    return {};
}

} // namespace ql32::platform::olimex_vga
