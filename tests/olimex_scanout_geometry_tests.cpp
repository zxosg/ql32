#include "ql32/platform/olimex_vga/scanout_geometry.hpp"

#include <cassert>
#include <initializer_list>

int main() {
    using namespace ql32::platform::olimex_vga;
    constexpr ScanoutGeometry modes[] = {{320, 200, 100, 50},
        {320, 240, 120, 60}, {400, 300, 150, 75},
        {640, 400, 100, 50}, {640, 480, 120, 60}};
    for (const auto expected : modes) {
        const auto actual = scanoutGeometry(expected.width, expected.height);
        assert(actual.valid());
        assert(actual.ringRows == expected.ringRows);
        assert(actual.refillRows == expected.refillRows);
    }
    // Single-framebuffer modes: packed4 framebuffer bytes and the internal DMA
    // line ring (1 byte/pixel) must stay within the qualified 400x300 budget,
    // and refill groups must give at least ~60 rows of lead.
    for (const auto mode : {scanoutGeometry(400, 300), scanoutGeometry(320, 240),
                            scanoutGeometry(320, 200)}) {
        assert(mode.width / 2 * mode.height <= 60000);
        assert(mode.width * mode.ringRows <= 60000);
        assert(mode.ringRows - mode.refillRows >= 50);
    }
    assert(scanoutGeometry(320, 240).width / 2 * scanoutGeometry(320, 240).height == 38400);
    assert(scanoutGeometry(320, 200).width / 2 * scanoutGeometry(320, 200).height == 32000);
    assert(!scanoutGeometry(800, 600).valid());
    assert(!(ScanoutGeometry{320, 240, 100, 50}.valid()));
    assert(!(ScanoutGeometry{320, 200, 100, 30}.valid()));
    assert(!(ScanoutGeometry{320, 200, 201, 1}.valid()));
    assert(!(ScanoutGeometry{318, 200, 100, 50}.valid()));
}
