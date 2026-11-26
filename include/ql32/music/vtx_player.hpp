#pragma once

#include "ql32/music/ay38912.hpp"
#include "ql32/music/vtx.hpp"

#include <cstddef>
#include <cstdint>

namespace ql32::music {

class VtxPlayer {
public:
    static constexpr std::uint32_t DefaultSampleRate = 22050;

    explicit VtxPlayer(VtxSong song,
                       std::uint32_t sampleRate = DefaultSampleRate);
    void render(std::int16_t* samples, std::size_t count) noexcept;
    void setPaused(bool paused) noexcept { paused_ = paused; }
    bool playing() const noexcept { return playing_; }
    bool paused() const noexcept { return paused_; }
    std::size_t framePosition() const noexcept { return framePosition_; }
    std::size_t frameCount() const noexcept { return song_.frameCount(); }
    const VtxMetadata& metadata() const noexcept { return song_.metadata(); }
    std::shared_ptr<AudioPrefetcher> prefetcher() const { return song_.prefetcher(); }

private:
    void loadFrame(std::size_t frame) noexcept;
    VtxSong song_;
    Ay38912 ay_;
    std::uint32_t sampleRate_;
    std::uint64_t frameAccumulator_ = 0;
    std::size_t framePosition_ = 0;
    bool playing_ = true;
    bool paused_ = false;
    std::uint8_t lastShape_ = 0;
};

} // namespace ql32::music
