#include "ql32/graphics/window_manager.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace ql32::graphics {
namespace {

std::size_t checkedPixelCount(std::uint32_t width, std::uint32_t height) {
    if (width == 0 || height == 0)
        throw std::invalid_argument("canvas dimensions must be positive");
    if (width > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()) ||
        height > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()))
        throw std::out_of_range("canvas dimensions exceed supported coordinates");
    const auto count = static_cast<std::uint64_t>(width) * height;
    if (count > std::numeric_limits<std::size_t>::max())
        throw std::length_error("canvas exceeds addressable memory");
    return static_cast<std::size_t>(count);
}

std::uint16_t colorCount(PixelDepth depth) {
    switch (depth) {
    case PixelDepth::One: return 2;
    case PixelDepth::Four: return 16;
    case PixelDepth::Eight: return 256;
    }
    throw std::invalid_argument("pixel depth must be 1, 4, or 8 bits");
}

std::size_t checkedCanvasBytes(std::uint32_t width, std::uint32_t height,
                               PixelDepth depth) {
    const std::uint64_t pixels = checkedPixelCount(width, height);
    const auto bits = static_cast<std::uint8_t>(depth);
    (void)colorCount(depth);
    if (pixels > (std::numeric_limits<std::uint64_t>::max() - 7) / bits)
        throw std::length_error("canvas exceeds addressable memory");
    const std::uint64_t bytes = (pixels * bits + 7) / 8;
    if (bytes > std::numeric_limits<std::size_t>::max())
        throw std::length_error("canvas exceeds addressable memory");
    return static_cast<std::size_t>(bytes);
}

ColorIndex colorMask(PixelDepth depth) {
    return static_cast<ColorIndex>(colorCount(depth) - 1);
}

std::array<std::uint8_t, 5> glyph(char character) {
    switch (character) {
    case 'A': return {0x7E, 0x11, 0x11, 0x11, 0x7E};
    case 'B': return {0x7F, 0x49, 0x49, 0x49, 0x36};
    case 'C': return {0x3E, 0x41, 0x41, 0x41, 0x22};
    case 'D': return {0x7F, 0x41, 0x41, 0x22, 0x1C};
    case 'E': return {0x7F, 0x49, 0x49, 0x49, 0x41};
    case 'F': return {0x7F, 0x09, 0x09, 0x09, 0x01};
    case 'G': return {0x3E, 0x41, 0x49, 0x49, 0x7A};
    case 'H': return {0x7F, 0x08, 0x08, 0x08, 0x7F};
    case 'I': return {0x00, 0x41, 0x7F, 0x41, 0x00};
    case 'J': return {0x20, 0x40, 0x41, 0x3F, 0x01};
    case 'K': return {0x7F, 0x08, 0x14, 0x22, 0x41};
    case 'L': return {0x7F, 0x40, 0x40, 0x40, 0x40};
    case 'M': return {0x7F, 0x02, 0x0C, 0x02, 0x7F};
    case 'N': return {0x7F, 0x04, 0x08, 0x10, 0x7F};
    case 'O': return {0x3E, 0x41, 0x41, 0x41, 0x3E};
    case 'P': return {0x7F, 0x09, 0x09, 0x09, 0x06};
    case 'Q': return {0x3E, 0x41, 0x51, 0x21, 0x5E};
    case 'R': return {0x7F, 0x09, 0x19, 0x29, 0x46};
    case 'S': return {0x46, 0x49, 0x49, 0x49, 0x31};
    case 'T': return {0x01, 0x01, 0x7F, 0x01, 0x01};
    case 'U': return {0x3F, 0x40, 0x40, 0x40, 0x3F};
    case 'V': return {0x1F, 0x20, 0x40, 0x20, 0x1F};
    case 'W': return {0x3F, 0x40, 0x38, 0x40, 0x3F};
    case 'X': return {0x63, 0x14, 0x08, 0x14, 0x63};
    case 'Y': return {0x07, 0x08, 0x70, 0x08, 0x07};
    case 'Z': return {0x61, 0x51, 0x49, 0x45, 0x43};
    case 'a': return {0x20, 0x54, 0x54, 0x54, 0x78};
    case 'b': return {0x7F, 0x48, 0x44, 0x44, 0x38};
    case 'c': return {0x38, 0x44, 0x44, 0x44, 0x20};
    case 'd': return {0x38, 0x44, 0x44, 0x48, 0x7F};
    case 'e': return {0x38, 0x54, 0x54, 0x54, 0x18};
    case 'f': return {0x08, 0x7E, 0x09, 0x01, 0x02};
    case 'g': return {0x0C, 0x52, 0x52, 0x52, 0x3E};
    case 'h': return {0x7F, 0x08, 0x04, 0x04, 0x78};
    case 'i': return {0x00, 0x44, 0x7D, 0x40, 0x00};
    case 'j': return {0x20, 0x40, 0x44, 0x3D, 0x00};
    case 'k': return {0x7F, 0x10, 0x28, 0x44, 0x00};
    case 'l': return {0x00, 0x41, 0x7F, 0x40, 0x00};
    case 'm': return {0x7C, 0x04, 0x18, 0x04, 0x78};
    case 'n': return {0x7C, 0x08, 0x04, 0x04, 0x78};
    case 'o': return {0x38, 0x44, 0x44, 0x44, 0x38};
    case 'p': return {0x7C, 0x14, 0x14, 0x14, 0x08};
    case 'q': return {0x08, 0x14, 0x14, 0x18, 0x7C};
    case 'r': return {0x7C, 0x08, 0x04, 0x04, 0x08};
    case 's': return {0x48, 0x54, 0x54, 0x54, 0x20};
    case 't': return {0x04, 0x3F, 0x44, 0x40, 0x20};
    case 'u': return {0x3C, 0x40, 0x40, 0x20, 0x7C};
    case 'v': return {0x1C, 0x20, 0x40, 0x20, 0x1C};
    case 'w': return {0x3C, 0x40, 0x30, 0x40, 0x3C};
    case 'x': return {0x44, 0x28, 0x10, 0x28, 0x44};
    case 'y': return {0x0C, 0x50, 0x50, 0x50, 0x3C};
    case 'z': return {0x44, 0x64, 0x54, 0x4C, 0x44};
    case '0': return {0x3E, 0x51, 0x49, 0x45, 0x3E};
    case '1': return {0x00, 0x42, 0x7F, 0x40, 0x00};
    case '2': return {0x42, 0x61, 0x51, 0x49, 0x46};
    case '3': return {0x21, 0x41, 0x45, 0x4B, 0x31};
    case '4': return {0x18, 0x14, 0x12, 0x7F, 0x10};
    case '5': return {0x27, 0x45, 0x45, 0x45, 0x39};
    case '6': return {0x3C, 0x4A, 0x49, 0x49, 0x30};
    case '7': return {0x01, 0x71, 0x09, 0x05, 0x03};
    case '8': return {0x36, 0x49, 0x49, 0x49, 0x36};
    case '9': return {0x06, 0x49, 0x49, 0x29, 0x1E};
    case '.': return {0x00, 0x60, 0x60, 0x00, 0x00};
    case ',': return {0x00, 0x40, 0x20, 0x00, 0x00};
    case ':': return {0x00, 0x36, 0x36, 0x00, 0x00};
    case ';': return {0x00, 0x56, 0x36, 0x00, 0x00};
    case '!': return {0x00, 0x00, 0x5F, 0x00, 0x00};
    case '?': return {0x02, 0x01, 0x51, 0x09, 0x06};
    case '-': return {0x08, 0x08, 0x08, 0x08, 0x08};
    case '_': return {0x40, 0x40, 0x40, 0x40, 0x40};
    case '+': return {0x08, 0x08, 0x3E, 0x08, 0x08};
    case '=': return {0x14, 0x14, 0x14, 0x14, 0x14};
    case '/': return {0x20, 0x10, 0x08, 0x04, 0x02};
    case '\\': return {0x02, 0x04, 0x08, 0x10, 0x20};
    case '(': return {0x00, 0x1C, 0x22, 0x41, 0x00};
    case ')': return {0x00, 0x41, 0x22, 0x1C, 0x00};
    case '[': return {0x00, 0x7F, 0x41, 0x41, 0x00};
    case ']': return {0x00, 0x41, 0x41, 0x7F, 0x00};
    case '<': return {0x08, 0x14, 0x22, 0x41, 0x00};
    case '>': return {0x00, 0x41, 0x22, 0x14, 0x08};
    case '"': return {0x00, 0x07, 0x00, 0x07, 0x00};
    case '\'': return {0x00, 0x05, 0x03, 0x00, 0x00};
    case '*': return {0x14, 0x08, 0x3E, 0x08, 0x14};
    case '#': return {0x14, 0x7F, 0x14, 0x7F, 0x14};
    case ' ': return {0, 0, 0, 0, 0};
    default: return {0x7F, 0x41, 0x5D, 0x41, 0x7F};
    }
}

} // namespace

