#include "ql32/music/ay38912.hpp"

#include <algorithm>
#include <array>

namespace ql32::music {
namespace {
constexpr std::array<std::int16_t, 16> Amplitude{{
    0, 836, 1212, 1773, 2619, 3875, 5397, 7796,
    11355, 16616, 23156, 27755, 31132, 32767, 32767, 32767
}};
std::uint32_t phaseStep(std::uint32_t clock, std::uint32_t sampleRate,
                        std::uint32_t divider, std::uint32_t period) noexcept {
    if (period == 0) period = 1;
    const std::uint64_t denominator = static_cast<std::uint64_t>(sampleRate) *
                                     divider * period;
    if (denominator == 0) return 0;
    return static_cast<std::uint32_t>((static_cast<std::uint64_t>(clock) << 32) /
                                      denominator);
}
} // namespace

void Ay38912::reset(std::uint32_t clockHz, std::uint32_t sampleRate) noexcept {
    clockHz_ = clockHz ? clockHz : 1773400;
    sampleRate_ = sampleRate ? sampleRate : 22050;
    registers_.fill(0);
    levelOverride_.fill(-1);
    direct_.fill(false);
    tonePhase_.fill(0);
    toneStep_.fill(0);
    noisePhase_ = 0;
    envelopePhase_ = 0;
    noiseLfsr_ = 0x1ffff;
    envelopeStep_ = 0;
    envelopeShape_ = 0;
    envelopeLevel_ = 15;
    envelopeHolding_ = false;
    updateSteps();
}

void Ay38912::updateSteps() noexcept {
    for (unsigned channel = 0; channel < 3; ++channel) {
        const std::uint32_t period = std::max<std::uint32_t>(
            registers_[channel * 2] |
            ((registers_[channel * 2 + 1] & 0x0f) << 8), 1);
        toneStep_[channel] = phaseStep(clockHz_, sampleRate_, 16, period);
    }
    const std::uint32_t noisePeriod = std::max<std::uint32_t>(registers_[6] & 0x1f, 1);
    noiseStep_ = phaseStep(clockHz_, sampleRate_, 16, noisePeriod);
    const std::uint32_t envelopePeriod =
        std::max<std::uint32_t>(registers_[11] | (registers_[12] << 8), 1);
    envelopeRateStep_ = phaseStep(clockHz_, sampleRate_, 256, envelopePeriod);
}

void Ay38912::restartEnvelope(std::uint8_t shape) noexcept {
    envelopeShape_ = shape & 0x0f;
    envelopeStep_ = 0;
    const bool attack = (envelopeShape_ & 0x04) != 0;
    envelopeLevel_ = attack ? 0 : 15;
    envelopeHolding_ = false;
    envelopePhase_ = 0;
}

void Ay38912::writeRegisters(const std::array<std::uint8_t, 14>& registers,
                             bool forceEnvelopeRestart) noexcept {
    if (forceEnvelopeRestart || (registers[13] & 0x0f) != (registers_[13] & 0x0f))
        restartEnvelope(registers[13]);
    registers_ = registers;
    updateSteps();
}

std::int16_t Ay38912::renderSample() noexcept {
    noisePhase_ += noiseStep_;
    if ((noisePhase_ & 0x80000000u) != 0) {
        noisePhase_ &= 0x7fffffffu;
        const std::uint32_t feedback = (noiseLfsr_ ^ (noiseLfsr_ >> 3)) & 1u;
        noiseLfsr_ = (noiseLfsr_ >> 1) | (feedback << 16);
    }

    envelopePhase_ += envelopeRateStep_;
    if (!envelopeHolding_ && (envelopePhase_ & 0x80000000u) != 0) {
        envelopePhase_ &= 0x7fffffffu;
        if (envelopeStep_ < 15) {
            ++envelopeStep_;
            const bool attack = (envelopeShape_ & 0x04) != 0;
            envelopeLevel_ = attack ? envelopeStep_ : 15 - envelopeStep_;
        } else if ((envelopeShape_ & 0x08) == 0) {
            envelopeLevel_ = 0;
            envelopeHolding_ = true;
        } else if ((envelopeShape_ & 0x01) != 0) {
            if ((envelopeShape_ & 0x02) != 0) envelopeLevel_ = 15 - envelopeLevel_;
            envelopeHolding_ = true;
        } else {
            if ((envelopeShape_ & 0x02) != 0) envelopeShape_ ^= 0x04;
            envelopeStep_ = 0;
        }
    }

    std::int32_t mixed = 0;
    const std::uint8_t mixer = registers_[7];
    for (unsigned channel = 0; channel < 3; ++channel) {
        tonePhase_[channel] += toneStep_[channel];
        if (direct_[channel]) {
            mixed += directValue_[channel];
            continue;
        }
        const bool tone = (mixer & (1u << channel)) != 0 ||
                          (tonePhase_[channel] & 0x80000000u) == 0;
        const bool noise = (mixer & (1u << (channel + 3))) != 0 ||
                           (noiseLfsr_ & 1u) != 0;
        const std::uint8_t volume = registers_[8 + channel];
        const std::uint8_t level = levelOverride_[channel] >= 0
            ? static_cast<std::uint8_t>(levelOverride_[channel] & 0x0f)
            : ((volume & 0x10) != 0 ? envelopeLevel_ : volume & 0x0f);
        const std::int32_t amplitude = Amplitude[level];
        if (amplitude != 0)
            mixed += (tone && noise ? amplitude : 0) - amplitude / 2;
    }
    return static_cast<std::int16_t>(mixed / 3);
}

} // namespace ql32::music
