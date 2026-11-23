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

    // Small writes never hold; crossing one SPIFFS block holds exactly once.
    Counts counts;
    CountedBulkWriteGate gate(onBegin, onEnd, &counts);
    {
        BulkWriteTracker small(&gate);
        small.add(1000);
        small.add(2000);
        assert(!small.held() && counts.begins == 0);
        BulkWriteTracker big(&gate);
        big.add(1024); big.add(1024); big.add(2048);
        assert(big.held() && counts.begins == 1);
        big.add(1024);
        assert(counts.begins == 1);
        // A second concurrent stream shares the one hold.
        BulkWriteTracker other(&gate);
        other.add(8192);
        assert(counts.begins == 1 && gate.activeHolds() == 2);
        other.finish();
        assert(counts.ends == 0 && gate.activeHolds() == 1);
    }
    // Destruction releases (error paths), exactly once.
    assert(counts.ends == 1 && gate.activeHolds() == 0);

    // finish() is idempotent; a moved tracker keeps a single release.
    {
        BulkWriteTracker a(&gate);
        a.add(4096);
        BulkWriteTracker b(std::move(a));
        assert(!a.held() && b.held());
        b.finish(); b.finish();
        assert(counts.begins == 2 && counts.ends == 2);
    }
    // No gate: tracker is inert.
    BulkWriteTracker none;
    none.add(1u << 20);
    assert(!none.held());
    // Unbalanced release is ignored.
    gate.release();
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
    return 0;
}
