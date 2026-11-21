#include "ql32/music/vtx_player.hpp"

#include <algorithm>

namespace ql32::music {

VtxPlayer::VtxPlayer(VtxSong song, std::uint32_t sampleRate)
    : song_(std::move(song)), sampleRate_(sampleRate ? sampleRate : DefaultSampleRate) {
    ay_.reset(song_.metadata().chipClockHz, sampleRate_);
    loadFrame(0);
}

void VtxPlayer::loadFrame(std::size_t frame) noexcept {
    if (frame >= song_.frameCount()) {
        playing_ = false;
        return;
    }
    auto registers = song_.registers(frame);
    // R13 = 0xFF marks "envelope shape not written this frame"; any other value is a write
    // that retriggers the envelope, even when it equals the previous shape.
    const bool shapeWritten = registers[13] != 0xff;
    if (shapeWritten) lastShape_ = registers[13];
    else registers[13] = lastShape_;
    ay_.writeRegisters(registers, shapeWritten);
    framePosition_ = frame;
}

void VtxPlayer::render(std::int16_t* samples, std::size_t count) noexcept {
    if (!samples) return;
    for (std::size_t i = 0; i < count; ++i) {
        if (!playing_ || paused_) {
            samples[i] = 0;
            continue;
        }
        samples[i] = ay_.renderSample();
        frameAccumulator_ += song_.metadata().playerHz;
        if (frameAccumulator_ >= sampleRate_) {
            frameAccumulator_ -= sampleRate_;
            std::size_t next = framePosition_ + 1;
            if (next >= song_.frameCount()) {
                const auto loop = song_.metadata().loopFrame;
                next = loop < song_.frameCount() ? loop : song_.frameCount();
            }
            loadFrame(next);
        }
    }
}

} // namespace ql32::music
