#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace ql32::platform::olimex_vga {

// Message drawn in the scanout line ring while internal flash is written and
// the refill task cannot run. The ring is half a frame, so it appears twice.
inline constexpr char kHoldMessage[] = "Busy, accessing disk";
inline constexpr std::uint32_t kHoldGlyphColumns = 5;
inline constexpr std::uint32_t kHoldGlyphRows = 8;
inline constexpr std::uint32_t kHoldGlyphAdvance = 6;

struct HoldGlyph {
    char character;
    std::uint8_t rows[kHoldGlyphRows];  // bit 4 is the left column
};

inline constexpr HoldGlyph kHoldGlyphs[] = {
    {'B', {0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e, 0x00}},
    {'u', {0x00, 0x00, 0x11, 0x11, 0x11, 0x13, 0x0d, 0x00}},
    {'s', {0x00, 0x00, 0x0f, 0x10, 0x0e, 0x01, 0x1e, 0x00}},
    {'y', {0x00, 0x00, 0x11, 0x11, 0x11, 0x0f, 0x01, 0x0e}},
    {',', {0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x04, 0x08}},
    {'a', {0x00, 0x00, 0x0e, 0x01, 0x0f, 0x11, 0x0f, 0x00}},
    {'c', {0x00, 0x00, 0x0e, 0x10, 0x10, 0x11, 0x0e, 0x00}},
    {'e', {0x00, 0x00, 0x0e, 0x11, 0x1f, 0x10, 0x0e, 0x00}},
    {'i', {0x04, 0x00, 0x0c, 0x04, 0x04, 0x04, 0x0e, 0x00}},
    {'n', {0x00, 0x00, 0x16, 0x19, 0x11, 0x11, 0x11, 0x00}},
    {'g', {0x00, 0x00, 0x0f, 0x11, 0x11, 0x0f, 0x01, 0x0e}},
    {'d', {0x01, 0x01, 0x0d, 0x13, 0x11, 0x13, 0x0d, 0x00}},
    {'k', {0x10, 0x10, 0x12, 0x14, 0x18, 0x14, 0x12, 0x00}},
};

constexpr std::uint32_t holdMessageLength() noexcept {
    return static_cast<std::uint32_t>(sizeof(kHoldMessage) - 1);
}

constexpr std::uint32_t holdMessageScale(std::uint32_t width) noexcept {
    const auto natural = holdMessageLength() * kHoldGlyphAdvance;
    const auto scale = (width * 4 / 5) / natural;
    return scale < 1 ? 1 : scale;
}

// Fills mask[0..width) with 1 for message pixels of ring row `row` (0 for the
// background). The message is centred in the ring; rows outside it are blank.
inline void fillHoldMaskRow(std::uint8_t* mask, std::uint32_t width,
                            std::uint32_t ringRows, std::uint32_t row) noexcept {
    std::memset(mask, 0, width);
    const auto scale = holdMessageScale(width);
    const auto textHeight = kHoldGlyphRows * scale;
    const auto textWidth = holdMessageLength() * kHoldGlyphAdvance * scale;
    if (ringRows < textHeight || width < textWidth) return;
    const auto top = (ringRows - textHeight) / 2;
    if (row < top || row >= top + textHeight) return;
    const auto glyphRow = (row - top) / scale;
    const auto left = (width - textWidth) / 2;
    for (std::uint32_t index = 0; index < holdMessageLength(); ++index) {
        const HoldGlyph* glyph = nullptr;
        for (const auto& candidate : kHoldGlyphs)
            if (candidate.character == kHoldMessage[index]) glyph = &candidate;
        if (!glyph) continue;
        const auto bits = glyph->rows[glyphRow];
        for (std::uint32_t column = 0; column < kHoldGlyphColumns; ++column) {
            if (!(bits & (1u << (kHoldGlyphColumns - 1 - column)))) continue;
            const auto x = left + (index * kHoldGlyphAdvance + column) * scale;
            for (std::uint32_t dx = 0; dx < scale; ++dx) mask[x + dx] = 1;
        }
    }
}

} // namespace ql32::platform::olimex_vga
