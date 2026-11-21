#pragma once

#include "ql32/music/ay38912.hpp"
#include "ql32/music/mixer.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ql32::music {

namespace pt3_tables {
extern const std::uint16_t NotePt33[96];
extern const std::uint16_t NotePt34[96];
extern const std::uint16_t NoteSt[96];
extern const std::uint16_t NoteAsm33[96];
extern const std::uint16_t NoteAsm34[96];
extern const std::uint16_t NoteReal33[96];
extern const std::uint16_t NoteReal34[96];
extern const std::uint8_t VolumeOld[256];
extern const std::uint8_t VolumeNew[256];
} // namespace pt3_tables

// ProTracker 3 / Vortex Tracker II module (single AY chip). TurboSound
// (second module appended to the file) is not supported; the extra data is ignored.
class Pt3Module {
public:
    static constexpr std::size_t MaximumBytes = 65536;
    static bool looksLikePt3(const std::uint8_t* data, std::size_t size) noexcept;
    // Throws std::runtime_error for malformed modules.
    static Pt3Module parse(std::vector<std::uint8_t> bytes);

    std::uint8_t at(std::size_t offset) const noexcept { return offset < bytes_.size() ? bytes_[offset] : 0; }
    std::uint16_t word(std::size_t offset) const noexcept { return static_cast<std::uint16_t>(at(offset) | (at(offset + 1) << 8)); }
    std::size_t size() const noexcept { return bytes_.size(); }
    unsigned version() const noexcept { return version_; }
    unsigned toneTable() const noexcept { return at(0x63); }
    unsigned initialDelay() const noexcept { return at(0x64); }
    unsigned positionCount() const noexcept { return at(0x65); }
    unsigned loopPosition() const noexcept { return at(0x66); }
    std::size_t patternsPointer() const noexcept { return word(0x67); }
    std::size_t samplePointer(unsigned n) const noexcept { return word(0x69 + (n & 31) * 2); }
    std::size_t ornamentPointer(unsigned n) const noexcept { return word(0xa9 + (n & 15) * 2); }
    unsigned positionPattern(unsigned position) const noexcept { return at(0xc9 + position); }
    std::uint16_t notePeriod(unsigned note) const noexcept;

private:
    std::vector<std::uint8_t> bytes_;
    unsigned version_ = 6;
};

// Register-level PT3 engine: one call to tick() per 50 Hz player interrupt.
class Pt3Engine {
public:
    explicit Pt3Engine(const Pt3Module& module) noexcept;
    // Returns the 14 AY registers; shapeWritten is true when register 13 must (re)start the envelope.
    std::array<std::uint8_t, 14> tick(bool& shapeWritten) noexcept;
    std::size_t loopCount() const noexcept { return loops_; }
    bool failed() const noexcept { return failed_; }

private:
    struct Channel {
        std::size_t address = 0;
        std::size_t samplePtr = 0, ornamentPtr = 0;
        unsigned sampleLoop = 0, sampleLength = 0, samplePos = 0;
        unsigned ornamentLoop = 0, ornamentLength = 0, ornamentPos = 0;
        unsigned note = 0, volume = 15;
        std::uint8_t skipCounter = 1, notesToSkip = 1;
        bool enabled = false, envelopeEnabled = false, simpleGliss = false;
        int tonAccumulator = 0, tonSliding = 0, tonSlideCount = 0, tonSlideDelay = 0, tonSlideStep = 0;
        int tonDelta = 0;
        unsigned slideToNote = 0;
        int amplitudeSliding = 0;
        std::uint8_t envelopeSliding = 0, noiseSliding = 0;
        unsigned onOffDelay = 0, offOnDelay = 0, currentOnOff = 0;
        int tone = 0, amplitude = 0;
    };
    void selectSample(Channel&, unsigned) noexcept;
    void selectOrnament(Channel&, unsigned) noexcept;
    void interpret(Channel&) noexcept;
    void changeRegisters(Channel&, std::uint8_t& addToEnv, std::uint8_t& mixer) noexcept;
    void loadPosition() noexcept;

    const Pt3Module& module_;
    std::array<Channel, 3> ch_{};
    unsigned delay_ = 1, delayCounter_ = 1, position_ = 0;
    unsigned noiseBase_ = 0, addToNoise_ = 0;
    std::uint16_t envBase_ = 0;
    int envSlide_ = 0, envSlideAdd_ = 0, envDelay_ = 0, curEnvDelay_ = 0;
    int pendingShape_ = -1;
    std::size_t loops_ = 0;
    bool failed_ = false;
};

class Pt3Player {
public:
    static constexpr std::uint32_t DefaultSampleRate = 22050;
    static constexpr std::uint32_t PlayerHz = 50;
    static constexpr std::uint32_t ChipClockHz = 1773400;
    explicit Pt3Player(Pt3Module module, std::uint32_t sampleRate = DefaultSampleRate);
    Pt3Player(const Pt3Player&) = delete;
    Pt3Player& operator=(const Pt3Player&) = delete;
    void render(std::int16_t* samples, std::size_t count) noexcept;
    bool playing() const noexcept { return playing_; }
    std::size_t loopCount() const noexcept { return engine_.loopCount(); }

private:
    Pt3Module module_;
    Pt3Engine engine_;
    Ay38912 ay_;
    std::uint32_t sampleRate_;
    std::uint32_t accumulator_ = 0;
    bool playing_ = true;
    bool primed_ = false;
    std::uint8_t lastShape_ = 0;
};

class Pt3Source final : public AudioSource {
public:
    explicit Pt3Source(Pt3Module module, std::uint32_t sampleRate = Pt3Player::DefaultSampleRate)
        : player_(std::move(module), sampleRate) {}
    void render(std::int16_t* samples, std::size_t count) noexcept override { player_.render(samples, count); }
    bool playing() const noexcept override { return player_.playing(); }

private:
    Pt3Player player_;
};

} // namespace ql32::music
