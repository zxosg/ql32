#pragma once

#include "ql32/music/mixer.hpp"
#include "ql32/music/vtx_player.hpp"

namespace ql32::music {

class VtxSource final : public AudioSource {
public:
    explicit VtxSource(VtxSong song, std::uint32_t sampleRate = VtxPlayer::DefaultSampleRate)
        : player_(std::move(song), sampleRate) {}
    void render(std::int16_t* samples, std::size_t count) noexcept override {
        player_.render(samples, count);
    }
    bool playing() const noexcept override { return player_.playing(); }
    std::shared_ptr<AudioPrefetcher> prefetcher() const override { return player_.prefetcher(); }
    std::uint32_t positionMs() const noexcept override {
        return static_cast<std::uint32_t>(player_.framePosition() * 1000ULL / hz());
    }
    std::uint32_t lengthMs() const noexcept override {
        return static_cast<std::uint32_t>(player_.frameCount() * 1000ULL / hz());
    }
    std::string title() const override { return player_.metadata().title; }

private:
    std::uint32_t hz() const noexcept {
        return player_.metadata().playerHz ? player_.metadata().playerHz : 50;
    }
    VtxPlayer player_;
};

} // namespace ql32::music