CanvasArena::Block& CanvasArena::find(std::uint64_t handle) {
    const auto found = std::find_if(blocks_.begin(), blocks_.end(),
        [handle](const Block& block) { return block.handle == handle; });
    if (found == blocks_.end()) throw std::out_of_range("unknown canvas arena handle");
    return *found;
}

const CanvasArena::Block& CanvasArena::find(std::uint64_t handle) const {
    const auto found = std::find_if(blocks_.begin(), blocks_.end(),
        [handle](const Block& block) { return block.handle == handle; });
    if (found == blocks_.end()) throw std::out_of_range("unknown canvas arena handle");
    return *found;
}

void CanvasArena::compact(std::uint64_t lastHandle) noexcept {
    std::sort(blocks_.begin(), blocks_.end(), [lastHandle](const Block& a, const Block& b) {
        if (a.handle == lastHandle) return false;
        if (b.handle == lastHandle) return true;
        return a.offset < b.offset;
    });
    std::size_t offset = 0;
    for (auto& block : blocks_) {
        if (block.offset != offset) {
            std::memmove(bytes_.data() + offset, bytes_.data() + block.offset, block.size);
            block.offset = offset;
        }
        offset += block.size;
    }
}

std::uint64_t CanvasArena::allocate(std::size_t bytes) {
    if (bytes == 0 || bytes > maximumBytes_)
        throw std::runtime_error("window manager canvas memory limit reached");
    compact();
    const auto used = bytes_.size();
    if (bytes > maximumBytes_ - used)
        throw std::runtime_error("window manager canvas memory limit reached");
    if (nextHandle_ == 0) throw std::overflow_error("canvas arena handles exhausted");
    const auto handle = nextHandle_++;
    blocks_.push_back({handle, used, bytes});
    try {
        bytes_.resize(used + bytes, 0);
    } catch (...) {
        blocks_.pop_back();
        throw;
    }
    return handle;
}

