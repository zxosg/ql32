#include "ql32/platform/olimex_vga/hold_message.hpp"
#include "ql32/runtime/bulk_write_gate.hpp"

#include <cassert>
#include <vector>

namespace {
struct Counts { int begins = 0; int ends = 0; };
void onBegin(void* c) { ++static_cast<Counts*>(c)->begins; }
void onEnd(void* c) { ++static_cast<Counts*>(c)->ends; }
}

int main() {
    using namespace ql32::runtime;
    using namespace ql32::platform::olimex_vga;

    // Each scope holds once and always releases, including on exceptions;
    // nested scopes share a single begin/end pair.
    Counts counts;
    CountedBulkWriteGate gate(onBegin, onEnd, &counts);
    {
        BulkWriteScope outer(&gate);
        assert(counts.begins == 1 && gate.activeHolds() == 1);
        {
            BulkWriteScope inner(&gate);
            assert(counts.begins == 1 && gate.activeHolds() == 2);
        }
        assert(counts.ends == 0);
    }
    assert(counts.begins == 1 && counts.ends == 1 && gate.activeHolds() == 0);
    try {
        BulkWriteScope scope(&gate);
        throw 1;
    } catch (int) {}
    assert(counts.begins == 2 && counts.ends == 2);
    BulkWriteScope inert(nullptr);
    gate.release();  // unbalanced release is ignored
    assert(counts.ends == 2);

    // The hold message lies inside the ring, is centred, and is non-empty,
    // for every Olimex single-framebuffer geometry.
    struct Mode { std::uint32_t width, ringRows; };
    for (const Mode mode : {Mode{320, 100}, Mode{320, 120}, Mode{400, 150}}) {
        std::vector<std::uint8_t> mask(mode.width);
        std::uint32_t lit = 0, firstRow = ~0u, lastRow = 0;
        for (std::uint32_t row = 0; row < mode.ringRows; ++row) {
            fillHoldMaskRow(mask.data(), mode.width, mode.ringRows, row);
            std::uint32_t rowLit = 0, minX = ~0u, maxX = 0;
            for (std::uint32_t x = 0; x < mode.width; ++x) {
                if (!mask[x]) continue;
                ++rowLit; minX = x < minX ? x : minX; maxX = x > maxX ? x : maxX;
            }
            lit += rowLit;
            if (rowLit) {
                if (firstRow == ~0u) firstRow = row;
                lastRow = row;
                assert(minX > 0 && maxX < mode.width - 1);
            }
        }
        assert(lit > 100);
        const auto scale = holdMessageScale(mode.width);
        assert(lastRow < mode.ringRows && firstRow > 0);
        assert(lastRow - firstRow + 1 <= kHoldGlyphRows * scale);
        assert(firstRow + lastRow >= mode.ringRows - 2 * scale - 2 &&
               firstRow + lastRow <= mode.ringRows + 2 * scale);
    }
    // Corner notification box: inside the frame, byte-aligned for pixel
    // ordering, red border, yellow text, black fill.
    for (const Mode mode : {Mode{320, 100}, Mode{400, 150}}) {
        const auto box = holdBox(mode.width, mode.ringRows * 2);
        assert(box.valid() && box.x % 8 == 0 && box.width % 8 == 0);
        assert(box.x + box.width <= mode.width && box.y + box.height <= mode.ringRows * 2);
        assert(box.x > mode.width / 2);
        std::uint32_t yellow = 0, red = 0, black = 0;
        std::vector<std::uint8_t> codes(box.width);
        for (std::uint32_t row = 0; row < box.height; ++row) {
            fillHoldBoxRow(codes.data(), box, row);
            for (const auto code : codes) {
                yellow += code == kBoxYellow; red += code == kBoxRed; black += code == kBoxBlack;
            }
        }
        assert(yellow > 100 && red == 2 * box.width + 2 * (box.height - 2) && black > yellow);
    }
    assert(!holdBox(100, 100).valid());
    return 0;

}
