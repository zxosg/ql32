#pragma once

#include "ql32/music/tone.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace ql32::music {

// A mono 16-bit PCM producer at the mixer sample rate.
class AudioSource {
public:
    virtual ~AudioSource() = default;
    // Writes exactly `count` samples (silence after the end of the stream).
    virtual void render(std::int16_t* samples, std::size_t count) noexcept = 0;
    virtual bool playing() const noexcept = 0;
};

class ToneSource final : public AudioSource {
public:
    ToneSource(std::uint32_t frequencyHz, std::uint64_t durationSamples,
               std::uint32_t sampleRate, Waveform waveform,
               std::int16_t amplitude = ToneGenerator::DefaultAmplitude) noexcept {
        tone_.start(frequencyHz, durationSamples, sampleRate, waveform, amplitude);
    }
    void render(std::int16_t* samples, std::size_t count) noexcept override {
        for (std::size_t i = 0; i < count; ++i) samples[i] = 0;
        tone_.mixInto(samples, count);
    }
    bool playing() const noexcept override { return tone_.active(); }

private:
    ToneGenerator tone_;
};

// Fixed voice slots summed with saturation. Slots are independent: a source in
// one slot can be replaced or cleared without touching the others.
class Mixer {
public:
    static constexpr std::size_t SlotCount = 4;
    enum Slot : std::size_t { Music = 0, Effect = 1 };

    void set(std::size_t slot, std::unique_ptr<AudioSource> source) {
        if (slot < SlotCount) slots_[slot] = std::move(source);
    }
    void clear(std::size_t slot) noexcept {
        if (slot < SlotCount) slots_[slot].reset();
    }
    void clearAll() noexcept {
        for (auto& slot : slots_) slot.reset();
    }
    bool active(std::size_t slot) const noexcept {
        return slot < SlotCount && slots_[slot] && slots_[slot]->playing();
    }
    bool anyActive() const noexcept {
        for (std::size_t i = 0; i < SlotCount; ++i)
            if (active(i)) return true;
        return false;
    }

    // Mixes all voices into `samples`; finished voices are released.
    void render(std::int16_t* samples, std::size_t count) noexcept {
        std::array<std::int32_t, Chunk> sum{};
        std::array<std::int16_t, Chunk> voice{};
        for (std::size_t offset = 0; offset < count; offset += Chunk) {
            const std::size_t n = count - offset < Chunk ? count - offset : Chunk;
            for (std::size_t i = 0; i < n; ++i) sum[i] = 0;
            for (auto& slot : slots_) {
                if (!slot) continue;
                slot->render(voice.data(), n);
                for (std::size_t i = 0; i < n; ++i) sum[i] += voice[i];
                if (!slot->playing()) slot.reset();
            }
            for (std::size_t i = 0; i < n; ++i)
                samples[offset + i] = static_cast<std::int16_t>(
                    sum[i] > 32767 ? 32767 : (sum[i] < -32768 ? -32768 : sum[i]));
        }
    }

private:
    static constexpr std::size_t Chunk = 256;
    std::array<std::unique_ptr<AudioSource>, SlotCount> slots_{};
};

} // namespace ql32::music