void CanvasArena::release(std::uint64_t handle) noexcept {
    const auto found = std::find_if(blocks_.begin(), blocks_.end(),
        [handle](const Block& block) { return block.handle == handle; });
    if (found == blocks_.end()) return;
    blocks_.erase(found);
    compact();
    if (blocks_.empty()) {
        std::vector<std::uint8_t>().swap(bytes_);
    } else {
        std::size_t used = 0;
        for (const auto& block : blocks_) used += block.size;
        bytes_.resize(used);
    }
}

void CanvasArena::resize(std::uint64_t handle, std::size_t bytes) {
    (void)find(handle);
    if (bytes == 0) throw std::runtime_error("canvas dimensions must be positive");
    if (bytes > maximumBytes_) throw std::runtime_error("window manager canvas memory limit reached");
    compact(handle);
    auto& target = find(handle);
    const auto oldSize = target.size;
    const auto used = bytes_.size();
    const auto otherBytes = used - oldSize;
    if (bytes > maximumBytes_ - otherBytes)
        throw std::runtime_error("window manager canvas memory limit reached");
    bytes_.resize(otherBytes + bytes, 0);
    target.offset = otherBytes;
    target.size = bytes;
}

std::uint8_t* CanvasArena::data(std::uint64_t handle) noexcept {
    const auto found = std::find_if(blocks_.begin(), blocks_.end(),
        [handle](const Block& block) { return block.handle == handle; });
    return found == blocks_.end() ? nullptr : bytes_.data() + found->offset;
}

const std::uint8_t* CanvasArena::data(std::uint64_t handle) const noexcept {
    const auto found = std::find_if(blocks_.begin(), blocks_.end(),
        [handle](const Block& block) { return block.handle == handle; });
    return found == blocks_.end() ? nullptr : bytes_.data() + found->offset;
}

std::size_t CanvasArena::size(std::uint64_t handle) const noexcept {
    const auto found = std::find_if(blocks_.begin(), blocks_.end(),
        [handle](const Block& block) { return block.handle == handle; });
    return found == blocks_.end() ? 0 : found->size;
}

IndexedCanvas::IndexedCanvas(std::uint32_t width, std::uint32_t height, PixelDepth depth)
    : IndexedCanvas(width, height, depth, nullptr) {}

IndexedCanvas::IndexedCanvas(std::uint32_t width, std::uint32_t height, PixelDepth depth,
                             CanvasArena* arena)
    : width_(width), height_(height), depth_(depth),
      ink_(std::min<ColorIndex>(7, colorMask(depth))), arena_(arena) {
    const auto bytes = checkedCanvasBytes(width, height, depth);
    if (arena_) arenaHandle_ = arena_->allocate(bytes);
    else pixels_.assign(bytes, 0);
}

IndexedCanvas::~IndexedCanvas() {
    if (arena_ && arenaHandle_) arena_->release(arenaHandle_);
}

IndexedCanvas::IndexedCanvas(IndexedCanvas&& other) noexcept
    : width_(other.width_), height_(other.height_), depth_(other.depth_),
      ink_(other.ink_), paper_(other.paper_), arena_(other.arena_),
      arenaHandle_(other.arenaHandle_), pixels_(std::move(other.pixels_)) {
    other.arena_ = nullptr;
    other.arenaHandle_ = 0;
}

IndexedCanvas& IndexedCanvas::operator=(IndexedCanvas&& other) noexcept {
    if (this == &other) return *this;
    if (arena_ && arenaHandle_) arena_->release(arenaHandle_);
    width_ = other.width_;
    height_ = other.height_;
    depth_ = other.depth_;
    ink_ = other.ink_;
    paper_ = other.paper_;
    arena_ = other.arena_;
    arenaHandle_ = other.arenaHandle_;
    pixels_ = std::move(other.pixels_);
    other.arena_ = nullptr;
    other.arenaHandle_ = 0;
    return *this;
}

std::uint8_t* IndexedCanvas::pixelData() noexcept {
    return arena_ ? arena_->data(arenaHandle_) : pixels_.data();
}

const std::uint8_t* IndexedCanvas::pixelData() const noexcept {
    return arena_ ? arena_->data(arenaHandle_) : pixels_.data();
}

std::size_t IndexedCanvas::storageBytes() const noexcept {
    return arena_ ? arena_->size(arenaHandle_) : pixels_.size();
}

std::uint16_t IndexedCanvas::paletteSize() const noexcept {
    return static_cast<std::uint16_t>(colorMask(depth_)) + 1;
}

ColorIndex IndexedCanvas::readPixel(std::size_t index) const noexcept {
    switch (depth_) {
    case PixelDepth::One: {
        const auto shift = 7u - static_cast<unsigned>(index & 7u);
        return static_cast<ColorIndex>((pixelData()[index >> 3u] >> shift) & 1u);
    }
    case PixelDepth::Four: {
        const auto shift = (index & 1u) == 0 ? 4u : 0u;
        return static_cast<ColorIndex>((pixelData()[index >> 1u] >> shift) & 0x0Fu);
    }
    case PixelDepth::Eight:
        return pixelData()[index];
    }
    return 0;
}

