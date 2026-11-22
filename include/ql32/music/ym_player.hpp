#pragma once

#include "ql32/music/ay38912.hpp"
#include "ql32/music/mixer.hpp"
#include "ql32/music/ym.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace ql32::music {

// Plays a YmSong through the AY core and interprets the YM5/YM6 effect bits
// (SID voice, sinus SID, digi-drum, sync-buzzer). The MFP timer clock is
// 2457600 Hz. Effect decoding follows the arnaud-carre/StSound behavior (MIT, reference
// only, independent code): YM5 slot 0 is SID and slot 1 a digi-drum; YM6 uses type bits;
// YM2 uses the Mad Max register conventions. Sinus-SID (not implemented by StSound) uses a
// 16-step sine amplitude shape and is QL/32-specific.
class YmPlayer {
public:
    static constexpr std::uint32_t DefaultSampleRate = 22050;
    explicit YmPlayer(YmSong song, std::uint32_t sampleRate = DefaultSampleRate);
    void render(std::int16_t* samples, std::size_t count) noexcept;
    bool playing() const noexcept { return playing_; }
    std::size_t framePosition() const noexcept { return frame_; }
    const YmMetadata& metadata() const noexcept { return song_.metadata(); }
    std::shared_ptr<AudioPrefetcher> prefetcher() const { return song_.prefetcher(); }

private:
    enum class Effect : std::uint8_t { None, Sid, Drum, Sinus, Buzzer };
    struct Voice {
        Effect effect = Effect::None;
        std::uint32_t phase = 0;
        std::uint32_t step = 0;
        std::uint8_t volume = 0;
        std::size_t drum = 0;
        std::uint64_t drumPos = 0;   // 32.32 fixed point sample index
        std::uint64_t drumStep = 0;
    };
    void loadFrame(std::size_t frame) noexcept;
    void startEffect(unsigned slot, const std::array<std::uint8_t, 16>& r) noexcept;
    void startDrum(unsigned voice, std::size_t number, std::uint32_t sampleHz) noexcept;
    void tickEffects() noexcept;

    YmSong song_;
    Ay38912 ay_;
    std::uint32_t sampleRate_;
    std::array<Voice, 3> voices_{};
    std::array<std::uint8_t, 16> last_{};
    std::uint64_t frameAccumulator_ = 0;
    std::size_t frame_ = 0;
    std::uint32_t buzzerPhase_ = 0;
    std::uint32_t buzzerStep_ = 0;
    std::uint8_t buzzerShape_ = 0;
    unsigned buzzerVoice_ = 0;
    bool ym5_ = false;
    bool ym2_ = false;
    bool playing_ = true;
};

class YmSource final : public AudioSource {
public:
    explicit YmSource(YmSong song, std::uint32_t sampleRate = YmPlayer::DefaultSampleRate)
        : player_(std::move(song), sampleRate) {}
    void render(std::int16_t* samples, std::size_t count) noexcept override {
        player_.render(samples, count);
    }
    bool playing() const noexcept override { return player_.playing(); }
    std::shared_ptr<AudioPrefetcher> prefetcher() const override { return player_.prefetcher(); }

private:
    YmPlayer player_;
};

} // namespace ql32::music
