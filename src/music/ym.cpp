#include "ql32/music/ym.hpp"
#include "ql32/music/lh5.hpp"
#include "ql32/music/vtx_stream.hpp"

#include <cstring>
#include <stdexcept>

namespace ql32::music {
namespace {
std::uint32_t be32(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) | p[3];
}
std::uint16_t be16(const std::uint8_t* p) { return static_cast<std::uint16_t>((p[0] << 8) | p[1]); }
std::uint32_t le32(const std::uint8_t* p) {
    return p[0] | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}
bool isLha(const std::uint8_t* b, std::size_t n) {
    return n >= 22 && (std::memcmp(b + 2, "-lh5-", 5) == 0 || std::memcmp(b + 2, "-lh0-", 5) == 0);
}
std::string readString(const std::uint8_t* b, std::size_t size, std::size_t& off) {
    const std::size_t start = off;
    while (off < size && b[off] != 0) ++off;
    if (off >= size) throw std::runtime_error("unterminated YM text field");
    std::string s(reinterpret_cast<const char*>(b + start), off - start);
    ++off;
    return s;
}
} // namespace

bool YmSong::looksLikeYm(const std::uint8_t* b, std::size_t n) noexcept {
    if (n >= 4 && b[0] == 'Y' && b[1] == 'M') return true;
    return isLha(b, n);
}

