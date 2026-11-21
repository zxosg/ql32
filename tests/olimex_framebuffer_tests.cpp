#include "ql32/platform/fabgl_vga/mode_memory.hpp"
#include "ql32/platform/olimex_vga/framebuffer.hpp"
#include "ql32/platform/olimex_vga/color_mapping.hpp"
#include "ql32/platform/olimex_vga/dma_pixel_order.hpp"
#include "ql32/platform/olimex_vga/palette.hpp"
#include "ql32/platform/olimex_vga/refill_window.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void testRgb222ClippingAndOverlap() {
    using namespace ql32::platform::olimex_vga;
    std::array<std::uint8_t, 32> storage{};
    FramebufferView framebuffer{storage.data(), 5, 4, 8, PixelFormat::Rgb222};

    fillRect(framebuffer, -1, 1, 3, 2, 0x3f);
    require(readPixel(framebuffer, 0, 1) == 0x3f, "clipped fill missed left edge");
    require(readPixel(framebuffer, 1, 2) == 0x3f, "clipped fill missed bottom row");
    require(readPixel(framebuffer, 2, 1) == 0, "clipped fill wrote past right edge");

    for (std::uint32_t x = 0; x < 5; ++x) writePixel(framebuffer, x, 0, x);
    copyRect(framebuffer, 0, 0, 1, 0, 4, 1);
    const std::array<std::uint8_t, 5> shifted{{0, 0, 1, 2, 3}};
    for (std::uint32_t x = 0; x < 5; ++x)
        require(readPixel(framebuffer, x, 0) == shifted[x], "RGB222 horizontal overlap");

    copyRect(framebuffer, 0, 0, 0, 1, 5, 3);
    for (std::uint32_t x = 0; x < 5; ++x)
        require(readPixel(framebuffer, x, 1) == shifted[x], "RGB222 vertical overlap");
}

void testOlimexColorMapping() {
    using namespace ql32::platform::olimex_vga;
    require(colorToRgb222({0, 0, 0}) == 0x00, "black RGB222 mapping");
    require(colorToRgb222({0, 0, 255}) == 0x03, "blue RGB222 mapping");
    require(colorToRgb222({255, 0, 0}) == 0x30, "red RGB222 mapping");
    require(colorToRgb222({0, 255, 0}) == 0x0c, "green RGB222 mapping");
    require(colorToRgb222({255, 255, 255}) == 0x3f, "white RGB222 mapping");
}

void testPacked4OddWidthAndOverlap() {
    using namespace ql32::platform::olimex_vga;
    std::array<std::uint8_t, 12> storage{};
    FramebufferView framebuffer{storage.data(), 5, 3, 3, PixelFormat::Indexed4};
    for (std::uint32_t x = 0; x < 5; ++x) writePixel(framebuffer, x, 0, x);
    require(storage[0] == 0x01 && storage[1] == 0x23 && storage[2] == 0x40,
            "packed4 high-nibble-first layout or odd tail is incorrect");

    fillRect(framebuffer, 1, 0, 3, 1, 0x0a);
    copyRect(framebuffer, 0, 0, 1, 0, 4, 1);
    const std::array<std::uint8_t, 5> shifted{{0, 0, 0x0a, 0x0a, 0x0a}};
    for (std::uint32_t x = 0; x < 5; ++x)
        require(readPixel(framebuffer, x, 0) == shifted[x], "packed4 horizontal overlap");

    clear(framebuffer, 7);
    for (std::uint32_t y = 0; y < framebuffer.height; ++y)
        for (std::uint32_t x = 0; x < framebuffer.width; ++x)
            require(readPixel(framebuffer, x, y) == 7, "packed4 clear missed a pixel");
}

