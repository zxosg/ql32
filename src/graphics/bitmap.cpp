#include "ql32/graphics/bitmap.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <utility>

namespace ql32::graphics {
namespace {

std::uint16_t little16(const std::uint8_t* bytes) noexcept {
    return static_cast<std::uint16_t>(bytes[0]) |
        static_cast<std::uint16_t>(bytes[1] << 8);
}

std::uint32_t little32(const std::uint8_t* bytes) noexcept {
    return static_cast<std::uint32_t>(bytes[0]) |
        (static_cast<std::uint32_t>(bytes[1]) << 8) |
        (static_cast<std::uint32_t>(bytes[2]) << 16) |
        (static_cast<std::uint32_t>(bytes[3]) << 24);
}

} // namespace

IndexedBitmap::IndexedBitmap(
        std::unique_ptr<runtime::StorageReadHandle> source,
        std::uint32_t width, std::uint32_t height, std::uint32_t stride,
        BitmapPalette palette)
    : source_(std::move(source)), width_(width), height_(height), stride_(stride),
      palette_(std::move(palette)) {}

std::unique_ptr<IndexedBitmap> IndexedBitmap::open(
        std::unique_ptr<runtime::StorageReadHandle> source) {
    if (!source || source->size() < HeaderSize ||
        source->size() > HeaderSize + MaximumPayloadBytes)
        throw std::invalid_argument("QIMG file size is outside supported bounds");

    std::array<std::uint8_t, HeaderSize> header{};
    if (source->readAt(0, header.data(), header.size()) != header.size())
        throw std::invalid_argument("QIMG header is truncated");
    if (header[0] != 'Q' || header[1] != 'I' || header[2] != 'M' || header[3] != 'G' ||
        header[4] != Version || header[5] != 4)
        throw std::invalid_argument("invalid QIMG header or pixel format");

    const auto width = little16(header.data() + 6);
    const auto height = little16(header.data() + 8);
    const auto stride = little16(header.data() + 10);
    const auto payloadSize = little32(header.data() + 12);
    BitmapPalette palette{};
    for (std::size_t index = 0; index < palette.size(); ++index) {
        const auto offset = 16 + index * 3;
        palette[index] = {header[offset], header[offset + 1], header[offset + 2]};
    }
    if (width == 0 || height == 0 || width > MaximumDimension ||
        height > MaximumDimension)
        throw std::invalid_argument("QIMG dimensions are outside supported bounds");
    const auto expectedStride = (static_cast<std::uint32_t>(width) + 1u) / 2u;
    const auto expectedPayload = static_cast<std::uint64_t>(expectedStride) * height;
    if (stride != expectedStride || payloadSize != expectedPayload ||
        payloadSize > MaximumPayloadBytes ||
        source->size() != HeaderSize + expectedPayload)
        throw std::invalid_argument("QIMG stride or payload size is invalid");

    return std::unique_ptr<IndexedBitmap>(
        new IndexedBitmap(std::move(source), width, height, stride,
                          std::move(palette)));
}

bool IndexedBitmap::readRow(std::uint32_t row,
                            std::vector<std::uint8_t>& pixels) const {
    if (row >= height_) return false;
    std::vector<std::uint8_t> packed(stride_);
    const auto offset = HeaderSize + static_cast<std::uint64_t>(row) * stride_;
    if (source_->readAt(offset, packed.data(), packed.size()) != packed.size())
        throw std::runtime_error("QIMG pixel data could not be read");
    pixels.resize(width_);
    for (std::uint32_t x = 0; x < width_; ++x) {
        const auto byte = packed[x / 2u];
        pixels[x] = static_cast<std::uint8_t>((x & 1u) == 0 ? byte >> 4 : byte & 0x0Fu);
    }
    return true;
}

BitmapRegistry::BitmapRegistry(std::shared_ptr<runtime::StorageService> storage)
    : storage_(std::move(storage)) {}

BitmapHandle BitmapRegistry::load(const std::string& path) {
    if (!storage_)
        throw runtime::ServiceError(runtime::ServiceErrorCode::Unavailable,
                                    "storage service unavailable");
    if (nextHandle_ == 0 || nextHandle_ == std::numeric_limits<BitmapHandle>::max())
        throw std::runtime_error("too many bitmap resources");
    auto bitmap = IndexedBitmap::open(storage_->openRead(path));
    const auto handle = nextHandle_++;
    loaded_.emplace(handle, std::move(bitmap));
    return handle;
}

void BitmapRegistry::unload(BitmapHandle handle) {
    if (loaded_.erase(handle) == 0)
        throw std::invalid_argument("unknown bitmap handle");
}

const IndexedBitmap& BitmapRegistry::get(BitmapHandle handle) const {
    const auto found = loaded_.find(handle);
    if (found == loaded_.end()) throw std::invalid_argument("unknown bitmap handle");
    return *found->second;
}

} // namespace ql32::graphics
