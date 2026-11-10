#pragma once

#include "ql32/runtime/storage.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ql32::graphics {

using FontHandle = std::uint32_t;
using FontChannel = std::uint64_t;

enum class FontStyle : std::uint8_t {
    Normal = 0,
    Bold = 1,
    Underline = 2
};

constexpr std::uint8_t fontStyleBits(FontStyle style) noexcept {
    return static_cast<std::uint8_t>(style);
}

constexpr bool validFontStyle(std::uint8_t style) noexcept {
    return (style & ~std::uint8_t{3}) == 0;
}

enum class FontMetric : std::uint32_t {
    Width = 0,
    Height = 1,
    Ascent = 2,
    LineHeight = 3,
    FirstCharacter = 4,
    CharacterCount = 5,
    MaxAdvance = 6
};

struct FontMetrics {
    std::uint8_t maxAdvance = 0;
    std::uint8_t lineHeight = 0;
    std::uint8_t ascent = 0;
    std::uint8_t firstCharacter = 32;
    std::uint8_t characterCount = 95;
};

struct FontGlyph {
    static constexpr std::size_t MaximumBitmapBytes = 48;

    std::uint8_t character = 0;
    std::uint8_t width = 0;
    std::uint8_t height = 0;
    std::uint8_t advance = 0;
    std::int8_t xOffset = 0;
    std::int8_t yOffset = 0;
    std::uint8_t flags = 0;
    std::array<std::uint8_t, MaximumBitmapBytes> bitmap{};

    bool pixel(std::uint8_t x, std::uint8_t y) const noexcept;
};

class BitmapFont {
public:
    static constexpr std::uint8_t Version = 2;
    static constexpr std::uint8_t FirstCharacter = 32;
    static constexpr std::uint8_t CharacterCount = 95;
    static constexpr std::size_t HeaderSize = 26;
    static constexpr std::size_t GlyphIndexEntrySize = 13;
    static constexpr std::size_t MaximumFileSize = 8192;

    static std::unique_ptr<BitmapFont> open(
        std::unique_ptr<runtime::StorageReadHandle> source);
    static const BitmapFont& builtIn(FontHandle handle);

    const FontMetrics& metrics() const noexcept { return metrics_; }
    bool readGlyph(std::uint8_t character, FontGlyph& glyph) const;
    bool builtIn() const noexcept { return builtIn_; }

private:
    BitmapFont(FontMetrics metrics,
               std::unique_ptr<runtime::StorageReadHandle> source,
               std::uint32_t indexOffset, std::uint32_t totalSize);
    BitmapFont(FontMetrics metrics, runtime::StorageReadHandle* source,
               std::uint32_t indexOffset, std::uint32_t totalSize,
               bool builtIn);
    static BitmapFont embedded(runtime::StorageReadHandle* source,
                               std::size_t size);

    FontMetrics metrics_;
    std::unique_ptr<runtime::StorageReadHandle> ownedSource_;
    runtime::StorageReadHandle* source_ = nullptr;
    std::uint32_t indexOffset_ = 0;
    std::uint32_t totalSize_ = 0;
    bool builtIn_ = false;
};

struct FontSelection {
    FontHandle handle = 0;
    FontStyle style = FontStyle::Normal;
};

class FontRegistry {
public:
    explicit FontRegistry(std::shared_ptr<runtime::StorageService> storage = {});
    FontRegistry(const FontRegistry&) = delete;
    FontRegistry& operator=(const FontRegistry&) = delete;

    FontHandle load(const std::string& path);
    void unload(FontHandle handle);
    const BitmapFont& get(FontHandle handle) const;
    const BitmapFont& selectText(FontChannel channel, FontHandle handle,
                                 std::uint8_t style);
    const BitmapFont& selectGraphics(FontChannel channel, FontHandle handle,
                                     std::uint8_t style);
    FontSelection textSelection(FontChannel channel) const;
    FontSelection graphicsSelection(FontChannel channel) const;
    void clearChannel(FontChannel channel) noexcept;
    const std::map<FontChannel, FontSelection>& textSelections() const noexcept {
        return textSelections_;
    }
    const std::map<FontChannel, FontSelection>& graphicsSelections() const noexcept {
        return graphicsSelections_;
    }
    std::uint32_t metric(FontMetric selector, FontHandle handle) const;

private:
    const BitmapFont& select(FontChannel channel, FontHandle handle,
                             std::uint8_t style,
                             std::map<FontChannel, FontSelection>& selections);
    bool inUse(FontHandle handle) const noexcept;

    std::shared_ptr<runtime::StorageService> storage_;
    std::map<FontHandle, std::unique_ptr<BitmapFont>> loaded_;
    std::map<FontChannel, FontSelection> textSelections_;
    std::map<FontChannel, FontSelection> graphicsSelections_;
    FontHandle nextHandle_ = 5;
};

std::uint8_t nextAsciiCharacter(std::string_view text, std::size_t& offset) noexcept;

} // namespace ql32::graphics