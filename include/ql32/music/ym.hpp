#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ql32::music {

class AudioPrefetcher;
class VtxPlaneFile;

struct YmMetadata {
    std::string format;   // "YM2!", "YM3!", "YM3b", "YM5!", "YM6!", "YMT1"
    std::string title;
    std::string author;
    std::string comment;
    std::uint32_t chipClockHz = 2000000;
    std::uint16_t frameRateHz = 50;
    std::uint32_t loopFrame = 0;
    bool hasLoop = true;
    bool drumsDropped = false;   // digi-drums omitted to fit the device memory budget
    std::uint8_t trackerVoices = 0;   // YMT1 only: sample voices (1-4); registers() holds 4 bytes each
};

// Atari ST / Amstrad YM register-dump songs (QL/32-specific player). Accepts
// the raw file or one LHA (-lh5-/-lh0-, header level 0) member wrapping it.
// YM5/YM6 special effects (SID voice, sinus SID, digi-drum, sync-buzzer) are
// carried as frame register bits and are interpreted by YmPlayer.
// Digi-drum samples held in small blocks: a drum can be 14 KB or more, and a
// fragmented heap seldom has one contiguous block that large.
class DrumSamples {
public:
    static constexpr std::size_t BlockSamples = 128;
    DrumSamples() = default;
    DrumSamples(const std::vector<std::int16_t>& samples) {
        resize(samples.size());
        for (std::size_t i = 0; i < samples.size(); ++i) (*this)[i] = samples[i];
    }
    void resize(std::size_t count) {
        blocks_.clear();
        for (std::size_t done = 0; done < count; done += BlockSamples)
            blocks_.emplace_back(std::min(BlockSamples, count - done));
        size_ = count;
    }
    void push_back(std::int16_t v) {
        if (size_ % BlockSamples == 0) {
            blocks_.emplace_back();
            blocks_.back().reserve(BlockSamples);
        }
        blocks_.back().push_back(v);
        ++size_;
    }
    void clear() { blocks_.clear(); size_ = 0; }
    std::size_t size() const noexcept { return size_; }
    bool empty() const noexcept { return size_ == 0; }
    std::int16_t& operator[](std::size_t i) { return blocks_[i / BlockSamples][i % BlockSamples]; }
    std::int16_t operator[](std::size_t i) const { return blocks_[i / BlockSamples][i % BlockSamples]; }
    bool operator==(const DrumSamples& other) const {
        if (size_ != other.size_) return false;
        for (std::size_t i = 0; i < size_; ++i)
            if ((*this)[i] != other[i]) return false;
        return true;
    }

private:
    std::vector<std::vector<std::int16_t>> blocks_;
    std::size_t size_ = 0;
};

class YmSong {
public:
    static constexpr std::size_t MaximumPackedBytes = 256 * 1024;
    static constexpr std::size_t MaximumUnpackedBytes = 1536 * 1024;

    static bool looksLikeYm(const std::uint8_t* bytes, std::size_t size) noexcept;
    static YmSong parse(const std::uint8_t* bytes, std::size_t size);
    static YmSong parse(std::vector<std::uint8_t> bytes) { return parse(bytes.data(), bytes.size()); }

    const YmMetadata& metadata() const noexcept { return metadata_; }
    std::size_t frameCount() const noexcept { return frameCount_; }
    // 16 registers; registers 14/15 only carry effect data in YM5/YM6.
    std::array<std::uint8_t, 16> registers(std::size_t frame) const;
    std::size_t drumCount() const noexcept { return drums_.size(); }
    const DrumSamples& drum(std::size_t index) const { return drums_.at(index); }
    bool hasEffects() const noexcept { return effects_; }
    // External digi-drum bank (centred 16-bit samples), used by YM2 Mad Max files whose
    // drums are not stored in the file. No bank is built in; without one those drums are silent.
    // Song whose register frames are read from a transcoded cache (see openYmStreamed).
    static YmSong fromPlaneFile(YmMetadata metadata,
                                std::vector<DrumSamples> drums,
                                std::shared_ptr<VtxPlaneFile> planes, std::size_t frameCount,
                                bool effects);
    // True once a storage read failed; playback should stop.
    bool failed() const noexcept;
    std::shared_ptr<AudioPrefetcher> prefetcher() const;
    void setDrumBank(const std::vector<std::vector<std::int16_t>>& drums) {
        drums_.assign(drums.begin(), drums.end());
    }

private:
    YmMetadata metadata_;
    std::vector<std::uint8_t> data_;       // frame-major, 16 bytes per frame
    std::vector<DrumSamples> drums_;   // centred 16-bit samples
    std::shared_ptr<VtxPlaneFile> planes_;
    std::size_t frameCount_ = 0;
    bool effects_ = false;
};

} // namespace ql32::music
