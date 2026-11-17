#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <limits>

namespace ql32::platform::olimex_vga {

// Describes CPU-addressable logical pixels. It contains no display-library,
// DMA, allocator, or board-specific types; allocation and scanout stay in the
// Olimex adapter.
enum class PixelFormat : std::uint8_t {
    Rgb222,       // One byte per pixel; the low six bits hold R2:G2:B2.
    Indexed4,     // Two palette indices per byte; even x uses the high nibble.
};

struct FramebufferView {
    std::uint8_t* pixels = nullptr;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::size_t stride = 0;
    PixelFormat format = PixelFormat::Rgb222;

    constexpr std::size_t minimumStride() const noexcept {
        return format == PixelFormat::Rgb222
            ? static_cast<std::size_t>(width)
            : (static_cast<std::size_t>(width) + 1) / 2;
    }

    constexpr std::size_t storageBytes() const noexcept {
        return stride * height;
    }

    constexpr bool valid() const noexcept {
        return pixels && width && height && stride >= minimumStride() &&
            stride <= std::numeric_limits<std::size_t>::max() / height;
    }
};

struct PixelRect {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    constexpr bool empty() const noexcept { return width == 0 || height == 0; }
};

constexpr PixelRect unite(PixelRect a, PixelRect b) noexcept {
    if (a.empty()) return b;
    if (b.empty()) return a;
    const auto left = a.x < b.x ? a.x : b.x;
    const auto top = a.y < b.y ? a.y : b.y;
    const auto rightA = a.x + a.width;
    const auto rightB = b.x + b.width;
    const auto bottomA = a.y + a.height;
    const auto bottomB = b.y + b.height;
    const auto right = rightA > rightB ? rightA : rightB;
    const auto bottom = bottomA > bottomB ? bottomA : bottomB;
    return {left, top, right - left, bottom - top};
}

struct BitmapView {
    const std::uint8_t* pixels = nullptr;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::size_t stride = 0;
    PixelFormat format = PixelFormat::Rgb222;

    constexpr std::size_t minimumStride() const noexcept {
        return format == PixelFormat::Rgb222
            ? static_cast<std::size_t>(width)
            : (static_cast<std::size_t>(width) + 1) / 2;
    }

    constexpr bool valid() const noexcept {
        return pixels && width && height && stride >= minimumStride() &&
            stride <= std::numeric_limits<std::size_t>::max() / height;
    }
};

struct MutableBitmapView {
    std::uint8_t* pixels = nullptr;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::size_t stride = 0;
    PixelFormat format = PixelFormat::Rgb222;

    constexpr std::size_t minimumStride() const noexcept {
        return format == PixelFormat::Rgb222
            ? static_cast<std::size_t>(width)
            : (static_cast<std::size_t>(width) + 1) / 2;
    }

