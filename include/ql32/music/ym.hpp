#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ql32::music {

struct YmMetadata {
    std::string format;   // "YM2!", "YM3!", "YM3b", "YM5!", "YM6!"
    std::string title;
    std::string author;
    std::string comment;
    std::uint32_t chipClockHz = 2000000;
    std::uint16_t frameRateHz = 50;
    std::uint32_t loopFrame = 0;
    bool hasLoop = true;
};

// Atari ST / Amstrad YM register-dump songs (QL/32-specific player). Accepts
// the raw file or one LHA (-lh5-/-lh0-, header level 0) member wrapping it.
// YM5/YM6 special effects (SID voice, sinus SID, digi-drum, sync-buzzer) are
// carried as frame register bits and are interpreted by YmPlayer.
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
    const std::vector<std::int16_t>& drum(std::size_t index) const { return drums_.at(index); }
    bool hasEffects() const noexcept { return effects_; }
    // External digi-drum bank (centred 16-bit samples), used by YM2 Mad Max files whose
    // drums are not stored in the file. No bank is built in; without one those drums are silent.
    void setDrumBank(std::vector<std::vector<std::int16_t>> drums) { drums_ = std::move(drums); }

private:
    YmMetadata metadata_;
    std::vector<std::uint8_t> data_;       // frame-major, 16 bytes per frame
    std::vector<std::vector<std::int16_t>> drums_;   // centred 16-bit samples
    std::size_t frameCount_ = 0;
    bool effects_ = false;
};

} // namespace ql32::music