void IndexedCanvas::writePixel(std::size_t index, ColorIndex color) noexcept {
    switch (depth_) {
    case PixelDepth::One: {
        const auto shift = 7u - static_cast<unsigned>(index & 7u);
        const auto mask = static_cast<ColorIndex>(1u << shift);
        pixelData()[index >> 3u] = static_cast<ColorIndex>(
            (pixelData()[index >> 3u] & static_cast<ColorIndex>(~mask)) |
            static_cast<ColorIndex>((color & 1u) << shift));
        break;
    }
    case PixelDepth::Four: {
        const auto shift = (index & 1u) == 0 ? 4u : 0u;
        const auto mask = static_cast<ColorIndex>(0x0Fu << shift);
        pixelData()[index >> 1u] = static_cast<ColorIndex>(
            (pixelData()[index >> 1u] & static_cast<ColorIndex>(~mask)) |
            static_cast<ColorIndex>((color & 0x0Fu) << shift));
        break;
    }
    case PixelDepth::Eight:
        pixelData()[index] = color;
        break;
    }
}

void IndexedCanvas::setDepth(PixelDepth depth) {
    if (depth == depth_) return;
    (void)colorCount(depth);
    std::vector<ColorIndex> converted(checkedCanvasBytes(width_, height_, depth), 0);
    const auto mask = colorMask(depth);
    for (std::size_t index = 0; index < static_cast<std::size_t>(width_) * height_; ++index) {
        const auto color = static_cast<ColorIndex>(readPixel(index) & mask);
        switch (depth) {
        case PixelDepth::One: {
            const auto shift = 7u - static_cast<unsigned>(index & 7u);
            converted[index >> 3u] |= static_cast<ColorIndex>((color & 1u) << shift);
            break;
        }
        case PixelDepth::Four: {
            const auto shift = (index & 1u) == 0 ? 4u : 0u;
            converted[index >> 1u] |= static_cast<ColorIndex>((color & 0x0Fu) << shift);
            break;
        }
        case PixelDepth::Eight:
            converted[index] = color;
            break;
        }
    }
    if (arena_) {
        arena_->resize(arenaHandle_, converted.size());
        std::memcpy(pixelData(), converted.data(), converted.size());
    } else {
        pixels_.swap(converted);
    }
    depth_ = depth;
    ink_ &= mask;
    paper_ &= mask;
}

void IndexedCanvas::setInk(ColorIndex color) {
    if (color >= paletteSize()) throw std::out_of_range("ink index exceeds window palette");
    ink_ = color;
}

void IndexedCanvas::setPaper(ColorIndex color) {
    if (color >= paletteSize()) throw std::out_of_range("paper index exceeds window palette");
    paper_ = color;
}

void IndexedCanvas::clear() {
    if (depth_ == PixelDepth::Eight) {
        std::fill(pixelData(), pixelData() + storageBytes(), paper_);
    } else if (depth_ == PixelDepth::Four) {
        std::fill(pixelData(), pixelData() + storageBytes(),
                  static_cast<ColorIndex>((paper_ << 4u) | paper_));
    } else {
        std::fill(pixelData(), pixelData() + storageBytes(), paper_ ? 0xFFu : 0x00u);
    }
}

void IndexedCanvas::scrollUp(std::uint32_t pixelRows) {
    if (pixelRows == 0) return;
    if (pixelRows >= height_) {
        clear();
        return;
    }
    const auto remaining = height_ - pixelRows;
    for (std::uint32_t y = 0; y < remaining; ++y)
        for (std::uint32_t x = 0; x < width_; ++x) {
            const auto source = static_cast<std::size_t>(y + pixelRows) * width_ + x;
            const auto target = static_cast<std::size_t>(y) * width_ + x;
            writePixel(target, readPixel(source));
        }
    for (std::uint32_t y = remaining; y < height_; ++y)
        for (std::uint32_t x = 0; x < width_; ++x)
            writePixel(static_cast<std::size_t>(y) * width_ + x, paper_);
}

void IndexedCanvas::setPixel(std::int32_t x, std::int32_t y, ColorIndex color) {
    if (color >= paletteSize()) throw std::out_of_range("pixel index exceeds window palette");
    if (x < 0 || y < 0 || static_cast<std::uint32_t>(x) >= width_ ||
        static_cast<std::uint32_t>(y) >= height_) return;
    writePixel(static_cast<std::size_t>(y) * width_ + static_cast<std::uint32_t>(x), color);
}

ColorIndex IndexedCanvas::pixel(std::uint32_t x, std::uint32_t y) const {
    if (x >= width_ || y >= height_) throw std::out_of_range("canvas pixel is outside bounds");
    return readPixel(static_cast<std::size_t>(y) * width_ + x);
}

void IndexedCanvas::line(Point from, Point to) {
    long double x0 = from.x;
    long double y0 = from.y;
    const long double clipDx = static_cast<long double>(to.x) - x0;
    const long double clipDy = static_cast<long double>(to.y) - y0;
    long double first = 0;
    long double last = 1;
    const auto clipTest = [&](long double p, long double q) {
        if (p == 0) return q >= 0;
        const long double ratio = q / p;
        if (p < 0) {
            if (ratio > last) return false;
            first = std::max(first, ratio);
        } else {
            if (ratio < first) return false;
            last = std::min(last, ratio);
        }
        return true;
    };
    if (!clipTest(-clipDx, x0) ||
        !clipTest(clipDx, static_cast<long double>(width_ - 1) - x0) ||
        !clipTest(-clipDy, y0) ||
        !clipTest(clipDy, static_cast<long double>(height_ - 1) - y0)) return;
    const auto endX = static_cast<std::int64_t>(std::llround(x0 + last * clipDx));
    const auto endY = static_cast<std::int64_t>(std::llround(y0 + last * clipDy));
    std::int64_t x = static_cast<std::int64_t>(std::llround(x0 + first * clipDx));
    std::int64_t y = static_cast<std::int64_t>(std::llround(y0 + first * clipDy));
    const std::int64_t dx = std::llabs(endX - x);
    const std::int64_t sx = x < endX ? 1 : -1;
    const std::int64_t dy = -std::llabs(endY - y);
    const std::int64_t sy = y < endY ? 1 : -1;
    std::int64_t error = dx + dy;
    for (;;) {
        if (x >= 0 && y >= 0 && x < width_ && y < height_)
            writePixel(static_cast<std::size_t>(y) * width_ + static_cast<std::size_t>(x), ink_);
        if (x == endX && y == endY) break;
        const std::int64_t twiceError = error * 2;
        if (twiceError >= dy) { error += dy; x += sx; }
        if (twiceError <= dx) { error += dx; y += sy; }
    }
}

