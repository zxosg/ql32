#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace ql32::music {

class Ay38912 {
public:
    void reset(std::uint32_t clockHz, std::uint32_t sampleRate) noexcept;
    // forceEnvelopeRestart: every register 13 write restarts the envelope (YM semantics).
    void writeRegisters(const std::array<std::uint8_t, 14>& registers,
                        bool forceEnvelopeRestart = false) noexcept;
    // Per-voice overrides used by YM special effects; level -1 / sample off = none.
    void setLevelOverride(unsigned voice, int level) noexcept { levelOverride_[voice] = static_cast<std::int8_t>(level); }
    void setDirectSample(unsigned voice, bool active, std::int16_t value = 0) noexcept {
        direct_[voice] = active; directValue_[voice] = value;
    }
    void restartEnvelopeNow() noexcept { restartEnvelope(registers_[13]); }
    // Sync-buzzer: restart the envelope with an explicit shape without changing register 13.
    void restartEnvelopeShape(std::uint8_t shape) noexcept { restartEnvelope(shape); }
    std::int16_t renderSample() noexcept;

private:
    void restartEnvelope(std::uint8_t shape) noexcept;
    void updateSteps() noexcept;
    std::int32_t renderTick() noexcept;
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
    std::int32_t dcLevel_ = 0;
    std::array<std::int8_t, 3> levelOverride_{{-1, -1, -1}};
    std::array<bool, 3> direct_{};
    std::array<std::int16_t, 3> directValue_{};
};

} // namespace ql32::music