void testMappedRowWrites() {
    using namespace ql32::platform::olimex_vga;
    std::array<std::uint8_t, 4> packedStorage{{0xAB, 0xCD, 0xEF, 0x12}};
    FramebufferView packed{packedStorage.data(), 6, 1, 4, PixelFormat::Indexed4};
    std::array<std::uint8_t, 256> mapping{};
    for (std::uint32_t i = 0; i < mapping.size(); ++i)
        mapping[i] = static_cast<std::uint8_t>(i ^ 0x05u);
    const std::array<std::uint8_t, 4> indices{{1, 2, 3, 4}};
    require(writeMappedRow(packed, 1, 0, indices.data(), indices.size(), mapping.data()),
            "packed mapped row rejected a valid odd-offset range");
    require(packedStorage[0] == 0xA4 && packedStorage[1] == 0x76 &&
            packedStorage[2] == 0x1F && packedStorage[3] == 0x12,
            "packed mapped row changed neighboring nibbles or mapped pixels incorrectly");
    require(!writeMappedRow(packed, 4, 0, indices.data(), indices.size(), mapping.data()),
            "packed mapped row accepted an out-of-range segment");

    std::array<std::uint8_t, 8> rgbStorage{};
    FramebufferView rgb{rgbStorage.data(), 4, 1, 8, PixelFormat::Rgb222};
    require(writeMappedRow(rgb, 1, 0, indices.data(), 2, mapping.data()),
            "RGB mapped row rejected a valid range");
    require(rgbStorage[0] == 0 && rgbStorage[1] == (mapping[1] & 0x3Fu) &&
            rgbStorage[2] == (mapping[2] & 0x3Fu) && rgbStorage[3] == 0,
            "RGB mapped row wrote incorrect values or crossed its range");
}

void testChunkedMappedRowMatchesPerByte() {
    using namespace ql32::platform::olimex_vga;
    std::array<std::uint8_t, 256> mapping{};
    for (std::uint32_t i = 0; i < mapping.size(); ++i)
        mapping[i] = static_cast<std::uint8_t>((i * 7u + 3u) & 0xFFu);
    constexpr std::uint32_t width = 700;
    std::vector<std::uint8_t> indices(width);
    for (std::uint32_t i = 0; i < width; ++i) indices[i] = static_cast<std::uint8_t>(i * 13u);
    for (std::uint32_t x : {0u, 1u, 2u, 5u})
        for (std::uint32_t count : {0u, 1u, 2u, 3u, 255u, 256u, 257u, 600u, 695u}) {
            if (x + count > width) continue;
            std::vector<std::uint8_t> a(width / 2, 0x5A), b(width / 2, 0x5A);
            FramebufferView fa{a.data(), width, 1, width / 2, PixelFormat::Indexed4};
            FramebufferView fb{b.data(), width, 1, width / 2, PixelFormat::Indexed4};
            require(writeMappedRow(fa, x, 0, indices.data(), count, mapping.data()) ==
                    writeMappedRowChunked(fb, x, 0, indices.data(), count, mapping.data()),
                    "chunked row result differs");
            require(a == b, "chunked row bytes differ from per-byte writer");
        }
}

void testVgaModeMemoryPreflight() {
    using namespace ql32::platform::fabgl_vga;
    // Measured: no-PSRAM mode 0 has 25-45 KB free
    require(!vgaModeFitsMemory(320, 240, 320 * 200, 45440), "320x240 must be refused without PSRAM");
    require(!vgaModeFitsMemory(400, 300, 320 * 200, 45176), "400x300 must be refused without PSRAM");
    require(vgaModeFitsMemory(320, 200, 400 * 300, 0), "shrinking is always allowed");
}

void testRegionSynchronization() {
    using namespace ql32::platform::olimex_vga;
    constexpr std::uint32_t width = 7;
    constexpr std::uint32_t height = 3;
    constexpr std::size_t stride = (width + 1) / 2;
    std::array<std::uint8_t, stride * height> sourceStorage{};
    std::array<std::uint8_t, stride * height> destinationStorage{};
    destinationStorage.fill(0xA5);
    FramebufferView source{sourceStorage.data(), width, height, stride,
                           PixelFormat::Indexed4};
    FramebufferView destination{destinationStorage.data(), width, height, stride,
                                PixelFormat::Indexed4};
    for (std::uint32_t y = 0; y < height; ++y)
        for (std::uint32_t x = 0; x < width; ++x)
            (void)writePixel(source, x, y, static_cast<std::uint8_t>(x + y * 7));

    require(copyRegion(source, destination, 1, 1, 5, 2),
            "packed region synchronization rejected a valid rectangle");
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const auto expected = y >= 1 && x >= 1 && x < 6
                ? readPixel(source, x, y) : ((x & 1u) ? 0x05 : 0x0A);
            require(readPixel(destination, x, y) == expected,
                    "packed region synchronization changed an edge nibble or missed a pixel");
        }
    }
    require(!copyRegion(source, destination, 6, 1, 2, 1),
            "region synchronization accepted an out-of-range rectangle");
}

