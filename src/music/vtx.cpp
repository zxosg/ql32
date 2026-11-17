#include "ql32/music/vtx.hpp"
#include "ql32/music/lh5.hpp"

#include <stdexcept>
#include <string_view>

namespace ql32::music {
namespace {
std::uint16_t le16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0] | (static_cast<unsigned>(p[1]) << 8));
}
std::uint32_t le32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) |
        (static_cast<std::uint32_t>(p[1]) << 8) |
        (static_cast<std::uint32_t>(p[2]) << 16) |
        (static_cast<std::uint32_t>(p[3]) << 24);
}
std::string readString(const std::uint8_t* bytes, std::size_t size,
                       std::size_t& offset) {
    const std::size_t start = offset;
    while (offset < size && bytes[offset] != 0) ++offset;
    if (offset == size) throw std::runtime_error("unterminated VTX metadata string");
    std::string result(reinterpret_cast<const char*>(bytes + start), offset - start);
    ++offset;
    return result;
}
} // namespace

VtxSong VtxSong::parse(const std::uint8_t* bytes, std::size_t size) {
    if (!bytes || size < 20 || size > MaximumFileBytes)
        throw std::runtime_error("VTX file size is invalid or exceeds 64 KiB");
    const std::string_view signature(reinterpret_cast<const char*>(bytes), 2);
    if (signature != "ay" && signature != "ym")
        throw std::runtime_error("file is not a VTX AY/YM stream");

    VtxSong song;
    song.metadata_.ym2149 = signature == "ym";
    song.metadata_.stereoMode = bytes[2] & 7;
    song.metadata_.loopFrame = le16(bytes + 3);
    song.metadata_.chipClockHz = le32(bytes + 5);
    song.metadata_.playerHz = bytes[9];
    const std::uint32_t declaredRegisterBytes = le32(bytes + 12);
    if (song.metadata_.chipClockHz == 0 || song.metadata_.playerHz == 0 ||
        declaredRegisterBytes == 0 || declaredRegisterBytes > MaximumRegisterBytes ||
        declaredRegisterBytes % 14 != 0)
        throw std::runtime_error("VTX register stream header is invalid");
    // Parse the five consecutive NUL-terminated metadata fields without
    // retaining a cursor allocation on the embedded target.
    std::size_t offset = 16;
    song.metadata_.title = readString(bytes, size, offset);
    song.metadata_.author = readString(bytes, size, offset);
    song.metadata_.source = readString(bytes, size, offset);
    song.metadata_.editor = readString(bytes, size, offset);
    song.metadata_.comment = readString(bytes, size, offset);
    if (offset >= size) throw std::runtime_error("VTX file has no LHA member");

    const std::uint8_t* compressed = bytes + offset;
    std::size_t compressedSize = size - offset;

    // VTX files commonly store the raw LH5 bitstream after the metadata.
    // Also accept a complete LHA member for files produced without stripping
    // the archive wrapper.
    if (compressedSize >= 7 &&
        std::string_view(reinterpret_cast<const char*>(compressed + 2), 5) == "-lh5-") {
        if (compressedSize < 24)
            throw std::runtime_error("VTX LHA member header is truncated");
        const std::size_t headerSize = static_cast<std::size_t>(compressed[0]) + 2;
        if (headerSize < 24 || headerSize > compressedSize)
            throw std::runtime_error("VTX LHA member header length is invalid");
        const std::uint32_t packedSize = le32(compressed + 7);
        const std::uint32_t unpackedSize = le32(compressed + 11);
        if (unpackedSize != declaredRegisterBytes ||
            packedSize > compressedSize - headerSize)
            throw std::runtime_error("VTX LHA member sizes do not match the VTX header");
        compressed += headerSize;
        compressedSize = packedSize;
    }

    const char* decodeError = nullptr;
    if (!decodeLh5(compressed, compressedSize, declaredRegisterBytes,
                   song.registers_, &decodeError))
        throw std::runtime_error(decodeError ? decodeError : "VTX LHA5 decode failed");
    song.frameCount_ = declaredRegisterBytes / 14;
    if (song.frameCount_ == 0) throw std::runtime_error("VTX has no register frames");
    return song;
}

std::array<std::uint8_t, 14> VtxSong::registers(std::size_t frame) const {
    if (frame >= frameCount_) throw std::out_of_range("VTX frame index is out of range");
    // YM3/VTX register data is stored as fourteen register planes.
    std::array<std::uint8_t, 14> result{};
    for (std::size_t reg = 0; reg < result.size(); ++reg)
        result[reg] = registers_[reg * frameCount_ + frame];
    return result;
}

} // namespace ql32::music
