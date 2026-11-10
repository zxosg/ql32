#include "ql32/graphics/font.hpp"

#include "ql32/graphics/spleen_font_data.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace ql32::graphics {
namespace {

constexpr std::size_t IndexEntrySize = BitmapFont::GlyphIndexEntrySize;
constexpr std::uint8_t MaximumGlyphWidth = 16;
constexpr std::uint8_t MaximumGlyphHeight = 24;

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

bool readExact(runtime::StorageReadHandle& source, std::uint64_t offset,
               std::uint8_t* destination, std::size_t count) {
    return source.readAt(offset, destination, count) == count;
}

struct IndexEntry {
    std::uint8_t character = 0;
    std::uint8_t width = 0;
    std::uint8_t height = 0;
    std::uint8_t advance = 0;
    std::int8_t xOffset = 0;
    std::int8_t yOffset = 0;
    std::uint8_t flags = 0;
    std::uint32_t bitmapOffset = 0;
    std::uint16_t bitmapLength = 0;
};

IndexEntry decodeEntry(const std::array<std::uint8_t, IndexEntrySize>& bytes) noexcept {
    return {bytes[0], bytes[1], bytes[2], bytes[3],
        static_cast<std::int8_t>(bytes[4]), static_cast<std::int8_t>(bytes[5]),
        bytes[6], little32(bytes.data() + 7), little16(bytes.data() + 11)};
}

class EmbeddedFontReader final : public runtime::StorageReadHandle {
public:
    EmbeddedFontReader(const std::uint8_t* data, std::size_t size)
        : data_(data), size_(size) {}

    std::uint64_t size() const noexcept override { return size_; }
    std::size_t readAt(std::uint64_t offset, std::uint8_t* destination,
                       std::size_t capacity) override {
        if (offset >= size_ || capacity == 0) return 0;
        const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(
            capacity, size_ - offset));
        std::copy_n(data_ + static_cast<std::size_t>(offset), count, destination);
        return count;
    }

private:
    const std::uint8_t* data_;
    std::size_t size_;
};

bool validBuiltInHandle(FontHandle handle) noexcept {
    return handle >= 1 && handle <= 4;
}

std::pair<std::uint8_t, std::uint8_t> builtInDimensions(FontHandle handle) {
    switch (handle) {
    case 1: return {5, 8};
    case 2: return {6, 12};
    case 3: return {8, 16};
    case 4: return {12, 24};
    default: throw std::invalid_argument("unknown built-in font handle");
    }
}

} // namespace

bool FontGlyph::pixel(std::uint8_t x, std::uint8_t y) const noexcept {
    if (x >= width || y >= height) return false;
    const auto rowBytes = static_cast<std::size_t>((width + 7) / 8);
    const auto offset = static_cast<std::size_t>(y) * rowBytes + x / 8;
    return (bitmap[offset] & (0x80u >> (x % 8))) != 0;
}

BitmapFont::BitmapFont(FontMetrics metrics,
                                             std::unique_ptr<runtime::StorageReadHandle> source,
                                             std::uint32_t indexOffset, std::uint32_t totalSize)
        : metrics_(metrics), ownedSource_(std::move(source)),
            source_(ownedSource_.get()), indexOffset_(indexOffset),
            totalSize_(totalSize), builtIn_(false) {}

BitmapFont::BitmapFont(FontMetrics metrics,
                       runtime::StorageReadHandle* source,
                                             std::uint32_t indexOffset, std::uint32_t totalSize,
                                             bool builtIn)
        : metrics_(metrics), source_(source), indexOffset_(indexOffset),
            totalSize_(totalSize), builtIn_(builtIn) {}