void testIncrementalTripleBufferRepair() {
    using namespace ql32::platform::olimex_vga;
    constexpr std::uint32_t width = 8;
    constexpr std::uint32_t height = 4;
    constexpr std::size_t bytes = width * height / 2;
    std::array<std::array<std::uint8_t, bytes>, 3> storage{};
    std::array<FramebufferView, 3> frames{};
    for (std::size_t i = 0; i < frames.size(); ++i)
        frames[i] = {storage[i].data(), width, height, width / 2,
                     PixelFormat::Indexed4};
    std::array<PixelRect, 3> stale{};
    std::size_t active = 0;

    const auto updateAndSwap = [&](std::size_t next, PixelRect changed,
                                   std::uint8_t color) {
        require(copyRegion(frames[active], frames[next], stale[next].x,
                           stale[next].y, stale[next].width,
                           stale[next].height),
                "triple-buffer stale-region repair failed");
        stale[next] = {};
        fillRect(frames[next], changed.x, changed.y, changed.width,
                 changed.height, color);
        for (std::size_t i = 0; i < frames.size(); ++i)
            if (i != next) stale[i] = unite(stale[i], changed);
        active = next;
    };

    const PixelRect first{1, 1, 3, 1};
    const PixelRect second{5, 2, 2, 1};
    updateAndSwap(1, first, 4);
    updateAndSwap(0, second, 9);
    const auto repair = stale[2];
    require(copyRegion(frames[active], frames[2], repair.x, repair.y,
                       repair.width, repair.height),
            "triple-buffer accumulated repair failed");
    for (std::uint32_t y = 0; y < height; ++y)
        for (std::uint32_t x = 0; x < width; ++x)
            require(readPixel(frames[2], x, y) == readPixel(frames[active], x, y),
                    "triple-buffer repair left stale pixels behind");
}

void testPacked4TriangleFillMatchesPixelWrites() {
    using namespace ql32::platform::olimex_vga;
    constexpr std::uint32_t width = 96;
    constexpr std::uint32_t height = 96;
    constexpr std::size_t stride = width / 2;
    std::array<std::uint8_t, stride * height> spanStorage{};
    std::array<std::uint8_t, stride * height> pixelStorage{};
    FramebufferView spans{spanStorage.data(), width, height, stride,
                          PixelFormat::Indexed4};
    FramebufferView pixels{pixelStorage.data(), width, height, stride,
                           PixelFormat::Indexed4};

    for (std::uint32_t y = 0; y < height; ++y) {
        fillRect(spans, 0, static_cast<std::int32_t>(y),
                 static_cast<std::int32_t>(y + 1), 1, 4);
        for (std::uint32_t x = 0; x <= y; ++x)
            (void)writePixel(pixels, x, y, 4);
    }
    require(spanStorage == pixelStorage,
            "packed4 span and pixel writers disagree on triangle pixel order");
}

void testDmaPixelOrderPermutation() {
    using ql32::platform::olimex_vga::applyDmaPixelOrder;
    std::array<std::uint8_t, 16> row{};
    for (std::uint8_t pixel = 0; pixel < row.size(); ++pixel)
        row[pixel] = pixel;

    applyDmaPixelOrder(row.data(), row.size());
    const std::array<std::uint8_t, 16> expected{{
        2, 3, 0, 1, 6, 7, 4, 5, 10, 11, 8, 9, 14, 15, 12, 13}};
    require(row == expected, "DMA pixel order must be applied per eight-pixel group");
}

