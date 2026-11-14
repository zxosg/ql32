#pragma once

#include "ql32/graphics/service.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace ql32::platform::olimex_vga {

// Olimex's three VGA outputs can select black/blue/red/magenta/green/cyan/
// yellow/white. The palette index bits are B,R,G (bits 0,1,2), while the
// stored RGB222 pixel uses R,G,B channel pairs from most to least significant.
inline constexpr std::array<graphics::RgbColor, 8> kOlimexHardwareColors{{
    {0, 0, 0}, {0, 0, 255}, {255, 0, 0}, {255, 0, 255},
    {0, 255, 0}, {0, 255, 255}, {255, 255, 0}, {255, 255, 255}
}};

constexpr std::uint32_t colorDistance(const graphics::RgbColor& left,
                                      const graphics::RgbColor& right) noexcept {
    const auto red = static_cast<std::int32_t>(left.red) - right.red;
    const auto green = static_cast<std::int32_t>(left.green) - right.green;
    const auto blue = static_cast<std::int32_t>(left.blue) - right.blue;
    return static_cast<std::uint32_t>(red * red + green * green + blue * blue);
}

constexpr std::uint8_t colorToRgb222(const graphics::RgbColor& color) noexcept {
    std::size_t nearest = 0;
    auto distance = std::numeric_limits<std::uint32_t>::max();
    for (std::size_t index = 0; index < kOlimexHardwareColors.size(); ++index) {
        const auto candidate = colorDistance(color, kOlimexHardwareColors[index]);
        if (candidate < distance) {
            distance = candidate;
            nearest = index;
        }
    }
    const auto paletteIndex = static_cast<std::uint8_t>(nearest);
    const auto red = (paletteIndex & 0x02u) ? 0x30u : 0u;
    const auto green = (paletteIndex & 0x04u) ? 0x0Cu : 0u;
    const auto blue = (paletteIndex & 0x01u) ? 0x03u : 0u;
    return static_cast<std::uint8_t>(red | green | blue);
}

} // namespace ql32::platform::olimex_vga