std::unique_ptr<BitmapFont> BitmapFont::open(
        std::unique_ptr<runtime::StorageReadHandle> source) {
    if (!source || source->size() < HeaderSize || source->size() > MaximumFileSize)
        throw std::invalid_argument("QFNT file size is outside supported bounds");

    std::array<std::uint8_t, HeaderSize> header{};
    if (!readExact(*source, 0, header.data(), header.size()))
        throw std::invalid_argument("QFNT header is truncated");
    if (header[0] != 'Q' || header[1] != 'F' || header[2] != 'N' || header[3] != 'T' ||
        header[4] != Version || header[5] != HeaderSize)
        throw std::invalid_argument("invalid QFNT header or version");

    const FontMetrics metrics{header[6], header[7], header[8], header[9], header[10]};
    if (metrics.maxAdvance == 0 || metrics.maxAdvance > MaximumGlyphWidth ||
        metrics.lineHeight == 0 || metrics.lineHeight > MaximumGlyphHeight ||
        metrics.ascent > metrics.lineHeight || metrics.firstCharacter != FirstCharacter ||
        metrics.characterCount != CharacterCount || header[11] != 0 ||
        little16(header.data() + 12) != 0)
        throw std::invalid_argument("QFNT font metrics are invalid");

    const auto indexOffset = little32(header.data() + 14);
    const auto bitmapOffset = little32(header.data() + 18);
    const auto totalSize = little32(header.data() + 22);
    const auto expectedBitmapOffset = static_cast<std::uint64_t>(indexOffset) +
        static_cast<std::uint64_t>(CharacterCount) * IndexEntrySize;
    if (indexOffset != HeaderSize || bitmapOffset != expectedBitmapOffset ||
        totalSize != source->size() || totalSize > MaximumFileSize)
        throw std::invalid_argument("QFNT index or file bounds are invalid");

    std::uint32_t previousEnd = bitmapOffset;
    for (std::uint16_t index = 0; index < CharacterCount; ++index) {
        std::array<std::uint8_t, IndexEntrySize> bytes{};
        if (!readExact(*source, indexOffset + index * IndexEntrySize,
                       bytes.data(), bytes.size()))
            throw std::invalid_argument("QFNT glyph index is truncated");
        const auto entry = decodeEntry(bytes);
        const auto expectedCharacter = static_cast<std::uint8_t>(FirstCharacter + index);
        const auto expectedLength = static_cast<std::uint16_t>(
            ((entry.width + 7) / 8) * entry.height);
        const auto end = static_cast<std::uint64_t>(entry.bitmapOffset) +
            entry.bitmapLength;
        if (entry.character != expectedCharacter || entry.width > MaximumGlyphWidth ||
            entry.height > MaximumGlyphHeight || entry.advance > metrics.maxAdvance ||
            entry.flags != 0 || entry.bitmapLength != expectedLength ||
            entry.bitmapOffset < previousEnd || end > totalSize)
            throw std::invalid_argument("QFNT glyph index entry is invalid");
        previousEnd = static_cast<std::uint32_t>(end);
    }
    if (previousEnd != totalSize)
        throw std::invalid_argument("QFNT bitmap payload bounds are invalid");

    return std::unique_ptr<BitmapFont>(new BitmapFont(
        metrics, std::move(source), indexOffset, totalSize));
}

const BitmapFont& BitmapFont::builtIn(FontHandle handle) {
    const auto [width, height] = builtInDimensions(handle);
    (void)width;
    (void)height;
    switch (handle) {
    case 1: {
        static EmbeddedFontReader source(font_data::spleen_5x8,
            sizeof(font_data::spleen_5x8));
        static const BitmapFont font = embedded(&source,
            sizeof(font_data::spleen_5x8));
        return font;
    }
    case 2: {
        static EmbeddedFontReader source(font_data::spleen_6x12,
            sizeof(font_data::spleen_6x12));
        static const BitmapFont font = embedded(&source,
            sizeof(font_data::spleen_6x12));
        return font;
    }
    case 3: {
        static EmbeddedFontReader source(font_data::spleen_8x16,
            sizeof(font_data::spleen_8x16));
        static const BitmapFont font = embedded(&source,
            sizeof(font_data::spleen_8x16));
        return font;
    }
    default: {
        static EmbeddedFontReader source(font_data::spleen_12x24,
            sizeof(font_data::spleen_12x24));
        static const BitmapFont font = embedded(&source,
            sizeof(font_data::spleen_12x24));
        return font;
    }
    }
}