void testPacked4ShiftedRowEncoderMatchesScalar() {
    using namespace ql32::platform::olimex_vga;
    constexpr std::size_t width = 32;
    std::array<std::uint8_t, width / 2> source{};
    for (std::size_t i = 0; i < source.size(); ++i)
        source[i] = static_cast<std::uint8_t>((i * 37u) ^ (i << 3));

    std::array<std::uint16_t, 256> pairLut{};
    for (std::uint32_t key = 0; key < pairLut.size(); ++key)
        pairLut[key] = static_cast<std::uint16_t>(
            (key >> 4) | ((key & 0x0Fu) << 8));

    for (const std::size_t shift : {0u, 1u, 7u, 15u, 31u}) {
        std::array<std::uint8_t, width> expected{};
        std::array<std::uint8_t, width> actual{};
        for (std::size_t x = 0; x < width; ++x) {
            const auto index = (x + shift) % width;
            const auto packed = source[index / 2];
            expected[x] = static_cast<std::uint8_t>(
                (index & 1u) ? (packed & 0x0Fu) : (packed >> 4));
        }
        applyDmaPixelOrder(expected.data(), expected.size());
        encodePacked4ShiftedRow(source.data(), actual.data(), width, shift,
                                pairLut.data());
        require(actual == expected,
                "packed4 shifted row encoder must match scalar output and DMA pixel order");
    }
}

void testPacked4AlignedRowEncoderMatchesReference() {
    using namespace ql32::platform::olimex_vga;
    std::array<std::uint16_t, 256> pairLut{};
    for (std::uint32_t key = 0; key < pairLut.size(); ++key)
        pairLut[key] = static_cast<std::uint16_t>(
            ((key * 29u) & 0xffu) | (((key * 7u + 3u) & 0xffu) << 8));

    for (const std::size_t width : {8u, 16u, 320u, 640u, 20u}) {
        alignas(4) std::array<std::uint8_t, 640 / 2> source{};
        for (std::size_t i = 0; i < source.size(); ++i)
            source[i] = static_cast<std::uint8_t>((i * 37u) ^ (i << 3));
        alignas(4) std::array<std::uint8_t, 640> expected{};
        alignas(4) std::array<std::uint8_t, 640> actual{};
        encodePacked4ShiftedRow(source.data(), expected.data(), width, 0, pairLut.data());
        const auto written = encodePacked4AlignedRow(source.data(), actual.data(),
                                                     width, pairLut.data());
        require(written == width / 8 * 8, "aligned encoder reports whole groups");
        require(std::equal(expected.begin(), expected.begin() + written, actual.begin()),
                "aligned word encoder must match the reference row encoder");
    }
}

void testLinesAndBitmapCopies() {
    using namespace ql32::platform::olimex_vga;
    std::array<std::uint8_t, 24> rgbStorage{};
    FramebufferView rgb{rgbStorage.data(), 4, 4, 6, PixelFormat::Rgb222};
    drawHorizontalLine(rgb, -1, 1, 4, 3);
    drawVerticalLine(rgb, 3, 0, 4, 5);
    require(readPixel(rgb, 0, 1) == 3 && readPixel(rgb, 2, 1) == 3,
            "clipped horizontal line");
    require(readPixel(rgb, 3, 0) == 5 && readPixel(rgb, 3, 3) == 5,
            "vertical line");

    const std::array<std::uint8_t, 8> rgbBitmap{{1, 2, 3, 4, 5, 6, 7, 8}};
    BitmapView rgbSource{rgbBitmap.data(), 4, 2, 4, PixelFormat::Rgb222};
    copyBitmapOpaque(rgb, 1, 2, rgbSource, 0, 0, 4, 2);
    require(readPixel(rgb, 1, 2) == 1 && readPixel(rgb, 3, 3) == 7,
            "RGB222 opaque bitmap copy");

    const std::array<std::uint8_t, 3> packedBitmap{{0x12, 0x34, 0x50}};
    BitmapView packedSource{packedBitmap.data(), 5, 1, 3, PixelFormat::Indexed4};
    std::array<std::uint8_t, 8> packedStorage{};
    FramebufferView packed{packedStorage.data(), 5, 2, 3, PixelFormat::Indexed4};
    copyBitmapOpaque(packed, -1, 1, packedSource, 0, 0, 5, 1);
    const std::array<std::uint8_t, 5> expected{{2, 3, 4, 5, 0}};
    for (std::uint32_t x = 0; x < 4; ++x)
        require(readPixel(packed, x, 1) == expected[x], "clipped packed4 bitmap copy");
}

