#pragma once

#include "ql32/music/ay38912.hpp"
#include "ql32/music/mixer.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ql32::music {

// SQ-Tracker (SQT) module. The format has no signature: looksLikeSqt()
// validates the pointer table geometry (fixed 98-byte samples, 34-byte ornaments).
class SqtModule {
public:
    static constexpr std::size_t MaximumBytes = 65536;
    static bool looksLikeSqt(const std::uint8_t* data, std::size_t size) noexcept;
    // Throws std::runtime_error for malformed modules. Pointers are rebased to file offsets.
    static SqtModule parse(std::vector<std::uint8_t> bytes);

    std::uint8_t at(std::size_t o) const noexcept { return o < bytes_.size() ? bytes_[o] : 0; }
    std::uint16_t word(std::size_t o) const noexcept { return static_cast<std::uint16_t>(at(o) | (at(o + 1) << 8)); }
    std::size_t size() const noexcept { return bytes_.size(); }
    std::size_t samplesPointer() const noexcept { return word(2); }
    std::size_t ornamentsPointer() const noexcept { return word(4); }
    std::size_t patternsPointer() const noexcept { return word(6); }
    std::size_t positionsPointer() const noexcept { return word(8); }
    std::size_t loopPointer() const noexcept { return word(10); }

private:
    std::vector<std::uint8_t> bytes_;
};

class SqtEngine {
public:
    explicit SqtEngine(const SqtModule& module) noexcept;
    std::array<std::uint8_t, 14> tick(bool& shapeWritten) noexcept;
    std::size_t loopCount() const noexcept { return loops_; }

private:
    struct Channel {
        std::size_t address = 0, samplePtr = 0, pointInSample = 0, ornamentPtr = 0, pointInOrnament = 0, ix27 = 0;
        std::uint16_t ton = 0;
        std::uint8_t volume = 0, amplitude = 0, note = 0, ix21 = 0;
        std::int16_t slideStep = 0, sliding = 0;
        std::int8_t sampleTik = 0, ornamentTik = 0, transpose = 0;
        bool enabled = false, envelope = false, ornament = false, gliss = false;
        bool mixNoise = false, mixTon = false, b4 = false, b6 = false, b7 = false;
    };
    void command(Channel&, std::size_t& ptr, int a) noexcept;
    void startSample(Channel&, unsigned) noexcept;
    void startOrnament(Channel&, unsigned) noexcept;
    void afterNote(Channel&, std::size_t& ptr) noexcept;
    void repeatLast(Channel&, std::size_t& ptr) noexcept;
    void interpret(Channel&) noexcept;
    void registersFor(Channel&, std::uint8_t& mixer) noexcept;
    void loadPosition(Channel&, bool first) noexcept;

    const SqtModule& m_;
    std::array<Channel, 3> ch_{};   // A, B, C
    std::uint8_t delay_ = 1, delayCounter_ = 1, lines_ = 1, noise_ = 0, envFine_ = 0;
    std::size_t positions_ = 0, loops_ = 0;
    int pendingShape_ = -1;
};

class SqtPlayer {
public:
    static constexpr std::uint32_t DefaultSampleRate = 22050;
    static constexpr std::uint32_t PlayerHz = 50;
    static constexpr std::uint32_t ChipClockHz = 1773400;
    explicit SqtPlayer(SqtModule module, std::uint32_t sampleRate = DefaultSampleRate);
    SqtPlayer(const SqtPlayer&) = delete;
    SqtPlayer& operator=(const SqtPlayer&) = delete;
    void render(std::int16_t* samples, std::size_t count) noexcept;
    bool playing() const noexcept { return true; }

private:
    SqtModule module_;
    SqtEngine engine_;
    Ay38912 ay_;
    std::uint32_t sampleRate_;
    std::uint32_t accumulator_ = 0;
    bool primed_ = false;
    std::uint8_t lastShape_ = 0;
};

class SqtSource final : public AudioSource {
public:
    explicit SqtSource(SqtModule module, std::uint32_t sampleRate = SqtPlayer::DefaultSampleRate)
        : player_(std::move(module), sampleRate) {}
    void render(std::int16_t* samples, std::size_t count) noexcept override { player_.render(samples, count); }
    bool playing() const noexcept override { return player_.playing(); }

private:
    SqtPlayer player_;
};

} // namespace ql32::music