void IndexedCanvas::rectangle(Point topLeft, std::int32_t width, std::int32_t height,
                              bool filled) {
    if (width <= 0 || height <= 0)
        throw std::invalid_argument("rectangle dimensions must be positive");
    const auto right = static_cast<std::int64_t>(topLeft.x) + width - 1;
    const auto bottom = static_cast<std::int64_t>(topLeft.y) + height - 1;
    if (right > std::numeric_limits<std::int32_t>::max() ||
        bottom > std::numeric_limits<std::int32_t>::max())
        throw std::out_of_range("rectangle exceeds supported coordinates");
    if (filled) {
        const auto left = std::max<std::int64_t>(0, topLeft.x);
        const auto clippedRight = std::min<std::int64_t>(width_ - 1, right);
        const auto top = std::max<std::int64_t>(0, topLeft.y);
        const auto clippedBottom = std::min<std::int64_t>(height_ - 1, bottom);
        for (auto y = top; y <= clippedBottom; ++y)
            for (auto x = left; x <= clippedRight; ++x)
                setPixel(static_cast<std::int32_t>(x), static_cast<std::int32_t>(y), ink_);
        return;
    }
    line(topLeft, {static_cast<std::int32_t>(right), topLeft.y});
    line({static_cast<std::int32_t>(right), topLeft.y},
         {static_cast<std::int32_t>(right), static_cast<std::int32_t>(bottom)});
    line({static_cast<std::int32_t>(right), static_cast<std::int32_t>(bottom)},
         {topLeft.x, static_cast<std::int32_t>(bottom)});
    line({topLeft.x, static_cast<std::int32_t>(bottom)}, topLeft);
}

void IndexedCanvas::circle(Point center, std::int32_t radius, bool filled) {
    if (radius < 0) throw std::invalid_argument("circle radius must be nonnegative");
    if (filled) {
        const auto top = std::max<std::int64_t>(0,
            static_cast<std::int64_t>(center.y) - radius);
        const auto bottom = std::min<std::int64_t>(height_ - 1,
            static_cast<std::int64_t>(center.y) + radius);
        const auto radiusSquared = static_cast<std::int64_t>(radius) * radius;
        for (auto y = top; y <= bottom; ++y) {
            const auto dy = y - center.y;
            const auto remaining = radiusSquared - dy * dy;
            if (remaining < 0) continue;
            auto span = static_cast<std::int64_t>(std::sqrt(
                static_cast<long double>(remaining)));
            while ((span + 1) * (span + 1) <= remaining) ++span;
            while (span * span > remaining) --span;
            const auto left = std::max<std::int64_t>(0,
                static_cast<std::int64_t>(center.x) - span);
            const auto right = std::min<std::int64_t>(width_ - 1,
                static_cast<std::int64_t>(center.x) + span);
            for (auto x = left; x <= right; ++x)
                setPixel(static_cast<std::int32_t>(x), static_cast<std::int32_t>(y), ink_);
        }
        return;
    }
    std::int64_t x = radius;
    std::int64_t y = 0;
    std::int64_t error = 1 - x;
    while (x >= y) {
        const std::int64_t points[8][2] = {
            {center.x + x, center.y + y}, {center.x + y, center.y + x},
            {center.x - y, center.y + x}, {center.x - x, center.y + y},
            {center.x - x, center.y - y}, {center.x - y, center.y - x},
            {center.x + y, center.y - x}, {center.x + x, center.y - y}
        };
        for (const auto& point : points) {
            if (point[0] >= 0 && point[1] >= 0 && point[0] < width_ && point[1] < height_)
                writePixel(static_cast<std::size_t>(point[1]) * width_ +
                           static_cast<std::size_t>(point[0]), ink_);
        }
        ++y;
        if (error < 0) error += 2 * y + 1;
        else { --x; error += 2 * (y - x + 1); }
    }
}

void IndexedCanvas::text(Point topLeft, const std::string& value) {
    std::int64_t originX = topLeft.x;
    std::int64_t x = originX;
    std::int64_t y = topLeft.y;
    for (const unsigned char raw : value) {
        if (raw == '\n') {
            x = originX;
            y += 8;
            continue;
        }
        if (raw == '\r') continue;
        const auto character = raw < 128 ? static_cast<char>(raw) : '?';
        const auto pattern = glyph(character);
        for (std::int64_t row = 0; row < 7; ++row) {
            for (std::int64_t column = 0; column < 5; ++column) {
                const auto px = x + column;
                const auto py = y + row;
                if (px < 0 || py < 0 || px >= width_ || py >= height_) continue;
                const bool set = (pattern[static_cast<std::size_t>(column)] & (1u << row)) != 0;
                writePixel(static_cast<std::size_t>(py) * width_ +
                           static_cast<std::size_t>(px), set ? ink_ : paper_);
            }
        }
        x += 6;
    }
}

