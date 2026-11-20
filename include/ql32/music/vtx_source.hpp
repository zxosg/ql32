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

private:
    VtxPlayer player_;
};

} // namespace ql32::music
