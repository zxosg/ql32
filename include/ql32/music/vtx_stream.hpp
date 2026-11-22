#pragma once

#include "ql32/music/mixer.hpp"
#include "ql32/music/vtx.hpp"
#include "ql32/music/ym.hpp"
#include "ql32/runtime/storage.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace ql32::music {

// QL/32-specific transcoded VTX cache ("Q32V"): a 32-byte header followed by
// the fourteen register planes exactly as LH5 yields them. The file is
// written sequentially (so any storage adapter can produce it) and played by
// reading small per-plane windows. Seeking between the planes is slow on some
// filesystems (about 40 ms per seek on SPIFFS), so the next window of each
// plane is read ahead by run(); render-time reads are only a fallback.
// Playback needs about 8 KiB of heap regardless of song length.
class VtxPlaneFile final : public AudioPrefetcher {
public:
    static constexpr std::size_t HeaderBytes = 32;
    static constexpr std::size_t WindowFrames = 256;

    static constexpr std::size_t MaximumPlanes = 16;

    // Loads the first window of every plane (may throw on storage errors).
    // interleaved: the data is frame-major (planeCount bytes per frame) instead
    // of one plane after another.
    VtxPlaneFile(std::unique_ptr<runtime::StorageReadHandle> file, std::size_t frameCount,
                 std::size_t loopFrame, std::size_t planeCount = 14, bool interleaved = false);
    std::array<std::uint8_t, 14> registers(std::size_t frame);
    // All sixteen YM registers; planes beyond the file's plane count read as zero.
    std::array<std::uint8_t, 16> registers16(std::size_t frame);
    bool failed() const noexcept { return failed_; }
    void run() noexcept override;

private:
    struct Window {
        std::size_t start = 0;
        std::size_t length = 0;
    };
    bool load(std::size_t plane, std::size_t start, std::vector<std::uint8_t>& destination,
              Window& window);
    std::unique_ptr<runtime::StorageReadHandle> file_;
    std::size_t frameCount_;
    std::size_t loopFrame_;
    std::size_t planeCount_;
    bool interleaved_;
    std::mutex fileMutex_;  // serialises reads from file_
    std::mutex stateMutex_; // guards the window tables and buffer swaps
    std::array<std::vector<std::uint8_t>, MaximumPlanes> current_;
    std::array<std::vector<std::uint8_t>, MaximumPlanes> next_;
    std::array<Window, MaximumPlanes> currentWindow_{};
    std::array<Window, MaximumPlanes> nextWindow_{};
    std::vector<std::uint8_t> scratch_; // render thread only
    std::atomic<std::size_t> lastFrame_{0};
    std::atomic<bool> failed_{false};
};

struct VtxStreamInfo {
    std::string cachePath;
    bool cacheHit = false;
};

// Opens a VTX for playback from a transcoded cache instead of decoding it into
// RAM. Cache roots are tried in order (for example {"SDC:/", "FFS:/"}); a
// root that is missing or write-protected is skipped. Throws std::runtime_error
// for a malformed VTX and runtime::ServiceError when no root can hold the cache.
VtxSong openVtxStreamed(runtime::StorageService& storage, const std::string& path,
                        const std::vector<std::string>& cacheRoots,
                        VtxStreamInfo* info = nullptr);

} // namespace ql32::music

namespace ql32::music {

// YM counterpart of openVtxStreamed: the YM5/YM6 (or YM2/YM3/YM3b, raw or LHA
// wrapped) register stream is transcoded once to a cache and played from small
// windows, so a song costs a few KiB of heap instead of frames x 16 bytes. Digi-drum
// samples stay in RAM (at most 32 KiB in total; larger drums are silent). Title,
// author and comment are not kept. Cache files use the "Q32Y" magic.
YmSong openYmStreamed(runtime::StorageService& storage, const std::string& path,
                      const std::vector<std::string>& cacheRoots,
                      VtxStreamInfo* info = nullptr);

} // namespace ql32::music
