#pragma once

#include "ql32/runtime/memory.hpp"
#include "ql32/runtime/music.hpp"
#include "ql32/runtime/storage.hpp"

#include <memory>
#include <string>
#include <vector>

namespace ql32::music { class AudioSource; }

namespace ql32::platform::olimex_vga {

class MusicService final : public runtime::MusicService {
public:
    // dacGpio selects the ESP32 built-in DAC pin: 25 (DAC1, default) or 26 (DAC2, CYD speaker).
    // vtxCacheRoots (tried in order, e.g. {"SDC:/", "FFS:/"}) makes VTX files play from a
    // transcoded cache on storage instead of being decoded into RAM; use it on targets
    // without PSRAM. Empty keeps the in-RAM decode.
    explicit MusicService(std::shared_ptr<runtime::StorageService> storage, int dacGpio = 25,
                          std::vector<std::string> vtxCacheRoots = {});
    ~MusicService() override;
    MusicService(const MusicService&) = delete;
    MusicService& operator=(const MusicService&) = delete;

    // Optional: refuse audio start and song loads when the heap is too low
    // (limits come from the service's AdmissionPolicy) instead of risking OOM.
    void setMemoryService(std::shared_ptr<runtime::MemoryService> memory);

    void play(runtime::MusicSessionId session, const std::string& path) override;
    void playWav(runtime::MusicSessionId session, const std::string& path) override;
    void testTone(runtime::MusicSessionId session) override;
    void beep(runtime::MusicSessionId session, std::uint32_t durationMs,
              std::uint32_t pitchHz, runtime::ToneWaveform waveform) override;
    void setVolume(std::uint8_t volume) override;
    std::uint8_t volume() const noexcept override;
    void stop(runtime::MusicSessionId session) override;
    void pause(runtime::MusicSessionId session) override;
    void resume(runtime::MusicSessionId session) override;
    runtime::MusicPlaybackState state() const noexcept override;
    std::uint64_t positionMs() const noexcept override;
    std::uint64_t lengthMs() const noexcept override;
    std::string title() const override;
    void stopSession(runtime::MusicSessionId session) noexcept override;

private:
    // Starts the audio task and I2S driver first, then checks the heap for a song.
    void prepareOutput(const char* what, bool loadsSong);
    void startMusic(runtime::MusicSessionId session, std::unique_ptr<music::AudioSource> source);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ql32::platform::olimex_vga
