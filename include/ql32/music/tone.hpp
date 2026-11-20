#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace ql32::music {

enum class Waveform : std::uint8_t { Square, Sine, Triangle, SawtoothRising, SawtoothFalling };

// Platform-neutral tone generator for BEEP and test tones. A zero duration
// means "until stopped". Amplitude is the peak of the signed 16-bit output
// before the global volume is applied.
class ToneGenerator {
public:
    static constexpr std::int16_t DefaultAmplitude = 12000;

    ToneGenerator() = default;

    void start(std::uint32_t frequencyHz, std::uint64_t durationSamples, std::uint32_t sampleRate,
               Waveform waveform, std::int16_t amplitude = DefaultAmplitude) noexcept {
        step_ = sampleRate ? static_cast<std::uint32_t>(
            (static_cast<std::uint64_t>(frequencyHz) << 32) / sampleRate) : 0;
        waveform_ = waveform;
        amplitude_ = amplitude;
        remaining_ = durationSamples;
        unlimited_ = durationSamples == 0;
        phase_ = 0;
        active_ = step_ != 0;
        if (waveform == Waveform::Sine && !tableReady_) buildSineTable();
    }

    void stop() noexcept { active_ = false; }
    bool active() const noexcept { return active_; }

    // Adds the tone to the samples already in the buffer, saturating at 16 bits.
    void mixInto(std::int16_t* samples, std::size_t count) noexcept {
        for (std::size_t i = 0; i < count && active_; ++i) {
            const std::int32_t mixed = static_cast<std::int32_t>(samples[i]) + next();
            samples[i] = static_cast<std::int16_t>(
                mixed > 32767 ? 32767 : (mixed < -32768 ? -32768 : mixed));
            if (!unlimited_ && --remaining_ == 0) active_ = false;
        }
    }

private:
    std::int32_t next() noexcept {
        const std::uint32_t phase = phase_;
        phase_ += step_;
        const std::int32_t a = amplitude_;
        switch (waveform_) {
        case Waveform::Square:
            return (phase & 0x80000000u) ? -a : a;
        case Waveform::Sine:
            return (static_cast<std::int32_t>(table_[phase >> 24]) * a) / 32767;
        case Waveform::Triangle: {
            // 0..2^32 -> -a..a..-a, using the top 16 bits of the phase
            const std::int32_t p = static_cast<std::int32_t>(phase >> 16);
            const std::int32_t folded = p < 32768 ? p : 65535 - p;
            return ((folded * 2 - 32767) * a) / 32767;
        }
        case Waveform::SawtoothRising:
            return ((static_cast<std::int32_t>(phase >> 16) * 2 - 65535) * a) / 65535;
        case Waveform::SawtoothFalling:
            return ((65535 - static_cast<std::int32_t>(phase >> 16) * 2) * a) / 65535;
        }
        return 0;
    }

    void buildSineTable() noexcept {
        for (std::size_t i = 0; i < table_.size(); ++i)
            table_[i] = static_cast<std::int16_t>(std::lround(
                32767.0 * std::sin(6.283185307179586 * static_cast<double>(i) /
                                      static_cast<double>(table_.size()))));
        tableReady_ = true;
    }

    std::uint32_t step_ = 0;
    std::uint32_t phase_ = 0;
    std::uint64_t remaining_ = 0;
    std::int16_t amplitude_ = DefaultAmplitude;
    Waveform waveform_ = Waveform::Square;
    bool active_ = false;
    bool unlimited_ = false;
    bool tableReady_ = false;
    std::array<std::int16_t, 256> table_{};
};

// Global output volume, 0..255, applied to every PCM sample before the DAC.
constexpr std::int16_t applyVolume(std::int16_t sample, std::uint8_t volume) noexcept {
    return static_cast<std::int16_t>((static_cast<std::int32_t>(sample) * volume) / 255);
}

constexpr std::uint8_t DefaultVolume = 128;

} // namespace ql32::music
