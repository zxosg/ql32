#pragma once

#include "ql32/music/vtx_source.hpp"
#include "ql32/music/ym_player.hpp"

#include <memory>
#include <stdexcept>
#include <vector>

namespace ql32::music {

// Chooses the chiptune decoder from the file content (not the extension).
constexpr std::size_t maximumSongFileBytes() noexcept { return YmSong::MaximumPackedBytes; }

inline std::unique_ptr<AudioSource> openSong(std::vector<std::uint8_t> bytes) {
    if (bytes.size() >= 2 && ((bytes[0] == 'a' && bytes[1] == 'y') ||
                              (bytes[0] == 'y' && bytes[1] == 'm'))) {
        if (bytes.size() > VtxSong::MaximumFileBytes)
            throw std::runtime_error("VTX file exceeds 64 KiB");
        return std::make_unique<VtxSource>(VtxSong::parse(std::move(bytes)));
    }
    if (YmSong::looksLikeYm(bytes.data(), bytes.size()))
        return std::make_unique<YmSource>(YmSong::parse(std::move(bytes)));
    throw std::runtime_error("unrecognised music file (supported: VTX, YM)");
}

} // namespace ql32::music
