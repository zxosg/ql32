#pragma once

#include "ql32/runtime/storage.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace ql32::graphics {

using BitmapHandle = std::uint32_t;

struct BitmapInfo {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

using BitmapRgb = std::array<std::uint8_t, 3>;
using BitmapPalette = std::array<BitmapRgb, 16>;

// QIMG v2 is a bounded, uncompressed, top-to-bottom indexed-4 image with an
// embedded 16-entry RGB palette. Rows are packed high-nibble-first.
class IndexedBitmap {
public:
    static constexpr std::uint8_t Version = 2;
    static constexpr std::size_t HeaderSize = 64;
    static constexpr std::uint32_t MaximumDimension = 2048;
    static constexpr std::uint32_t MaximumPayloadBytes = 1024 * 1024;

    static std::unique_ptr<IndexedBitmap> open(
        std::unique_ptr<runtime::StorageReadHandle> source);

    BitmapInfo info() const noexcept { return {width_, height_}; }
    const BitmapPalette& palette() const noexcept { return palette_; }
    bool readRow(std::uint32_t row, std::vector<std::uint8_t>& pixels) const;

private:
    IndexedBitmap(std::unique_ptr<runtime::StorageReadHandle> source,
                  std::uint32_t width, std::uint32_t height,
                  std::uint32_t stride, BitmapPalette palette);

    std::unique_ptr<runtime::StorageReadHandle> source_;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    std::uint32_t stride_ = 0;
    BitmapPalette palette_{};
};

// A registry belongs to one BASIC Interpreter, so loaded image handles and
// their open storage readers are automatically session-owned.
class BitmapRegistry {
public:
    explicit BitmapRegistry(std::shared_ptr<runtime::StorageService> storage = {});
    BitmapRegistry(const BitmapRegistry&) = delete;
    BitmapRegistry& operator=(const BitmapRegistry&) = delete;

    BitmapHandle load(const std::string& path);
    void unload(BitmapHandle handle);
    const IndexedBitmap& get(BitmapHandle handle) const;

private:
    std::shared_ptr<runtime::StorageService> storage_;
    std::map<BitmapHandle, std::unique_ptr<IndexedBitmap>> loaded_;
    BitmapHandle nextHandle_ = 1;
};

} // namespace ql32::graphics