BitmapFont BitmapFont::embedded(runtime::StorageReadHandle* source,
                               std::size_t size) {
    std::array<std::uint8_t, HeaderSize> header{};
    if (!source || !readExact(*source, 0, header.data(), header.size()) ||
        header[0] != 'Q' || header[1] != 'F' || header[2] != 'N' || header[3] != 'T' ||
        header[4] != Version || header[5] != HeaderSize)
        throw std::logic_error("invalid embedded QFNT asset");
    const FontMetrics metrics{header[6], header[7], header[8], header[9], header[10]};
    if (header[11] != 0 || little16(header.data() + 12) != 0)
        throw std::logic_error("invalid embedded QFNT flags");
    const auto indexOffset = little32(header.data() + 14);
    const auto totalSize = little32(header.data() + 22);
    if (totalSize != size || indexOffset != HeaderSize)
        throw std::logic_error("invalid embedded QFNT asset");
    return BitmapFont(metrics, source, indexOffset, totalSize, true);
}

bool BitmapFont::readGlyph(std::uint8_t character, FontGlyph& glyph) const {
    if (character < metrics_.firstCharacter ||
        character >= metrics_.firstCharacter + metrics_.characterCount)
        character = '?';
    const auto index = static_cast<std::uint32_t>(character - metrics_.firstCharacter);
    std::array<std::uint8_t, IndexEntrySize> bytes{};
    if (!readExact(*source_, indexOffset_ + index * IndexEntrySize,
                   bytes.data(), bytes.size()))
        throw std::runtime_error("QFNT glyph index read failed");
    const auto entry = decodeEntry(bytes);
    glyph = {};
    glyph.character = entry.character;
    glyph.width = entry.width;
    glyph.height = entry.height;
    glyph.advance = entry.advance;
    glyph.xOffset = entry.xOffset;
    glyph.yOffset = entry.yOffset;
    glyph.flags = entry.flags;
    if (entry.bitmapLength > glyph.bitmap.size() ||
        !readExact(*source_, entry.bitmapOffset, glyph.bitmap.data(), entry.bitmapLength))
        throw std::runtime_error("QFNT glyph bitmap read failed");
    return true;
}

FontRegistry::FontRegistry(std::shared_ptr<runtime::StorageService> storage)
    : storage_(std::move(storage)) {}

FontHandle FontRegistry::load(const std::string& path) {
    if (!storage_)
        throw runtime::ServiceError(runtime::ServiceErrorCode::Unavailable,
                                    "storage service unavailable");
    auto font = BitmapFont::open(storage_->openRead(path));
    if (nextHandle_ < 5)
        throw std::overflow_error("font handles exhausted");
    while (loaded_.find(nextHandle_) != loaded_.end()) {
        if (nextHandle_ == std::numeric_limits<FontHandle>::max())
            throw std::overflow_error("font handles exhausted");
        ++nextHandle_;
    }
    const auto handle = nextHandle_;
    nextHandle_ = handle == std::numeric_limits<FontHandle>::max() ? 0 : handle + 1;
    loaded_.emplace(handle, std::move(font));
    return handle;
}

void FontRegistry::unload(FontHandle handle) {
    if (validBuiltInHandle(handle))
        throw std::invalid_argument("built-in fonts cannot be unloaded");
    const auto found = loaded_.find(handle);
    if (found == loaded_.end()) throw std::invalid_argument("unknown font handle");
    if (inUse(handle)) throw std::invalid_argument("font is selected by a graphics channel");
    loaded_.erase(found);
}

const BitmapFont& FontRegistry::get(FontHandle handle) const {
    if (validBuiltInHandle(handle)) return BitmapFont::builtIn(handle);
    const auto found = loaded_.find(handle);
    if (found == loaded_.end()) throw std::invalid_argument("unknown font handle");
    return *found->second;
}