void testPaletteOperationsAndStagedSwap() {
    using namespace ql32::platform::olimex_vga;
    Palette palette;
    const auto initial = palette.entries();
    palette.rotate(1);
    palette.rotate(-1);
    require(palette.entries() == initial, "signed palette rotation");

    palette.shift(Palette::Direction::Left, 0x3f);
    require(palette[15] == 0x3f, "left palette shift insertion");
    palette.shift(Palette::Direction::Right, 0x00);
    require(palette[0] == 0x00, "right palette shift insertion");

    Palette::Entries white{};
    white.fill(0x3f);
    palette.load(Palette::Entries{});
    palette.fadeTo(white, 1, 2);
    require(palette[0] == 0x2a, "RGB222 palette fade interpolation");
    palette.save();
    palette.set(0, 0x15);
    palette.restore();
    require(palette[0] == 0x2a, "palette restore");

    PaletteExchange exchange;
    const auto before = exchange.active()[0];
    auto* edit = exchange.beginEdit();
    require(edit != nullptr, "begin palette edit");
    require(edit->set(0, 0x2a), "set staged palette entry");
    require(exchange.publish(), "publish palette edit");
    require(exchange.active()[0] == before, "staged palette leaked before frame boundary");
    require(exchange.beginEdit() == nullptr, "edit allowed while swap pending");
    require(exchange.applyAtFrameBoundary(), "pending palette not applied");
    require(exchange.active()[0] == 0x2a, "palette swap missing after frame boundary");
    require(!exchange.applyAtFrameBoundary(), "palette swapped twice");
}

void testRefillWindowDropsObsoleteRows() {
    using ql32::platform::olimex_vga::makeRefillWindow;
    const auto startup = makeRefillWindow(0, 63, 128, 64);
    require(startup.firstSequence == 0 && startup.rowCount() == 64 &&
            startup.skippedRows == 0, "initial refill window");

    const auto steady = makeRefillWindow(64, 127, 128, 64);
    require(steady.firstSequence == 64 && steady.rowCount() == 64 &&
            steady.skippedRows == 0, "steady refill window");

    const auto delayed = makeRefillWindow(896, 1087, 128, 64);
    require(delayed.firstSequence == 1024 && delayed.rowCount() == 64 &&
            delayed.skippedRows == 128,
            "delayed refill must skip expired ring rows");
    require(delayed.firstSequence + 128 == 1152,
            "delayed refill must prepare rows beyond the next deadline");

    const auto noWork = makeRefillWindow(1088, 1087, 128, 64);
    require(noWork.rowCount() == 0 && noWork.skippedRows == 0,
            "refill window with no completed sequence");

    const auto ring200First = makeRefillWindow(0, 49, 200, 50);
    const auto ring200Second = makeRefillWindow(50, 99, 200, 50);
    require(ring200First.rowCount() == 50 && ring200First.skippedRows == 0 &&
            ring200Second.firstSequence == 50 && ring200Second.rowCount() == 50,
            "200-row ring must retain sequential 50-row refill batches");
    const auto ring400 = makeRefillWindow(0, 49, 400, 50);
    require(ring400.rowCount() == 50 && ring400.skippedRows == 0,
            "400-row ring must retain initial 50-row refill batch");
}

