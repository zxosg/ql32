#include "ql32/platform/olimex_vga/scanout_geometry.hpp"

#include <cassert>

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
    assert(!scanoutGeometry(800, 600).valid());
    assert(!(ScanoutGeometry{320, 240, 100, 50}.valid()));
    assert(!(ScanoutGeometry{320, 200, 100, 30}.valid()));
    assert(!(ScanoutGeometry{320, 200, 201, 1}.valid()));
    assert(!(ScanoutGeometry{318, 200, 100, 50}.valid()));
}