void IndexedCanvas::resize(std::uint32_t width, std::uint32_t height) {
    if (width > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()) ||
        height > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()))
        throw std::out_of_range("canvas dimensions exceed supported coordinates");
    if (width == width_ && height == height_) return;

    const auto newBytes = checkedCanvasBytes(width, height, depth_);
    if (!arena_ && newBytes <= pixels_.capacity()) {
        const auto oldWidth = width_;
        const auto oldHeight = height_;
        const auto copyWidth = std::min(oldWidth, width);
        const auto copyHeight = std::min(oldHeight, height);
        if (newBytes > pixels_.size()) pixels_.resize(newBytes, 0);

        // Move overlapping rows in the safe direction when the row stride
        // changes. This lets orientation swaps reuse the existing canvas
        // allocation instead of requiring a second full-size buffer.
        if (width > oldWidth) {
            for (std::uint32_t row = copyHeight; row > 0; --row)
                for (std::uint32_t column = copyWidth; column > 0; --column) {
                    const auto source = static_cast<std::size_t>(row - 1) * oldWidth + column - 1;
                    const auto destination = static_cast<std::size_t>(row - 1) * width + column - 1;
                    writePixel(destination, readPixel(source));
                }
        } else {
            for (std::uint32_t row = 0; row < copyHeight; ++row)
                for (std::uint32_t column = 0; column < copyWidth; ++column) {
                    const auto source = static_cast<std::size_t>(row) * oldWidth + column;
                    const auto destination = static_cast<std::size_t>(row) * width + column;
                    writePixel(destination, readPixel(source));
                }
        }

        width_ = width;
        height_ = height;
        for (std::uint32_t row = 0; row < height; ++row)
            for (std::uint32_t column = 0; column < width; ++column)
                if (row >= copyHeight || column >= copyWidth)
                    writePixel(static_cast<std::size_t>(row) * width + column, paper_);
        if (newBytes < pixels_.size()) pixels_.resize(newBytes);
        return;
    }

    IndexedCanvas resized(width, height, depth_);
    resized.setInk(ink_);
    resized.setPaper(paper_);
    resized.clear();
    const auto copyWidth = std::min(width_, width);
    const auto copyHeight = std::min(height_, height);
    for (std::uint32_t row = 0; row < copyHeight; ++row)
        for (std::uint32_t column = 0; column < copyWidth; ++column) {
            const auto color = readPixel(static_cast<std::size_t>(row) * width_ + column);
            resized.writePixel(static_cast<std::size_t>(row) * width + column, color);
        }
    if (arena_) {
        arena_->resize(arenaHandle_, newBytes);
        std::memcpy(pixelData(), resized.pixels_.data(), newBytes);
    } else {
        pixels_.swap(resized.pixels_);
    }
    width_ = width;
    height_ = height;
}

WindowManager::WindowManager(std::uint32_t screenWidth, std::uint32_t screenHeight,
                             std::size_t maximumWindows,
                             std::size_t maximumCanvasBytes)
    : screenWidth_(screenWidth), screenHeight_(screenHeight),
      maximumWindows_(maximumWindows), maximumCanvasBytes_(maximumCanvasBytes),
      arena_(maximumCanvasBytes) {
    if (screenWidth_ == 0 || screenHeight_ == 0)
        throw std::invalid_argument("window manager screen dimensions must be positive");
    if (screenWidth_ > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()) ||
        screenHeight_ > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()))
        throw std::out_of_range("window manager screen exceeds supported coordinates");
    if (maximumWindows_ == 0)
        throw std::invalid_argument("window manager maximum window count must be positive");
}

void WindowManager::validate(Rect bounds) {
    if (bounds.width == 0 || bounds.height == 0)
        throw std::invalid_argument("window dimensions must be positive");
    const auto right = static_cast<std::int64_t>(bounds.x) + bounds.width;
    const auto bottom = static_cast<std::int64_t>(bounds.y) + bounds.height;
    if (right > std::numeric_limits<std::int32_t>::max() ||
        bottom > std::numeric_limits<std::int32_t>::max())
        throw std::out_of_range("window bounds exceed supported coordinates");
}

WindowId WindowManager::create(SessionId owner, Rect bounds, PixelDepth depth) {
    if (owner == 0) throw std::invalid_argument("window owner must be a valid session");
    validate(bounds);
    if (windows_.size() >= maximumWindows_)
        throw std::runtime_error("window manager window limit reached");
    const auto newBytes = checkedCanvasBytes(bounds.width, bounds.height, depth);
    if (maximumCanvasBytes_ != 0) {
        std::size_t allocated = 0;
        for (const auto& entry : windows_) {
            const auto bytes = entry.canvas.storageBytes();
            if (bytes > maximumCanvasBytes_ - allocated)
                throw std::runtime_error("window manager canvas memory limit reached");
            allocated += bytes;
        }
        if (newBytes > maximumCanvasBytes_ - allocated)
            throw std::runtime_error("window manager canvas memory limit reached");
    }
    if (nextId_ == 0) throw std::overflow_error("window identifiers exhausted");
    const WindowId id = nextId_++;
    windows_.push_back({id, owner, bounds, true,
                        IndexedCanvas(bounds.width, bounds.height, depth, &arena_)});
    return id;
}

WindowManager::Entry& WindowManager::find(WindowId id) {
    const auto found = std::find_if(windows_.begin(), windows_.end(),
                                    [id](const Entry& item) { return item.id == id; });
    if (found == windows_.end()) throw std::out_of_range("unknown window identifier");
    return *found;
}

