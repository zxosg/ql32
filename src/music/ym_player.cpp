#include "ql32/music/ym_player.hpp"

namespace ql32::music {
namespace {
constexpr std::uint32_t MfpClock = 2457600;
constexpr std::array<std::uint16_t, 8> MfpPredivider{{0, 4, 10, 16, 50, 64, 100, 200}};
constexpr std::array<std::uint8_t, 16> SinusShape{{
    15, 14, 12, 9, 5, 2, 1, 2, 5, 9, 12, 14, 15, 14, 12, 9}};
// Timer step in 32-bit phase units per output sample.
std::uint32_t timerStep(std::uint32_t timerHz, std::uint32_t sampleRate) noexcept {
    return static_cast<std::uint32_t>((static_cast<std::uint64_t>(timerHz) << 32) / sampleRate);
}
} // namespace

YmPlayer::YmPlayer(YmSong song, std::uint32_t sampleRate)
    : song_(std::move(song)), sampleRate_(sampleRate ? sampleRate : DefaultSampleRate) {
    ay_.reset(song_.metadata().chipClockHz, sampleRate_);
    loadFrame(0);
}

void YmPlayer::startEffect(unsigned slot, const std::array<std::uint8_t, 16>& r) noexcept {
    const std::uint8_t control = r[1 + slot * 2];
    const unsigned code = (control >> 4) & 3;
    if (code == 0) return;
    const unsigned voice = code - 1;
    const std::uint32_t predivider = static_cast<std::uint32_t>(
        MfpPredivider[(r[6 + slot * 2] >> 5) & 7]) * r[14 + slot];
    if (predivider == 0) return;
    const std::uint32_t timerHz = MfpClock / predivider;
    Voice& v = voices_[voice];
    v.volume = r[8 + voice] & 15;
    switch ((control >> 6) & 3) {
    case 0: v.effect = Effect::Sid; v.phase = 0; v.step = timerStep(timerHz, sampleRate_); break;
    case 1: {
        const std::size_t number = r[8 + voice] & 31;
        if (number >= song_.drumCount() || song_.drum(number).empty()) break;
        v.effect = Effect::Drum;
        v.drum = number;
        v.drumPos = 0;
        v.drumStep = (static_cast<std::uint64_t>(timerHz) << 32) / sampleRate_;
        break;
    }
    case 2: v.effect = Effect::Sinus; v.phase = 0; v.step = timerStep(timerHz, sampleRate_); break;
    default: v.effect = Effect::Buzzer; v.phase = 0; v.step = timerStep(timerHz, sampleRate_); break;
    }
}

void YmPlayer::loadFrame(std::size_t frame) noexcept {
    if (frame >= song_.frameCount()) {
        playing_ = false;
        return;
    }
    auto r = song_.registers(frame);
    frame_ = frame;
    std::array<std::uint8_t, 14> chip;
    bool restart = false;
    for (std::size_t i = 0; i < 14; ++i) chip[i] = r[i];
    if (r[13] == 0xff) chip[13] = last_[13]; else restart = true;
    last_ = r;
    last_[13] = chip[13];
    ay_.writeRegisters(chip, restart);
    for (auto& v : voices_) {
        if (v.effect != Effect::Drum) v.effect = Effect::None;
    }
    if (song_.hasEffects()) {
        startEffect(0, r);
        startEffect(1, r);
    }
    for (unsigned i = 0; i < 3; ++i) {
        if (voices_[i].effect == Effect::None) {
            ay_.setLevelOverride(i, -1);
            if (!(voices_[i].effect == Effect::Drum)) ay_.setDirectSample(i, false);
        }
    }
}

void YmPlayer::tickEffects() noexcept {
    for (unsigned i = 0; i < 3; ++i) {
        Voice& v = voices_[i];
        switch (v.effect) {
        case Effect::Sid:
            v.phase += v.step;
            ay_.setLevelOverride(i, (v.phase & 0x80000000u) ? v.volume : 0);
            break;
        case Effect::Sinus:
            v.phase += v.step;
            ay_.setLevelOverride(i, SinusShape[v.phase >> 28] * v.volume / 15);
            break;
        case Effect::Buzzer:
            v.phase += v.step;
            if (v.phase < v.step) ay_.restartEnvelopeNow();
            break;
        case Effect::Drum: {
            const auto& data = song_.drum(v.drum);
            const std::uint64_t index = v.drumPos >> 32;
            if (index >= data.size()) {
                v.effect = Effect::None;
                ay_.setDirectSample(i, false);
            } else {
                ay_.setDirectSample(i, true, data[index]);
                v.drumPos += v.drumStep;
            }
            break;
        }
        case Effect::None: break;
        }
    }
}

void YmPlayer::render(std::int16_t* samples, std::size_t count) noexcept {
    if (!samples) return;
    const auto rate = song_.metadata().frameRateHz;
    for (std::size_t i = 0; i < count; ++i) {
        if (!playing_) {
            samples[i] = 0;
            continue;
        }
        if (song_.hasEffects()) tickEffects();
        samples[i] = ay_.renderSample();
        frameAccumulator_ += rate;
        if (frameAccumulator_ >= sampleRate_) {
            frameAccumulator_ -= sampleRate_;
            std::size_t next = frame_ + 1;
            if (next >= song_.frameCount())
                next = song_.metadata().hasLoop ? song_.metadata().loopFrame : song_.frameCount();
            loadFrame(next);
        }
    }
}

} // namespace ql32::music
