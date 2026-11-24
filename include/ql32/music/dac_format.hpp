#pragma once

#include <cstddef>
#include <cstdint>

namespace ql32::music {

// ESP32 built-in 8-bit DAC reads the upper byte of each 16-bit I2S slot as an
// unsigned level, so signed PCM is offset to mid-scale and truncated to 8 bits.
constexpr std::uint16_t pcmToDac16(std::int16_t sample) noexcept {
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>((static_cast<std::int32_t>(sample) + 32768) >> 8) << 8);
}

} // namespace ql32::music