const WindowManager::Entry& WindowManager::find(WindowId id) const {
    const auto found = std::find_if(windows_.begin(), windows_.end(),
                                    [id](const Entry& item) { return item.id == id; });
    if (found == windows_.end()) throw std::out_of_range("unknown window identifier");
    return *found;
}

void WindowManager::close(WindowId id) {
    const auto found = std::find_if(windows_.begin(), windows_.end(),
                                    [id](const Entry& item) { return item.id == id; });
    if (found == windows_.end()) throw std::out_of_range("unknown window identifier");
    windows_.erase(found);
}

void WindowManager::closeSession(SessionId owner) {
    windows_.erase(std::remove_if(windows_.begin(), windows_.end(),
                                  [owner](const Entry& item) { return item.owner == owner; }),
                   windows_.end());
}

WindowState WindowManager::windowState(WindowId id) const {
    const auto& entry = find(id);
    return {entry.id, entry.owner, entry.bounds, clip(entry.bounds),
            entry.canvas.depth(), entry.visible};
}

void WindowManager::setBounds(WindowId id, Rect bounds) {
    validate(bounds);
    auto& entry = find(id);
    const auto newBytes = checkedCanvasBytes(bounds.width, bounds.height, entry.canvas.depth());
    if (maximumCanvasBytes_ != 0) {
        std::size_t allocated = newBytes;
        if (allocated > maximumCanvasBytes_)
            throw std::runtime_error("window manager canvas memory limit reached");
        for (const auto& item : windows_) {
            if (item.id == id) continue;
            const auto bytes = item.canvas.storageBytes();
            if (bytes > maximumCanvasBytes_ - allocated)
                throw std::runtime_error("window manager canvas memory limit reached");
            allocated += bytes;
        }
    }
    if (bounds.width == entry.bounds.width && bounds.height == entry.bounds.height) {
        entry.bounds = bounds;
        return;
    }
    entry.canvas.resize(bounds.width, bounds.height);
    entry.bounds = bounds;
}

void WindowManager::setPixelDepth(WindowId id, PixelDepth depth) {
    auto& entry = find(id);
    const auto newBytes = checkedCanvasBytes(entry.bounds.width, entry.bounds.height, depth);
    if (maximumCanvasBytes_ != 0) {
        std::size_t allocated = newBytes;
        if (allocated > maximumCanvasBytes_)
            throw std::runtime_error("window manager canvas memory limit reached");
        for (const auto& item : windows_) {
            if (item.id == id) continue;
            const auto bytes = item.canvas.storageBytes();
            if (bytes > maximumCanvasBytes_ - allocated)
                throw std::runtime_error("window manager canvas memory limit reached");
            allocated += bytes;
        }
    }
    entry.canvas.setDepth(depth);
}

void WindowManager::setScreenSize(std::uint32_t width, std::uint32_t height) {
    (void)checkedPixelCount(width, height);
    screenWidth_ = width;
    screenHeight_ = height;
}

void WindowManager::setVisible(WindowId id, bool visible) {
    find(id).visible = visible;
}

void WindowManager::raise(WindowId id) {
    const auto found = std::find_if(windows_.begin(), windows_.end(),
                                    [id](const Entry& item) { return item.id == id; });
    if (found == windows_.end()) throw std::out_of_range("unknown window identifier");
    Entry raised = std::move(*found);
    windows_.erase(found);
    windows_.push_back(std::move(raised));
}

Rect WindowManager::clip(Rect bounds) const {
    const std::int64_t left = std::max<std::int64_t>(0, bounds.x);
    const std::int64_t top = std::max<std::int64_t>(0, bounds.y);
    const std::int64_t right = std::min<std::int64_t>(screenWidth_,
        static_cast<std::int64_t>(bounds.x) + bounds.width);
    const std::int64_t bottom = std::min<std::int64_t>(screenHeight_,
        static_cast<std::int64_t>(bounds.y) + bounds.height);
    if (left >= right || top >= bottom) return {};
    return {static_cast<std::int32_t>(left), static_cast<std::int32_t>(top),
            static_cast<std::uint32_t>(right - left),
            static_cast<std::uint32_t>(bottom - top)};
}

IndexedCanvas& WindowManager::canvas(WindowId id) { return find(id).canvas; }

const IndexedCanvas& WindowManager::canvas(WindowId id) const { return find(id).canvas; }

std::vector<ColorIndex> WindowManager::compose(ColorIndex desktopPaper) const {
    const auto framePixels = checkedPixelCount(screenWidth_, screenHeight_);
    std::vector<ColorIndex> frame;
    frame.reserve(framePixels);
    composeInto(frame, desktopPaper);
    return frame;
}

void WindowManager::composeInto(std::vector<ColorIndex>& frame,
                                ColorIndex desktopPaper) const {
    const auto framePixels = checkedPixelCount(screenWidth_, screenHeight_);
    frame.resize(framePixels);
    std::fill(frame.begin(), frame.end(), desktopPaper);
    for (const auto& state : compositionOrder()) {
        const auto& source = find(state.id).canvas;
        const auto sourceX = static_cast<std::uint32_t>(
            static_cast<std::int64_t>(state.screenClip.x) - state.bounds.x);
        const auto sourceY = static_cast<std::uint32_t>(
            static_cast<std::int64_t>(state.screenClip.y) - state.bounds.y);
        for (std::uint32_t row = 0; row < state.screenClip.height; ++row) {
            const auto destinationOffset = static_cast<std::size_t>(state.screenClip.y + row) * screenWidth_ +
                                           static_cast<std::uint32_t>(state.screenClip.x);
            for (std::uint32_t column = 0; column < state.screenClip.width; ++column) {
                frame[destinationOffset + column] =
                    source.pixel(sourceX + column, sourceY + row);
            }
        }
    }
}

