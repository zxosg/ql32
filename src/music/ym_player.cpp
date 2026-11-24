#include "ql32/music/ym_player.hpp"

namespace ql32::music {
namespace {
constexpr std::uint32_t MfpClock = 2457600;
constexpr std::array<std::uint16_t, 8> MfpPredivider{{0, 4, 10, 16, 50, 64, 100, 200}};
constexpr std::array<std::uint8_t, 16> SinusShape{{
    15, 14, 12, 9, 5, 2, 1, 2, 5, 9, 12, 14, 15, 14, 12, 9}};
// Effect timer phase: bit 31 flips once per MFP timer tick, so one tick is 2^31 units.
// Behavior (not code) follows the arnaud-carre/StSound reference (MIT): a SID gate and a
// sync-buzzer restart are both clocked by the timer and a SID square lasts two ticks.
std::uint32_t timerStep(std::uint32_t timerHz, std::uint32_t sampleRate) noexcept {
    return static_cast<std::uint32_t>((static_cast<std::uint64_t>(timerHz) << 31) / sampleRate);
}
} // namespace

YmPlayer::YmPlayer(YmSong song, std::uint32_t sampleRate)
    : song_(std::move(song)), sampleRate_(sampleRate ? sampleRate : DefaultSampleRate) {
    ym5_ = song_.metadata().format == "YM5!";
    ym2_ = song_.metadata().format == "YM2!";
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
    // YM5 has no effect type bits: slot 0 is always a SID voice and slot 1 a digi-drum.
    const unsigned type = ym5_ ? (slot == 0 ? 0 : 1) : ((control >> 6) & 3);
    switch (type) {
    case 0: v.effect = Effect::Sid; v.step = timerStep(timerHz, sampleRate_); break;
    case 1: startDrum(voice, r[8 + voice] & 31, timerHz); break;
    case 2: v.effect = Effect::Sinus; v.phase = 0; v.step = 2 * timerStep(timerHz, sampleRate_); break;
    default:
        v.effect = Effect::Buzzer;
        buzzerStep_ = timerStep(timerHz, sampleRate_);
        buzzerPhase_ = 0;
        buzzerShape_ = v.volume;
        buzzerVoice_ = voice;
        break;
    }
}

void YmPlayer::startDrum(unsigned voice, std::size_t number, std::uint32_t sampleHz) noexcept {
    if (number >= song_.drumCount() || song_.drum(number).empty() || sampleHz == 0) return;
    Voice& v = voices_[voice];
    v.effect = Effect::Drum;
    v.drum = number;
    v.drumPos = 0;
    v.drumStep = (static_cast<std::uint64_t>(sampleHz) << 32) / sampleRate_;
}

void YmPlayer::loadFrame(std::size_t frame) noexcept {
    if (frame >= song_.frameCount()) {
        playing_ = false;
        return;
    }
    auto r = song_.registers(frame);
    if (song_.failed()) {
        playing_ = false;
        return;
    }
    frame_ = frame;
    std::array<std::uint8_t, 14> chip;
    bool restart = false;
    for (std::size_t i = 0; i < 14; ++i) chip[i] = r[i];
    if (r[13] == 0xff) {
        chip[13] = last_[13];
    } else if (ym2_) {
        // YM2 (Mad Max): any register 13 write means "buzzer": shape 10 with register 12 = 0.
        chip[12] = 0;
        chip[13] = 10;
        restart = true;
    } else {
        restart = true;
    }
    int madMaxDrum = -1;
    if (ym2_ && (r[10] & 0x80)) {
        // Register 10 bit 7 announces a Mad Max digi-drum on voice C; the voice itself is muted.
        madMaxDrum = r[10] & 0x7f;
        chip[7] |= 0x24;
        chip[10] = 0;
    }
    last_ = r;
    last_[13] = chip[13];
    ay_.writeRegisters(chip, restart);
    buzzerStep_ = 0;
    for (auto& v : voices_) {
        if (v.effect != Effect::Drum) v.effect = Effect::None;
    }
    if (madMaxDrum >= 0 && r[12] != 0) startDrum(2, static_cast<std::size_t>(madMaxDrum), (MfpClock / 4) / r[12]);
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
    if (buzzerStep_ != 0) {
        buzzerPhase_ += buzzerStep_;
        if (buzzerPhase_ & 0x80000000u) {
            buzzerPhase_ &= 0x7fffffffu;
            ay_.restartEnvelopeShape(buzzerShape_);
        }
    }
    for (unsigned i = 0; i < 3; ++i) {
        Voice& v = voices_[i];
        switch (v.effect) {
        case Effect::Sid:
            // The phase keeps running across frames, like the timer does.
            v.phase += v.step;
            ay_.setLevelOverride(i, (v.phase & 0x80000000u) ? v.volume : 0);
            break;
        case Effect::Sinus:
            v.phase += v.step;
            ay_.setLevelOverride(i, SinusShape[v.phase >> 28] * v.volume / 15);
            break;
        case Effect::Buzzer:
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
        if (song_.hasEffects() || ym2_) tickEffects();
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
