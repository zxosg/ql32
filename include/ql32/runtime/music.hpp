#pragma once

#include <cstdint>
#include <string>

namespace ql32::runtime {

using MusicSessionId = std::uint64_t;

enum class MusicPlaybackState : std::uint8_t { Stopped, Playing, Paused };

enum class ToneWaveform : std::uint8_t { Square, Sine, Triangle, SawtoothRising, SawtoothFalling };

class MusicService {
public:
    virtual ~MusicService() = default;
    virtual void play(MusicSessionId session, const std::string& path) = 0;
    // Streams an uncompressed PCM WAV from storage without loading it fully.
    virtual void playWav(MusicSessionId session, const std::string& path) = 0;
    virtual void testTone(MusicSessionId session) = 0;
    // Starts a tone and returns at once; it ends by itself after durationMs.
    virtual void beep(MusicSessionId session, std::uint32_t durationMs,
                      std::uint32_t pitchHz, ToneWaveform waveform) = 0;
    // Global output volume (0-255) for BEEP and MUSIC, not per session.
    virtual void setVolume(std::uint8_t volume) = 0;
    virtual std::uint8_t volume() const noexcept = 0;
    virtual void stop(MusicSessionId session) = 0;
    virtual void pause(MusicSessionId session) = 0;
    virtual void resume(MusicSessionId session) = 0;
    virtual MusicPlaybackState state() const noexcept = 0;
    // Readback for the current song; 0 / empty when stopped or unknown.
    virtual std::uint64_t positionMs() const noexcept { return 0; }
    virtual std::uint64_t lengthMs() const noexcept { return 0; }
    virtual std::string title() const { return {}; }
    virtual void stopSession(MusicSessionId session) noexcept = 0;
};

} // namespace ql32::runtime
