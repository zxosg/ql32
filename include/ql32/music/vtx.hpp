#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ql32::music {

struct VtxMetadata {
    std::string title;
    std::string author;
    std::string source;
    std::string editor;
    std::string comment;
    std::uint32_t chipClockHz = 1773400;
    std::uint16_t loopFrame = 0xffff;
    std::uint8_t playerHz = 50;
    std::uint8_t stereoMode = 0;
    bool ym2149 = false;
};

class VtxSong {
public:
    static constexpr std::size_t MaximumFileBytes = 64 * 1024;
    static constexpr std::size_t MaximumRegisterBytes = 96 * 1024;

    static VtxSong parse(const std::uint8_t* bytes, std::size_t size);
    static VtxSong parse(std::vector<std::uint8_t> bytes) {
        return parse(bytes.data(), bytes.size());
    }

    const VtxMetadata& metadata() const noexcept { return metadata_; }
    std::size_t frameCount() const noexcept { return frameCount_; }
    std::array<std::uint8_t, 14> registers(std::size_t frame) const;

private:
    VtxMetadata metadata_;
    std::vector<std::uint8_t> registers_;
    std::size_t frameCount_ = 0;
};

} // namespace ql32::music