void WindowManager::composeRowInto(std::uint32_t y,
                                   std::vector<ColorIndex>& row,
                                   ColorIndex desktopPaper) const {
    composeRegionRowInto(y, 0, screenWidth_, row, desktopPaper);
}

void WindowManager::composeRegionRowInto(std::uint32_t y, std::uint32_t x,
                                         std::uint32_t width,
                                         std::vector<ColorIndex>& row,
                                         ColorIndex desktopPaper) const {
    if (y >= screenHeight_ || x > screenWidth_ ||
        width > screenWidth_ - x)
        throw std::out_of_range("composed region row is outside the screen");
    row.resize(width);
    std::fill(row.begin(), row.end(), desktopPaper);
    if (width == 0) return;
    const auto right = x + width;
    for (const auto& entry : windows_) {
        if (!entry.visible) continue;
        const Rect visible = clip(entry.bounds);
        if (visible.width == 0 || visible.height == 0 ||
            y < static_cast<std::uint32_t>(visible.y) ||
            y >= static_cast<std::uint32_t>(visible.y) + visible.height)
            continue;
        const auto left = std::max(x, static_cast<std::uint32_t>(visible.x));
        const auto clippedRight = std::min(right,
            static_cast<std::uint32_t>(visible.x) + visible.width);
        if (left >= clippedRight) continue;
        const auto sourceX = static_cast<std::uint32_t>(
            static_cast<std::int64_t>(left) - entry.bounds.x);
        const auto sourceY = static_cast<std::uint32_t>(
            static_cast<std::int64_t>(y) - entry.bounds.y);
        for (std::uint32_t destinationX = left;
             destinationX < clippedRight; ++destinationX) {
            row[destinationX - x] = entry.canvas.pixel(
                sourceX + destinationX - left, sourceY);
        }
    }
}

std::vector<WindowState> WindowManager::compositionOrder() const {
    std::vector<WindowState> result;
    result.reserve(windows_.size());
    for (const auto& entry : windows_) {
        if (!entry.visible) continue;
        const Rect visible = clip(entry.bounds);
        if (visible.width == 0 || visible.height == 0) continue;
        result.push_back({entry.id, entry.owner, entry.bounds, visible,
                          entry.canvas.depth(), entry.visible});
    }
    return result;
}

void WindowManager::tile() {
    std::vector<Entry*> visible;
    visible.reserve(windows_.size());
    for (auto& entry : windows_) {
        if (entry.visible) visible.push_back(&entry);
    }
    if (visible.empty()) return;

    if (visible.size() > std::numeric_limits<std::uint32_t>::max())
        throw std::runtime_error("too many visible windows to tile");
    const auto count = static_cast<std::uint32_t>(visible.size());
    if (visible.size() > static_cast<std::uint64_t>(screenWidth_) * screenHeight_)
        throw std::runtime_error("not enough screen pixels to tile all visible windows");
    auto columns = static_cast<std::uint32_t>(std::sqrt(static_cast<double>(count)));
    if (columns * columns < count) ++columns;
    columns = std::min(columns, screenWidth_);
    auto rows = (count + columns - 1) / columns;
    if (rows > screenHeight_) {
        rows = screenHeight_;
        columns = (count + rows - 1) / rows;
    }
    std::vector<Rect> tiledBounds;
    tiledBounds.reserve(visible.size());
    std::size_t requiredBytes = 0;
    for (std::size_t index = 0; index < visible.size(); ++index) {
        const auto column = static_cast<std::uint32_t>(index % columns);
        const auto row = static_cast<std::uint32_t>(index / columns);
        const auto left = static_cast<std::uint32_t>(
            static_cast<std::uint64_t>(column) * screenWidth_ / columns);
        const auto right = static_cast<std::uint32_t>(
            static_cast<std::uint64_t>(column + 1) * screenWidth_ / columns);
        const auto top = static_cast<std::uint32_t>(
            static_cast<std::uint64_t>(row) * screenHeight_ / rows);
        const auto bottom = static_cast<std::uint32_t>(
            static_cast<std::uint64_t>(row + 1) * screenHeight_ / rows);
        Rect bounds{static_cast<std::int32_t>(left), static_cast<std::int32_t>(top),
                    right - left, bottom - top};
        const auto bytes = checkedCanvasBytes(bounds.width, bounds.height,
                                               visible[index]->canvas.depth());
        if (bytes > std::numeric_limits<std::size_t>::max() - requiredBytes)
            throw std::length_error("tiled canvas size exceeds addressable memory");
        requiredBytes += bytes;
        tiledBounds.push_back(bounds);
    }
    for (const auto& entry : windows_) {
        if (entry.visible) continue;
        const auto bytes = entry.canvas.storageBytes();
        if (bytes > std::numeric_limits<std::size_t>::max() - requiredBytes)
            throw std::length_error("tiled canvas size exceeds addressable memory");
        requiredBytes += bytes;
    }
    if (maximumCanvasBytes_ != 0 && requiredBytes > maximumCanvasBytes_)
        throw std::runtime_error("window manager canvas memory limit reached");
    for (std::size_t index = 0; index < visible.size(); ++index) {
        visible[index]->canvas.resize(tiledBounds[index].width, tiledBounds[index].height);
        visible[index]->bounds = tiledBounds[index];
    }
}

} // namespace ql32::graphics
