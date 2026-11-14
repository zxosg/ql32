#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace ql32::platform::olimex_vga {

// The Olimex ESP32 I2S VGA path emits each group of eight pixels in this
// physical order. Arrange encoded row bytes so the displayed pixels run left
// to right. Any trailing partial group is left unchanged.
inline void applyDmaPixelOrder(std::uint8_t* row,
                               std::size_t pixelCount) noexcept {
    constexpr std::array<std::uint8_t, 8> order{{2, 3, 0, 1, 6, 7, 4, 5}};
    for (std::size_t base = 0; base + order.size() <= pixelCount;
         base += order.size()) {
        std::array<std::uint8_t, 8> source{};
        for (std::size_t pixel = 0; pixel < order.size(); ++pixel)
            source[pixel] = row[base + pixel];
        for (std::size_t pixel = 0; pixel < order.size(); ++pixel)
            row[base + pixel] = source[order[pixel]];
    }
}

} // namespace ql32::platform::olimex_vga