const BitmapFont& FontRegistry::selectText(FontChannel channel, FontHandle handle,
                                           std::uint8_t style) {
    return select(channel, handle, style, textSelections_);
}

const BitmapFont& FontRegistry::selectGraphics(FontChannel channel, FontHandle handle,
                                               std::uint8_t style) {
    return select(channel, handle, style, graphicsSelections_);
}

const BitmapFont& FontRegistry::select(FontChannel channel, FontHandle handle,
                                      std::uint8_t style,
                                      std::map<FontChannel, FontSelection>& selections) {
    if (!validFontStyle(style)) throw std::invalid_argument("invalid font style flags");
    const auto& selected = get(handle);
    selections[channel] = {handle, static_cast<FontStyle>(style)};
    return selected;
}

FontSelection FontRegistry::textSelection(FontChannel channel) const {
    const auto found = textSelections_.find(channel);
    return found == textSelections_.end() ? FontSelection{} : found->second;
}

FontSelection FontRegistry::graphicsSelection(FontChannel channel) const {
    const auto found = graphicsSelections_.find(channel);
    return found == graphicsSelections_.end() ? FontSelection{} : found->second;
}

void FontRegistry::clearChannel(FontChannel channel) noexcept {
    textSelections_.erase(channel);
    graphicsSelections_.erase(channel);
}

std::uint32_t FontRegistry::metric(FontMetric selector, FontHandle handle) const {
    const auto& value = get(handle).metrics();
    switch (selector) {
    case FontMetric::Width: return value.maxAdvance;
    case FontMetric::Height: return value.lineHeight;
    case FontMetric::Ascent: return value.ascent;
    case FontMetric::LineHeight: return value.lineHeight;
    case FontMetric::FirstCharacter: return value.firstCharacter;
    case FontMetric::CharacterCount: return value.characterCount;
    case FontMetric::MaxAdvance: return value.maxAdvance;
    }
    throw std::invalid_argument("unknown font metric selector");
}

bool FontRegistry::inUse(FontHandle handle) const noexcept {
    const auto selected = [handle](const auto& selections) {
        return std::any_of(selections.begin(), selections.end(),
            [handle](const auto& item) { return item.second.handle == handle; });
    };
    return selected(textSelections_) || selected(graphicsSelections_);
}

std::uint8_t nextAsciiCharacter(std::string_view text, std::size_t& offset) noexcept {
    const auto first = static_cast<std::uint8_t>(text[offset++]);
    if (first < 0x80) return first >= 32 && first <= 126 ? first : static_cast<std::uint8_t>('?');

    std::size_t continuationCount = 0;
    std::uint32_t codePoint = 0;
    if ((first & 0xe0) == 0xc0) {
        continuationCount = 1;
        codePoint = first & 0x1f;
    } else if ((first & 0xf0) == 0xe0) {
        continuationCount = 2;
        codePoint = first & 0x0f;
    } else if ((first & 0xf8) == 0xf0) {
        continuationCount = 3;
        codePoint = first & 0x07;
    } else {
        return '?';
    }

    const auto start = offset - 1;
    for (std::size_t index = 0; index < continuationCount; ++index) {
        if (offset >= text.size() ||
            (static_cast<std::uint8_t>(text[offset]) & 0xc0) != 0x80) {
            offset = start + 1;
            return '?';
        }
        codePoint = (codePoint << 6) |
            (static_cast<std::uint8_t>(text[offset++]) & 0x3f);
    }
    const bool overlong = (continuationCount == 1 && codePoint < 0x80) ||
        (continuationCount == 2 && codePoint < 0x800) ||
        (continuationCount == 3 && codePoint < 0x10000);
    if (overlong || codePoint > 0x10ffff ||
        (codePoint >= 0xd800 && codePoint <= 0xdfff))
        offset = start + 1;
    return '?';
}

} // namespace ql32::graphics