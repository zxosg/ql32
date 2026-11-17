#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace ql32::platform::olimex_vga {

// The Olimex ESP32 I2S VGA path emits each group of eight pixels in this
// physical order. Arrange encoded row bytes so the displayed pixels run left
// to right. Any trailing partial group is left unchanged.
inline void applyDmaPixelOrder(std::uint8_t* row,
                               std::size_t pixelCount) noexcept {
    for (std::size_t base = 0; base + 8 <= pixelCount; base += 8) {
        const auto first = row[base];
        const auto second = row[base + 1];
        row[base] = row[base + 2];
        row[base + 1] = row[base + 3];
        row[base + 2] = first;
        row[base + 3] = second;

        const auto fifth = row[base + 4];
        const auto sixth = row[base + 5];
        row[base + 4] = row[base + 6];
        row[base + 5] = row[base + 7];
        row[base + 6] = fifth;
        row[base + 7] = sixth;
    }
}

// Convert a circularly shifted packed-4 row directly into the VGA byte order.
// pairOutputLut maps two palette indices (high nibble first) to two output
// bytes. Supported VGA widths are multiples of eight, while this also handles
// a trailing partial group for small host-side fixtures.
inline void encodePacked4ShiftedRow(const std::uint8_t* source,
                                    std::uint8_t* output,
                                    std::size_t pixelCount,
                                    std::size_t shift,
                                    const std::uint16_t* pairOutputLut) noexcept {
    if (!source || !output || !pairOutputLut || pixelCount == 0) return;
    shift %= pixelCount;

    const auto pair = [source, pixelCount, pairOutputLut](
        std::size_t base, std::size_t offset) noexcept {
        auto first = base + offset;
        if (first >= pixelCount) first -= pixelCount;
        auto second = first + 1;
        if (second >= pixelCount) second -= pixelCount;
        const auto firstIndex = static_cast<std::uint8_t>(
            (source[first / 2] >> ((first & 1u) ? 0 : 4)) & 0x0Fu);
        const auto secondPacked = source[second / 2];
        const auto secondIndex = static_cast<std::uint8_t>(
            (secondPacked >> ((second & 1u) ? 0 : 4)) & 0x0Fu);
        return pairOutputLut[(firstIndex << 4) | secondIndex];
    };

    std::size_t x = 0;
    for (; x + 8 <= pixelCount; x += 8) {
        auto base = shift + x;
        if (base >= pixelCount) base -= pixelCount;

        // The DMA stream displays logical pairs in order 1, 0, 3, 2 within
        // each eight-pixel block.
        const auto pair1 = pair(base, 2);
        const auto pair0 = pair(base, 0);
        const auto pair3 = pair(base, 6);
        const auto pair2 = pair(base, 4);
        std::memcpy(output + x, &pair1, sizeof(pair1));
        std::memcpy(output + x + 2, &pair0, sizeof(pair0));
        std::memcpy(output + x + 4, &pair3, sizeof(pair3));
        std::memcpy(output + x + 6, &pair2, sizeof(pair2));
    }
    for (; x < pixelCount; ++x) {
        auto index = shift + x;
        if (index >= pixelCount) index -= pixelCount;
        const auto packed = source[index / 2];
        const auto value = static_cast<std::uint8_t>(
            (index & 1u) ? (packed & 0x0Fu) : (packed >> 4));
        output[x] = static_cast<std::uint8_t>(
            pairOutputLut[(value << 4) | value] & 0xFFu);
    }
}

} // namespace ql32::platform::olimex_vga
