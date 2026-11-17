#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace ql32::music {

class Ay38912 {
public:
    void reset(std::uint32_t clockHz, std::uint32_t sampleRate) noexcept;
    void writeRegisters(const std::array<std::uint8_t, 14>& registers) noexcept;
    std::int16_t renderSample() noexcept;

private:
    void restartEnvelope(std::uint8_t shape) noexcept;
    void updateSteps() noexcept;
    std::uint32_t clockHz_ = 1773400;
    std::uint32_t sampleRate_ = 22050;
    std::array<std::uint8_t, 14> registers_{};
    std::array<std::uint32_t, 3> tonePhase_{};
    std::array<std::uint32_t, 3> toneStep_{};
    std::uint32_t noiseStep_ = 0;
    std::uint32_t envelopeRateStep_ = 0;
    std::uint32_t noisePhase_ = 0;
    std::uint32_t envelopePhase_ = 0;
    std::uint32_t noiseLfsr_ = 0x1ffff;
    std::uint8_t envelopeStep_ = 0;
    std::uint8_t envelopeShape_ = 0;
    std::uint8_t envelopeLevel_ = 0;
    bool envelopeHolding_ = false;
};

} // namespace ql32::music
