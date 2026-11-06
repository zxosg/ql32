#include "capabilities.hpp"
#include "pins.hpp"

#include <cassert>

int main() {
    using namespace ql32::boards::tenstar_esp32s3;
    assert(tftMosi == 35 && tftMiso == 37 && tftSck == 36);
    assert(tftChipSelect == 7 && tftDataCommand == 39 && tftReset == 40);
    assert(tftBacklight == 45 && i2cSda == 42 && i2cScl == 41);

    constexpr auto profile = capabilities();
    static_assert(profile.display->width == 240 && profile.display->height == 135);
    static_assert(profile.display->pixelFormat == ql32::platform::PixelFormat::Rgb565);
    assert(profile.valid());
    assert(profile.has(ql32::platform::ServiceKind::Display));
    assert(profile.has(ql32::platform::ServiceKind::Input));
    assert(profile.has(ql32::platform::ServiceKind::Storage));
    assert(profile.has(ql32::platform::ServiceKind::Channels));
    assert(profile.has(ql32::platform::InputKind::Serial));
    assert(!profile.has(ql32::platform::ServiceKind::CapTouch));
}
