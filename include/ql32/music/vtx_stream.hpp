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

// QL/32-specific transcoded register cache ("Q32V" for VTX, "Q32Y" for YM,
// version 2): a 32-byte header, then the register data cut into windows of 256
// frames, each window stored as a small blob (u16 code: bit 15 = stored raw,
// otherwise the length of an LZ payload; see lz8 in vtx_stream.cpp), then (YM) the
// digi-drum bank, then a trailer with the start/loop offsets of every stream.
// Planar sources keep one stream per register plane (windows of one plane follow
// each other); interleaved YM sources are stored window-major (one stream whose
// blobs hold all planes of a window). Reading seeks between streams are slow on
// some filesystems (about 40 ms on SPIFFS), so the next window of each plane is
// read ahead by run(); render-time reads are only a fallback. Decoding needs no
// tables. Playback needs a few KiB of heap regardless of song length.
class VtxPlaneFile final : public AudioPrefetcher {
public:
    static constexpr std::size_t HeaderBytes = 32;
    static constexpr std::size_t WindowFrames = 256;
    static constexpr std::size_t MaximumPlanes = 16;

    struct Layout {
        std::array<std::uint32_t, MaximumPlanes> start{};   // first blob of each stream
        std::array<std::uint32_t, MaximumPlanes> loop{};    // blob of the loop window
        std::uint32_t drumStart = 0;                        // end of the register blobs
        std::uint32_t trailerStart = 0;                     // end of the drum bank
    };
    // Reads and validates the trailer (throws on a damaged cache).
    static Layout readLayout(runtime::StorageReadHandle& file, std::size_t planeCount,
                             bool windowMajor);

    // Loads the first window of every plane (may throw on storage errors).
    VtxPlaneFile(std::unique_ptr<runtime::StorageReadHandle> file, std::size_t frameCount,
                 std::size_t loopFrame, std::size_t planeCount, bool windowMajor,
                 const Layout& layout);
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
    struct Known {
        std::size_t window = 0;
        std::uint32_t offset = 0;
    };
    bool load(std::size_t plane, std::size_t start, std::vector<std::uint8_t>& destination,
              Window& window);
    void readWindow(std::size_t stream, std::size_t window, std::size_t rawLength,
                    std::size_t rawBytesPerWindow, std::uint8_t* destination);
    std::unique_ptr<runtime::StorageReadHandle> file_;
    std::size_t frameCount_;
    std::size_t loopFrame_;
    std::size_t planeCount_;
    bool windowMajor_;
    Layout layout_;
    std::mutex fileMutex_;  // serialises reads from file_ and guards the members below
    std::array<Known, MaximumPlanes> known_{};
    std::vector<std::uint8_t> blob_;
    std::vector<std::uint8_t> wholeWindow_;
    std::size_t wholeWindowIndex_ = static_cast<std::size_t>(-1);
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
// Chooses the digi-drum sample budget for the next YM load: the platform's full
// budget, or a reduced one (drums above it are dropped and the song is flagged).
void setYmDrumBudgetReduced(bool reduced);

YmSong openYmStreamed(runtime::StorageService& storage, const std::string& path,
                      const std::vector<std::string>& cacheRoots,
                      VtxStreamInfo* info = nullptr);

} // namespace ql32::music