    constexpr bool valid() const noexcept {
        return pixels && width && height && stride >= minimumStride() &&
            stride <= std::numeric_limits<std::size_t>::max() / height;
    }
};

inline std::uint8_t readPixel(const FramebufferView& framebuffer,
                              std::uint32_t x, std::uint32_t y) noexcept {
    if (!framebuffer.valid() || x >= framebuffer.width || y >= framebuffer.height)
        return 0;
    const auto* row = framebuffer.pixels + static_cast<std::size_t>(y) * framebuffer.stride;
    if (framebuffer.format == PixelFormat::Rgb222) return row[x] & 0x3Fu;
    const auto packed = row[x / 2];
    return static_cast<std::uint8_t>((x & 1u) ? packed & 0x0Fu : packed >> 4);
}

inline std::uint8_t readPixel(const BitmapView& bitmap, std::uint32_t x,
                              std::uint32_t y) noexcept {
    if (!bitmap.valid() || x >= bitmap.width || y >= bitmap.height) return 0;
    const auto* row = bitmap.pixels + static_cast<std::size_t>(y) * bitmap.stride;
    if (bitmap.format == PixelFormat::Rgb222) return row[x] & 0x3Fu;
    const auto packed = row[x / 2];
    return static_cast<std::uint8_t>((x & 1u) ? packed & 0x0Fu : packed >> 4);
}

inline bool writePixel(FramebufferView framebuffer, std::uint32_t x,
                       std::uint32_t y, std::uint8_t value) noexcept {
    if (!framebuffer.valid() || x >= framebuffer.width || y >= framebuffer.height)
        return false;
    auto* row = framebuffer.pixels + static_cast<std::size_t>(y) * framebuffer.stride;
    if (framebuffer.format == PixelFormat::Rgb222) {
        row[x] = value & 0x3Fu;
    } else {
        auto& packed = row[x / 2];
        if (x & 1u)
            packed = static_cast<std::uint8_t>((packed & 0xF0u) | (value & 0x0Fu));
        else
            packed = static_cast<std::uint8_t>((packed & 0x0Fu) | ((value & 0x0Fu) << 4));
    }
    return true;
}

// Convert and write a complete row segment. Packed-4 output writes two
// adjacent mapped pixels per byte, avoiding a read/modify/write for each
// pixel during framebuffer presentation.
inline bool writeMappedRow(FramebufferView framebuffer, std::uint32_t x,
                           std::uint32_t y, const std::uint8_t* indices,
                           std::uint32_t count,
                           const std::uint8_t* indexMap) noexcept {
    if (!framebuffer.valid() || !indices || !indexMap || y >= framebuffer.height ||
        x > framebuffer.width || count > framebuffer.width - x)
        return false;
    auto* row = framebuffer.pixels + static_cast<std::size_t>(y) * framebuffer.stride;
    if (framebuffer.format == PixelFormat::Rgb222) {
        for (std::uint32_t i = 0; i < count; ++i)
            row[x + i] = indexMap[indices[i]] & 0x3Fu;
        return true;
    }

    std::uint32_t i = 0;
    if ((x & 1u) && count != 0) {
        auto& packed = row[x / 2];
        packed = static_cast<std::uint8_t>((packed & 0xF0u) |
                                           (indexMap[indices[i++]] & 0x0Fu));
        ++x;
        --count;
    }
    while (count >= 2) {
        row[x / 2] = static_cast<std::uint8_t>(
            ((indexMap[indices[i]] & 0x0Fu) << 4) |
             (indexMap[indices[i + 1]] & 0x0Fu));
        x += 2;
        i += 2;
        count -= 2;
    }
    if (count != 0) {
        auto& packed = row[x / 2];
        packed = static_cast<std::uint8_t>((packed & 0x0Fu) |
                                           ((indexMap[indices[i]] & 0x0Fu) << 4));
    }
    return true;
}

// Synchronize a pixel rectangle between distinct framebuffers of the same
// format. Packed-4 edges preserve the untouched neighboring nibble.
inline bool copyRegion(const FramebufferView& source, FramebufferView destination,
                       std::uint32_t x, std::uint32_t y,
                       std::uint32_t width, std::uint32_t height) noexcept {
    if (!source.valid() || !destination.valid() || !source.pixels ||
        source.format != destination.format || source.width != destination.width ||
        source.height != destination.height || x > source.width ||
        y > source.height || width > source.width - x ||
        height > source.height - y)
        return false;
    if (width == 0 || height == 0) return true;

    for (std::uint32_t rowIndex = y; rowIndex < y + height; ++rowIndex) {
        const auto* input = source.pixels +
            static_cast<std::size_t>(rowIndex) * source.stride;
        auto* output = destination.pixels +
            static_cast<std::size_t>(rowIndex) * destination.stride;
        if (source.format == PixelFormat::Rgb222) {
            std::memcpy(output + x, input + x, width);
            continue;
        }

        auto column = x;
        auto remaining = width;
        if ((column & 1u) && remaining != 0) {
            auto& target = output[column / 2];
            target = static_cast<std::uint8_t>((target & 0xF0u) |
                                               (input[column / 2] & 0x0Fu));
            ++column;
            --remaining;
        }
        const auto pairedPixels = remaining & ~std::uint32_t{1};
        if (pairedPixels != 0) {
            std::memcpy(output + column / 2, input + column / 2,
                        pairedPixels / 2);
            column += pairedPixels;
            remaining -= pairedPixels;
        }
        if (remaining != 0) {
            auto& target = output[column / 2];
            target = static_cast<std::uint8_t>((target & 0x0Fu) |
                                               (input[column / 2] & 0xF0u));
        }
    }
    return true;
}

inline void clear(FramebufferView framebuffer, std::uint8_t value) noexcept {
    if (!framebuffer.valid()) return;
    if (framebuffer.format == PixelFormat::Rgb222) {
        std::memset(framebuffer.pixels, value & 0x3Fu, framebuffer.storageBytes());
        return;
    }
    value &= 0x0Fu;
    const auto packed = static_cast<std::uint8_t>((value << 4) | value);
    std::memset(framebuffer.pixels, packed, framebuffer.storageBytes());
}

inline void fillRect(FramebufferView framebuffer, std::int32_t x,
                     std::int32_t y, std::int32_t width,
                     std::int32_t height, std::uint8_t value) noexcept {
    if (!framebuffer.valid() || width <= 0 || height <= 0) return;
    const auto left = x < 0 ? std::uint32_t{0} : static_cast<std::uint32_t>(x);
    const auto top = y < 0 ? std::uint32_t{0} : static_cast<std::uint32_t>(y);
    const auto right = static_cast<std::uint32_t>(
        (static_cast<std::int64_t>(x) + width) < framebuffer.width
            ? static_cast<std::int64_t>(x) + width : framebuffer.width);
    const auto bottom = static_cast<std::uint32_t>(
        (static_cast<std::int64_t>(y) + height) < framebuffer.height
            ? static_cast<std::int64_t>(y) + height : framebuffer.height);
    if (left >= right || top >= bottom) return;

    if (framebuffer.format == PixelFormat::Rgb222) {
        for (auto row = top; row < bottom; ++row)
            std::memset(framebuffer.pixels + static_cast<std::size_t>(row) * framebuffer.stride + left,
                        value & 0x3Fu, right - left);
        return;
    }

    value &= 0x0Fu;
    const auto repeated = static_cast<std::uint8_t>((value << 4) | value);
    for (auto rowIndex = top; rowIndex < bottom; ++rowIndex) {
        auto* row = framebuffer.pixels + static_cast<std::size_t>(rowIndex) * framebuffer.stride;
        auto column = left;
        if (column & 1u) {
            auto& edge = row[column / 2];
            edge = static_cast<std::uint8_t>((edge & 0xF0u) | value);
            ++column;
        }
        const auto pairedPixels = (right - column) / 2;
        if (pairedPixels != 0) {
            std::memset(row + column / 2, repeated, pairedPixels);
            column += pairedPixels * 2;
        }
        if (column < right) {
            auto& edge = row[column / 2];
            edge = static_cast<std::uint8_t>((edge & 0x0Fu) | (value << 4));
        }
    }
}

inline void drawHorizontalLine(FramebufferView framebuffer, std::int32_t x,
                               std::int32_t y, std::int32_t length,
                               std::uint8_t value) noexcept {
    fillRect(framebuffer, x, y, length, 1, value);
}

inline void drawVerticalLine(FramebufferView framebuffer, std::int32_t x,
                             std::int32_t y, std::int32_t length,
                             std::uint8_t value) noexcept {
    fillRect(framebuffer, x, y, 1, length, value);
}

inline void copyRect(FramebufferView framebuffer,
                     std::int32_t sourceX, std::int32_t sourceY,
                     std::int32_t destinationX, std::int32_t destinationY,
                     std::int32_t width, std::int32_t height) noexcept {
    if (!framebuffer.valid() || width <= 0 || height <= 0) return;
    std::int64_t sx = sourceX, sy = sourceY, dx = destinationX, dy = destinationY;
    std::int64_t w = width, h = height;
    if (sx < 0) { const auto n = -sx; sx = 0; dx += n; w -= n; }
    if (sy < 0) { const auto n = -sy; sy = 0; dy += n; h -= n; }
    if (dx < 0) { const auto n = -dx; dx = 0; sx += n; w -= n; }
    if (dy < 0) { const auto n = -dy; dy = 0; sy += n; h -= n; }
    w = std::min<std::int64_t>(w, static_cast<std::int64_t>(framebuffer.width) - sx);
    w = std::min<std::int64_t>(w, static_cast<std::int64_t>(framebuffer.width) - dx);
    h = std::min<std::int64_t>(h, static_cast<std::int64_t>(framebuffer.height) - sy);
    h = std::min<std::int64_t>(h, static_cast<std::int64_t>(framebuffer.height) - dy);
    if (w <= 0 || h <= 0) return;

    if (framebuffer.format == PixelFormat::Rgb222) {
        const auto rowBytes = static_cast<std::size_t>(w);
        const auto first = dy > sy ? h - 1 : 0;
        const auto end = dy > sy ? -1 : h;
        const auto step = dy > sy ? -1 : 1;
        for (auto row = first; row != end; row += step) {
            auto* target = framebuffer.pixels +
                static_cast<std::size_t>(dy + row) * framebuffer.stride + dx;
            const auto* source = framebuffer.pixels +
                static_cast<std::size_t>(sy + row) * framebuffer.stride + sx;
            std::memmove(target, source, rowBytes);
        }
        return;
    }

    if (((sx | dx | w) & 1) == 0) {
        const auto rowBytes = static_cast<std::size_t>(w / 2);
        const auto first = dy > sy ? h - 1 : 0;
        const auto end = dy > sy ? -1 : h;
        const auto step = dy > sy ? -1 : 1;
        for (auto row = first; row != end; row += step) {
            auto* target = framebuffer.pixels +
                static_cast<std::size_t>(dy + row) * framebuffer.stride + dx / 2;
            const auto* source = framebuffer.pixels +
                static_cast<std::size_t>(sy + row) * framebuffer.stride + sx / 2;
            std::memmove(target, source, rowBytes);
        }
        return;
    }

    const auto reverseRows = dy > sy;
    const auto reverseColumns = dx > sx;
    for (std::int64_t rowOffset = 0; rowOffset < h; ++rowOffset) {
        const auto row = reverseRows ? h - 1 - rowOffset : rowOffset;
        for (std::int64_t columnOffset = 0; columnOffset < w; ++columnOffset) {
            const auto column = reverseColumns ? w - 1 - columnOffset : columnOffset;
            const auto pixel = readPixel(framebuffer,
                static_cast<std::uint32_t>(sx + column),
                static_cast<std::uint32_t>(sy + row));
            (void)writePixel(framebuffer,
                static_cast<std::uint32_t>(dx + column),
                static_cast<std::uint32_t>(dy + row), pixel);
        }
    }
}

inline void copyRow(FramebufferView framebuffer, std::int32_t sourceX,
                    std::int32_t sourceY, std::int32_t destinationX,
                    std::int32_t destinationY, std::int32_t width) noexcept {
    copyRect(framebuffer, sourceX, sourceY, destinationX, destinationY, width, 1);
}

// Copies an opaque bitmap using the same pixel format as the destination.
// For screen-to-screen copies use copyRect, which handles overlap safely.
inline void copyBitmapOpaque(FramebufferView destination,
                             std::int32_t destinationX,
                             std::int32_t destinationY,
                             const BitmapView& source,
                             std::int32_t sourceX, std::int32_t sourceY,
                             std::int32_t width, std::int32_t height) noexcept {
    if (!destination.valid() || !source.valid() ||
        destination.format != source.format || width <= 0 || height <= 0)
        return;
    std::int64_t sx = sourceX, sy = sourceY;
    std::int64_t dx = destinationX, dy = destinationY;
    std::int64_t w = width, h = height;
    if (sx < 0) { const auto n = -sx; sx = 0; dx += n; w -= n; }
    if (sy < 0) { const auto n = -sy; sy = 0; dy += n; h -= n; }
    if (dx < 0) { const auto n = -dx; dx = 0; sx += n; w -= n; }
    if (dy < 0) { const auto n = -dy; dy = 0; sy += n; h -= n; }
    w = std::min<std::int64_t>(w, static_cast<std::int64_t>(source.width) - sx);
    w = std::min<std::int64_t>(w, static_cast<std::int64_t>(destination.width) - dx);
    h = std::min<std::int64_t>(h, static_cast<std::int64_t>(source.height) - sy);
    h = std::min<std::int64_t>(h, static_cast<std::int64_t>(destination.height) - dy);
    if (w <= 0 || h <= 0) return;

    if (destination.format == PixelFormat::Rgb222) {
        for (std::int64_t row = 0; row < h; ++row) {
            auto* out = destination.pixels +
                static_cast<std::size_t>(dy + row) * destination.stride + dx;
            const auto* in = source.pixels +
                static_cast<std::size_t>(sy + row) * source.stride + sx;
            std::memcpy(out, in, static_cast<std::size_t>(w));
        }
        return;
    }
    if (((sx | dx | w) & 1) == 0) {
        const auto rowBytes = static_cast<std::size_t>(w / 2);
        for (std::int64_t row = 0; row < h; ++row) {
            auto* out = destination.pixels +
                static_cast<std::size_t>(dy + row) * destination.stride + dx / 2;
            const auto* in = source.pixels +
                static_cast<std::size_t>(sy + row) * source.stride + sx / 2;
            std::memcpy(out, in, rowBytes);
        }
        return;
    }

    for (std::int64_t row = 0; row < h; ++row) {
        auto* out = destination.pixels +
            static_cast<std::size_t>(dy + row) * destination.stride;
        const auto* in = source.pixels +
            static_cast<std::size_t>(sy + row) * source.stride;
        for (std::int64_t column = 0; column < w; ++column) {
            const auto sourceX = sx + column;
            const auto destinationX = dx + column;
            const auto value = (sourceX & 1)
                ? static_cast<std::uint8_t>(in[sourceX / 2] & 0x0Fu)
                : static_cast<std::uint8_t>(in[sourceX / 2] >> 4);
            auto& packed = out[destinationX / 2];
            if (destinationX & 1)
                packed = static_cast<std::uint8_t>((packed & 0xF0u) | value);
            else
                packed = static_cast<std::uint8_t>((packed & 0x0Fu) | (value << 4));
        }
    }
}

inline void copyBitmapTransparent(FramebufferView destination,
                                  std::int32_t destinationX,
                                  std::int32_t destinationY,
                                  const BitmapView& source,
                                  std::int32_t sourceX, std::int32_t sourceY,
                                  std::int32_t width, std::int32_t height,
                                  std::uint8_t transparentValue) noexcept {
    if (!destination.valid() || !source.valid() ||
        destination.format != source.format || width <= 0 || height <= 0)
        return;
    std::int64_t sx = sourceX, sy = sourceY;
    std::int64_t dx = destinationX, dy = destinationY;
    std::int64_t w = width, h = height;
    if (sx < 0) { const auto n = -sx; sx = 0; dx += n; w -= n; }
    if (sy < 0) { const auto n = -sy; sy = 0; dy += n; h -= n; }
    if (dx < 0) { const auto n = -dx; dx = 0; sx += n; w -= n; }
    if (dy < 0) { const auto n = -dy; dy = 0; sy += n; h -= n; }
    w = std::min<std::int64_t>(w, static_cast<std::int64_t>(source.width) - sx);
    w = std::min<std::int64_t>(w, static_cast<std::int64_t>(destination.width) - dx);
    h = std::min<std::int64_t>(h, static_cast<std::int64_t>(source.height) - sy);
    h = std::min<std::int64_t>(h, static_cast<std::int64_t>(destination.height) - dy);
    if (w <= 0 || h <= 0) return;
    const auto key = destination.format == PixelFormat::Rgb222
        ? static_cast<std::uint8_t>(transparentValue & 0x3Fu)
        : static_cast<std::uint8_t>(transparentValue & 0x0Fu);
    if (destination.format == PixelFormat::Rgb222) {
        for (std::int64_t row = 0; row < h; ++row) {
            auto* out = destination.pixels +
                static_cast<std::size_t>(dy + row) * destination.stride + dx;
            const auto* in = source.pixels +
                static_cast<std::size_t>(sy + row) * source.stride + sx;
            for (std::int64_t column = 0; column < w; ++column) {
                const auto value = static_cast<std::uint8_t>(in[column] & 0x3Fu);
                if (value != key) out[column] = value;
            }
        }
        return;
    }
    if (((sx | dx | w) & 1) == 0) {
        const auto rowBytes = static_cast<std::size_t>(w / 2);
        for (std::int64_t row = 0; row < h; ++row) {
            auto* out = destination.pixels +
                static_cast<std::size_t>(dy + row) * destination.stride + dx / 2;
            const auto* in = source.pixels +
                static_cast<std::size_t>(sy + row) * source.stride + sx / 2;
            for (std::size_t column = 0; column < rowBytes; ++column) {
                const auto value = in[column];
                auto result = out[column];
                const auto high = static_cast<std::uint8_t>(value >> 4);
                const auto low = static_cast<std::uint8_t>(value & 0x0Fu);
                if (high != key)
                    result = static_cast<std::uint8_t>((result & 0x0Fu) | (high << 4));
                if (low != key)
                    result = static_cast<std::uint8_t>((result & 0xF0u) | low);
                out[column] = result;
            }
        }
        return;
    }
    for (std::int64_t row = 0; row < h; ++row) {
        auto* out = destination.pixels +
            static_cast<std::size_t>(dy + row) * destination.stride;
        const auto* in = source.pixels +
            static_cast<std::size_t>(sy + row) * source.stride;
        for (std::int64_t column = 0; column < w; ++column) {
            const auto sourceX = sx + column;
            const auto destinationX = dx + column;
            const auto value = (sourceX & 1)
                ? static_cast<std::uint8_t>(in[sourceX / 2] & 0x0Fu)
                : static_cast<std::uint8_t>(in[sourceX / 2] >> 4);
            if (value == key) continue;
            auto& packed = out[destinationX / 2];
            if (destinationX & 1)
                packed = static_cast<std::uint8_t>((packed & 0xF0u) | value);
            else
                packed = static_cast<std::uint8_t>((packed & 0x0Fu) | (value << 4));
        }
    }
}

inline void copyFramebufferToBitmap(const FramebufferView& source,
                                    MutableBitmapView destination,
                                    std::int32_t destinationX,
                                    std::int32_t destinationY,
                                    std::int32_t sourceX, std::int32_t sourceY,
                                    std::int32_t width, std::int32_t height) noexcept {
    if (!source.valid() || !destination.valid() ||
        source.format != destination.format || width <= 0 || height <= 0)
        return;
    std::int64_t sx = sourceX, sy = sourceY;
    std::int64_t dx = destinationX, dy = destinationY;
    std::int64_t w = width, h = height;
    if (sx < 0) { const auto n = -sx; sx = 0; dx += n; w -= n; }
    if (sy < 0) { const auto n = -sy; sy = 0; dy += n; h -= n; }
    if (dx < 0) { const auto n = -dx; dx = 0; sx += n; w -= n; }
    if (dy < 0) { const auto n = -dy; dy = 0; sy += n; h -= n; }
    w = std::min<std::int64_t>(w, static_cast<std::int64_t>(source.width) - sx);
    w = std::min<std::int64_t>(w, static_cast<std::int64_t>(destination.width) - dx);
    h = std::min<std::int64_t>(h, static_cast<std::int64_t>(source.height) - sy);
    h = std::min<std::int64_t>(h, static_cast<std::int64_t>(destination.height) - dy);
    if (w <= 0 || h <= 0) return;
    if (source.format == PixelFormat::Rgb222) {
        for (std::int64_t row = 0; row < h; ++row) {
            const auto* in = source.pixels +
                static_cast<std::size_t>(sy + row) * source.stride + sx;
            auto* out = destination.pixels +
                static_cast<std::size_t>(dy + row) * destination.stride + dx;
            std::memcpy(out, in, static_cast<std::size_t>(w));
        }
        return;
    }
    for (std::int64_t row = 0; row < h; ++row) {
        for (std::int64_t column = 0; column < w; ++column) {
            auto* out = destination.pixels +
                static_cast<std::size_t>(dy + row) * destination.stride;
            const auto value = readPixel(source,
                static_cast<std::uint32_t>(sx + column),
                static_cast<std::uint32_t>(sy + row));
            auto& packed = out[static_cast<std::size_t>(dx + column) / 2];
            if ((dx + column) & 1)
                packed = static_cast<std::uint8_t>((packed & 0xF0u) | value);
            else
                packed = static_cast<std::uint8_t>((packed & 0x0Fu) | (value << 4));
        }
    }
}

inline void translateIndexed4(FramebufferView framebuffer,
                              const std::array<std::uint8_t, 16>& translation) noexcept {
    if (!framebuffer.valid() || framebuffer.format != PixelFormat::Indexed4) return;
    std::array<std::uint8_t, 256> packedTranslation{};
    for (std::uint32_t packed = 0; packed < packedTranslation.size(); ++packed) {
        packedTranslation[packed] = static_cast<std::uint8_t>(
            ((translation[packed >> 4] & 0x0Fu) << 4) |
            (translation[packed & 0x0Fu] & 0x0Fu));
    }
    for (std::uint32_t y = 0; y < framebuffer.height; ++y) {
        auto* row = framebuffer.pixels + static_cast<std::size_t>(y) * framebuffer.stride;
        const auto pairCount = framebuffer.width / 2;
        for (std::uint32_t pair = 0; pair < pairCount; ++pair)
            row[pair] = packedTranslation[row[pair]];
        if (framebuffer.width & 1u) {
            auto& tail = row[pairCount];
            tail = static_cast<std::uint8_t>(
                ((translation[tail >> 4] & 0x0Fu) << 4) | (tail & 0x0Fu));
        }
    }
}

// Positive distance scrolls content up; negative distance scrolls it down.
inline void scrollVertical(FramebufferView framebuffer, std::int32_t distance,
                           std::uint8_t exposedValue) noexcept {
    if (!framebuffer.valid() || distance == 0) return;
    const auto amount = distance > 0 ? static_cast<std::int64_t>(distance)
                                     : -static_cast<std::int64_t>(distance);
    if (amount >= framebuffer.height) {
        clear(framebuffer, exposedValue);
        return;
    }
    const auto rows = static_cast<std::int32_t>(amount);
    const auto bytesToMove = static_cast<std::size_t>(framebuffer.height - amount) *
        framebuffer.stride;
    if (distance > 0) {
        std::memmove(framebuffer.pixels,
                     framebuffer.pixels + static_cast<std::size_t>(rows) * framebuffer.stride,
                     bytesToMove);
        fillRect(framebuffer, 0,
                 static_cast<std::int32_t>(framebuffer.height) - rows,
                 static_cast<std::int32_t>(framebuffer.width), rows, exposedValue);
    } else {
        std::memmove(framebuffer.pixels + static_cast<std::size_t>(rows) * framebuffer.stride,
                     framebuffer.pixels, bytesToMove);
        fillRect(framebuffer, 0, 0,
                 static_cast<std::int32_t>(framebuffer.width), rows, exposedValue);
    }
}

inline void drawGlyphRow(FramebufferView framebuffer, std::int32_t x,
                         std::int32_t y, const std::uint8_t* bits,
                         std::uint32_t width, std::uint8_t ink,
                         bool opaque = false, std::uint8_t paper = 0) noexcept {
    if (!framebuffer.valid() || !bits) return;
    for (std::uint32_t column = 0; column < width; ++column) {
        const bool set = (bits[column / 8] & (0x80u >> (column & 7u))) != 0;
        if (set) {
            const auto px = static_cast<std::int64_t>(x) + column;
            if (px >= 0 && px < framebuffer.width && y >= 0 &&
                static_cast<std::uint32_t>(y) < framebuffer.height)
                (void)writePixel(framebuffer, static_cast<std::uint32_t>(px),
                    static_cast<std::uint32_t>(y), ink);
        } else if (opaque) {
            const auto px = static_cast<std::int64_t>(x) + column;
            if (px >= 0 && px < framebuffer.width && y >= 0 &&
                static_cast<std::uint32_t>(y) < framebuffer.height)
                (void)writePixel(framebuffer, static_cast<std::uint32_t>(px),
                    static_cast<std::uint32_t>(y), paper);
        }
    }
}

inline void drawLine(FramebufferView framebuffer, std::int32_t x0,
                     std::int32_t y0, std::int32_t x1, std::int32_t y1,
                     std::uint8_t value) noexcept {
    if (!framebuffer.valid()) return;
    long double ax = x0, ay = y0, bx = x1, by = y1;
    const auto maxX = static_cast<long double>(framebuffer.width - 1);
    const auto maxY = static_cast<long double>(framebuffer.height - 1);
    const auto code = [maxX, maxY](long double x, long double y) {
        return (x < 0 ? 1 : 0) | (x > maxX ? 2 : 0) |
               (y < 0 ? 4 : 0) | (y > maxY ? 8 : 0);
    };
    for (unsigned attempt = 0; attempt < 8; ++attempt) {
        const auto ca = code(ax, ay), cb = code(bx, by);
        if (!(ca | cb)) break;
        if (ca & cb) return;
        const auto outside = ca ? ca : cb;
        long double x = 0, y = 0;
        if (outside & 8) {
            y = maxY; x = ax + (bx - ax) * (maxY - ay) / (by - ay);
        } else if (outside & 4) {
            y = 0; x = ax + (bx - ax) * (0 - ay) / (by - ay);
        } else if (outside & 2) {
            x = maxX; y = ay + (by - ay) * (maxX - ax) / (bx - ax);
        } else {
            x = 0; y = ay + (by - ay) * (0 - ax) / (bx - ax);
        }
        if (outside == ca) { ax = x; ay = y; }
        else { bx = x; by = y; }
    }
    std::int64_t ix0 = std::clamp<std::int64_t>(std::llround(ax), 0, framebuffer.width - 1);
    std::int64_t iy0 = std::clamp<std::int64_t>(std::llround(ay), 0, framebuffer.height - 1);
    const std::int64_t ix1 = std::clamp<std::int64_t>(std::llround(bx), 0, framebuffer.width - 1);
    const std::int64_t iy1 = std::clamp<std::int64_t>(std::llround(by), 0, framebuffer.height - 1);
    const auto dx = std::llabs(ix1 - ix0);
    const std::int64_t sx = ix0 < ix1 ? 1 : -1;
    const auto dy = -std::llabs(iy1 - iy0);
    const std::int64_t sy = iy0 < iy1 ? 1 : -1;
    auto error = dx + dy;
    for (;;) {
        (void)writePixel(framebuffer, static_cast<std::uint32_t>(ix0),
                         static_cast<std::uint32_t>(iy0), value);
        if (ix0 == ix1 && iy0 == iy1) break;
        const auto twice = 2 * error;
        if (twice >= dy) { error += dy; ix0 += sx; }
        if (twice <= dx) { error += dx; iy0 += sy; }
    }
}

inline void drawCircle(FramebufferView framebuffer, std::int32_t centerX,
                       std::int32_t centerY, std::int32_t radius,
                       std::uint8_t value, bool filled = false) noexcept {
    if (!framebuffer.valid() || radius < 0) return;
    const auto cx = static_cast<std::int64_t>(centerX);
    const auto cy = static_cast<std::int64_t>(centerY);
    const auto r = static_cast<std::int64_t>(radius);
    if (cx + r < 0 || cy + r < 0 || cx - r >= framebuffer.width ||
        cy - r >= framebuffer.height) return;
    const auto span = [&](std::int64_t y, std::int64_t halfWidth) {
        if (y < 0 || y >= framebuffer.height) return;
        const auto left = std::max<std::int64_t>(0, cx - halfWidth);
        const auto right = std::min<std::int64_t>(framebuffer.width - 1, cx + halfWidth);
        if (left > right) return;
        auto* row = framebuffer.pixels + static_cast<std::size_t>(y) * framebuffer.stride;
        if (framebuffer.format == PixelFormat::Rgb222) {
            std::memset(row + left, value & 0x3Fu,
                        static_cast<std::size_t>(right - left + 1));
        } else {
            fillRect(framebuffer, static_cast<std::int32_t>(left),
                static_cast<std::int32_t>(y),
                static_cast<std::int32_t>(right - left + 1), 1, value);
        }
    };
    const auto point = [&](std::int64_t x, std::int64_t y) {
        if (x >= 0 && x < framebuffer.width && y >= 0 && y < framebuffer.height)
            (void)writePixel(framebuffer, static_cast<std::uint32_t>(x),
                             static_cast<std::uint32_t>(y), value);
    };
    if (filled) {
        const auto radiusSquared = r * r;
        auto halfWidth = r;
        for (std::int64_t row = 0; row <= r; ++row) {
            while (halfWidth > 0 && halfWidth * halfWidth + row * row > radiusSquared)
                --halfWidth;
            span(cy + row, halfWidth);
            if (row != 0) span(cy - row, halfWidth);
        }
        return;
    }
    std::int64_t x = r, y = 0, error = 1 - r;
    while (x >= y) {
        point(cx + x, cy + y); point(cx + y, cy + x);
        point(cx - y, cy + x); point(cx - x, cy + y);
        point(cx - x, cy - y); point(cx - y, cy - x);
        point(cx + y, cy - x); point(cx + x, cy - y);
        ++y;
        if (error < 0) error += 2 * y + 1;
        else { --x; error += 2 * (y - x) + 1; }
    }
}

} // namespace ql32::platform::olimex_vga