void testBlitsScrollAndTranslation() {
    using namespace ql32::platform::olimex_vga;
    std::array<std::uint8_t, 32> screenBytes{};
    FramebufferView screen{screenBytes.data(), 5, 4, 8, PixelFormat::Rgb222};
    for (std::uint32_t y = 0; y < screen.height; ++y)
        fillRect(screen, 0, static_cast<std::int32_t>(y), 5, 1,
                 static_cast<std::uint8_t>(y + 1));
    scrollVertical(screen, 1, 9);
    require(readPixel(screen, 0, 0) == 2 && readPixel(screen, 4, 2) == 4 &&
            readPixel(screen, 0, 3) == 9, "scroll up and clear exposed row");
    scrollVertical(screen, -2, 7);
    require(readPixel(screen, 0, 0) == 7 && readPixel(screen, 0, 1) == 7 &&
            readPixel(screen, 0, 2) == 2, "scroll down and clear exposed rows");

    const std::array<std::uint8_t, 4> transparentPixels{{0, 3, 0, 5}};
    BitmapView transparent{transparentPixels.data(), 4, 1, 4, PixelFormat::Rgb222};
    copyBitmapTransparent(screen, 0, 2, transparent, 0, 0, 4, 1, 0);
    require(readPixel(screen, 0, 2) == 2 && readPixel(screen, 1, 2) == 3 &&
            readPixel(screen, 2, 2) == 2 && readPixel(screen, 3, 2) == 5,
            "transparent bitmap copy");

    std::array<std::uint8_t, 12> bitmapBytes{};
    MutableBitmapView bitmap{bitmapBytes.data(), 4, 3, 4, PixelFormat::Rgb222};
    copyFramebufferToBitmap(screen, bitmap, 1, 1, 0, 0, 3, 2);
    require(bitmapBytes[5] == readPixel(screen, 0, 0) &&
            bitmapBytes[10] == readPixel(screen, 2, 1), "screen-to-bitmap copy");

    std::array<std::uint8_t, 12> indexedBytes{};
    FramebufferView indexed{indexedBytes.data(), 5, 2, 3, PixelFormat::Indexed4};
    clear(indexed, 2);
    indexedBytes[2] = static_cast<std::uint8_t>((indexedBytes[2] & 0xF0u) | 0x0Au);
    std::array<std::uint8_t, 16> reverse{};
    for (std::uint8_t i = 0; i < reverse.size(); ++i)
        reverse[i] = static_cast<std::uint8_t>(15 - i);
    translateIndexed4(indexed, reverse);
    require(readPixel(indexed, 0, 0) == 13 && readPixel(indexed, 4, 0) == 13 &&
            (indexedBytes[2] & 0x0Fu) == 0x0Au, "packed4 translation/padding nibble");
}

void testGeometricPrimitivesAndGlyphRows() {
    using namespace ql32::platform::olimex_vga;
    std::array<std::uint8_t, 49> storage{};
    FramebufferView framebuffer{storage.data(), 7, 7, 7, PixelFormat::Rgb222};
    drawLine(framebuffer, -5, -5, 5, 5, 3);
    for (std::uint32_t i = 0; i < 6; ++i)
        require(readPixel(framebuffer, i, i) == 3, "clipped diagonal line");

    clear(framebuffer, 0);
    drawCircle(framebuffer, 3, 3, 2, 4);
    require(readPixel(framebuffer, 3, 1) == 4 && readPixel(framebuffer, 5, 3) == 4 &&
            readPixel(framebuffer, 3, 3) == 0, "circle outline");
    clear(framebuffer, 0);
    drawCircle(framebuffer, 3, 3, 2, 5, true);
    require(readPixel(framebuffer, 3, 3) == 5 && readPixel(framebuffer, 1, 3) == 5,
            "filled circle spans");

    const std::array<std::uint8_t, 1> mask{{0b10100000}};
    drawGlyphRow(framebuffer, 1, 5, mask.data(), 4, 6, true, 1);
    require(readPixel(framebuffer, 1, 5) == 6 && readPixel(framebuffer, 2, 5) == 1 &&
            readPixel(framebuffer, 3, 5) == 6 && readPixel(framebuffer, 4, 5) == 1,
            "opaque glyph row");
}

} // namespace

int main() {
    testOlimexColorMapping();
    testRgb222ClippingAndOverlap();
    testPacked4OddWidthAndOverlap();
    testMappedRowWrites();
    testChunkedMappedRowMatchesPerByte();
    testVgaModeMemoryPreflight();
    testRegionSynchronization();
    testIncrementalTripleBufferRepair();
    testPacked4TriangleFillMatchesPixelWrites();
    testDmaPixelOrderPermutation();
    testPacked4ShiftedRowEncoderMatchesScalar();
    testPacked4AlignedRowEncoderMatchesReference();
    testLinesAndBitmapCopies();
    testPaletteOperationsAndStagedSwap();
    testRefillWindowDropsObsoleteRows();
    testBlitsScrollAndTranslation();
    testGeometricPrimitivesAndGlyphRows();
}