YmSong YmSong::parse(const std::uint8_t* bytes, std::size_t size) {
    if (!bytes || size < 8 || size > MaximumPackedBytes + 1024 * 1024)
        throw std::runtime_error("YM file size is invalid");
    std::vector<std::uint8_t> unpacked;
    if (isLha(bytes, size)) {
        const std::size_t headerSize = static_cast<std::size_t>(bytes[0]) + 2;
        if (bytes[20] != 0 || headerSize < 24 || headerSize > size)
            throw std::runtime_error("unsupported LHA header (level 0 required)");
        const std::uint32_t packed = le32(bytes + 7);
        const std::uint32_t original = le32(bytes + 11);
        if (packed > size - headerSize || original < 8 || original > MaximumUnpackedBytes)
            throw std::runtime_error("YM LHA member sizes are invalid or too large");
        if (std::memcmp(bytes + 2, "-lh0-", 5) == 0) {
            if (packed != original) throw std::runtime_error("YM LHA stored member is inconsistent");
            unpacked.assign(bytes + headerSize, bytes + headerSize + packed);
        } else {
            const char* error = nullptr;
            if (!decodeLh5(bytes + headerSize, packed, original, unpacked, &error))
                throw std::runtime_error(error ? error : "YM LHA5 decode failed");
        }
        bytes = unpacked.data();
        size = unpacked.size();
    }
    if (size < 8 || bytes[0] != 'Y' || bytes[1] != 'M')
        throw std::runtime_error("file is not a YM song");

    YmSong song;
    song.metadata_.format.assign(reinterpret_cast<const char*>(bytes), 4);
    const std::string& fmt = song.metadata_.format;
    if (fmt == "YM2!" || fmt == "YM3!" || fmt == "YM3b") {
        const bool loopTail = fmt == "YM3b";
        const std::size_t payload = size - 4 - (loopTail ? 4 : 0);
        if (size < (loopTail ? 8u : 4u) || payload % 14 != 0 || payload == 0)
            throw std::runtime_error("YM3 register data size is invalid");
        const std::size_t frames = payload / 14;
        song.data_.assign(frames * 16, 0);
        for (std::size_t reg = 0; reg < 14; ++reg)
            for (std::size_t f = 0; f < frames; ++f)
                song.data_[f * 16 + reg] = bytes[4 + reg * frames + f];
        song.frameCount_ = frames;
        if (loopTail) song.metadata_.loopFrame = be32(bytes + size - 4);
        song.metadata_.chipClockHz = 2000000;
        song.metadata_.frameRateHz = 50;
    } else if (fmt == "YM5!" || fmt == "YM6!") {
        if (size < 34 || std::memcmp(bytes + 4, "LeOnArD!", 8) != 0)
            throw std::runtime_error("YM5/YM6 header is invalid");
        const std::uint32_t frames = be32(bytes + 12);
        const std::uint32_t attributes = be32(bytes + 16);
        const std::uint16_t drums = be16(bytes + 20);
        song.metadata_.chipClockHz = be32(bytes + 22);
        song.metadata_.frameRateHz = be16(bytes + 26);
        song.metadata_.loopFrame = be32(bytes + 28);
        const std::uint16_t extra = be16(bytes + 32);
        std::size_t off = 34 + static_cast<std::size_t>(extra);
        if (frames == 0 || song.metadata_.chipClockHz == 0 || song.metadata_.frameRateHz == 0 ||
            off > size)
            throw std::runtime_error("YM5/YM6 header values are invalid");
        const bool planar = (attributes & 1) != 0;
        const bool signedDrums = (attributes & 2) != 0;
        const bool fourBitDrums = (attributes & 4) != 0;
        for (unsigned i = 0; i < drums; ++i) {
            if (off + 4 > size) throw std::runtime_error("YM digi-drum table is truncated");
            const std::uint32_t length = be32(bytes + off);
            off += 4;
            if (length > size - off) throw std::runtime_error("YM digi-drum is truncated");
            std::vector<std::int16_t> pcm(length);
            for (std::uint32_t k = 0; k < length; ++k) {
                const int v = bytes[off + k];
                pcm[k] = static_cast<std::int16_t>(fourBitDrums ? ((v & 15) - 8) * 2048
                    : signedDrums ? static_cast<std::int8_t>(v) * 128 : (v - 128) * 128);
            }
            song.drums_.push_back(std::move(pcm));
            off += length;
        }
        song.metadata_.title = readString(bytes, size, off);
        song.metadata_.author = readString(bytes, size, off);
        song.metadata_.comment = readString(bytes, size, off);
        // Some files carry only 14 registers per frame or lose their tail; derive both.
        std::size_t body = size - off;
        if (body >= 4 && std::memcmp(bytes + size - 4, "End!", 4) == 0) body -= 4;
        const std::size_t regs = body >= static_cast<std::size_t>(frames) * 16 - 16 ? 16 : 14;
        const std::size_t usable = body / regs;
        if (usable == 0) throw std::runtime_error("YM register data is truncated");
        const std::size_t count = usable < frames ? usable : frames;
        song.data_.assign(count * 16, 0);
        for (std::size_t f = 0; f < count; ++f)
            for (std::size_t reg = 0; reg < regs; ++reg)
                song.data_[f * 16 + reg] = planar ? bytes[off + reg * count + f]
                                                  : bytes[off + f * regs + reg];
        song.frameCount_ = count;
        song.effects_ = regs == 16;
    } else {
        throw std::runtime_error("unsupported YM variant (supported: YM2!, YM3!, YM3b, YM5!, YM6!; trackers YMT1/YMT2 and MIX are not)");
    }
    if (song.metadata_.loopFrame >= song.frameCount_) song.metadata_.hasLoop = false;
    return song;
}

YmSong YmSong::fromPlaneFile(YmMetadata metadata, std::vector<std::vector<std::int16_t>> drums,
                             std::shared_ptr<VtxPlaneFile> planes, std::size_t frameCount,
                             bool effects) {
    YmSong song;
    song.metadata_ = std::move(metadata);
    song.drums_ = std::move(drums);
    song.planes_ = std::move(planes);
    song.frameCount_ = frameCount;
    song.effects_ = effects;
    song.metadata_.hasLoop = song.metadata_.loopFrame < frameCount;
    return song;
}

bool YmSong::failed() const noexcept { return planes_ && planes_->failed(); }

std::shared_ptr<AudioPrefetcher> YmSong::prefetcher() const { return planes_; }

std::array<std::uint8_t, 16> YmSong::registers(std::size_t frame) const {
    if (frame >= frameCount_) throw std::out_of_range("YM frame index is out of range");
    if (planes_) return planes_->registers16(frame);
    std::array<std::uint8_t, 16> r{};
    std::memcpy(r.data(), data_.data() + frame * 16, 16);
    return r;
}

} // namespace ql32::music
