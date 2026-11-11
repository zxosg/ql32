#include "capabilities.hpp"
#include "pins.hpp"
#include "ql32/platform/esp32_cyd/touch_transform.hpp"

#include <cassert>

int main() {
    using namespace ql32::boards::esp32_2432s028;
    assert(tftSck == 14 && tftMiso == 12 && tftMosi == 13);
    assert(tftChipSelect == 15 && tftDataCommand == 2 && tftBacklight == 21);
    assert(touchSck == 25 && touchMiso == 39 && touchMosi == 32 &&
           touchChipSelect == 33 && touchInterrupt == 36);
    assert(sdSck == 18 && sdMiso == 19 && sdMosi == 23 && sdChipSelect == 5);

    constexpr auto profile = capabilities();
    static_assert(profile.display->width == 320 && profile.display->height == 240);
    static_assert(profile.display->pixelFormat == ql32::platform::PixelFormat::Rgb565);
    assert(profile.valid());
    assert(profile.has(ql32::platform::ServiceKind::Display));
    assert(profile.has(ql32::platform::ServiceKind::Input));
    assert(profile.has(ql32::platform::ServiceKind::Storage));
    assert(profile.has(ql32::platform::ServiceKind::Network));
    assert(profile.has(ql32::platform::ServiceKind::Channels));
    assert(profile.has(ql32::platform::InputKind::Serial));
    assert(profile.has(ql32::platform::InputKind::Touch));

    using ql32::platform::esp32_cyd::mapRawTouchPoint;
    const auto topLeft = mapRawTouchPoint(250, 3850, 320, 240, 0);
    const auto topRight = mapRawTouchPoint(3850, 3850, 320, 240, 0);
    const auto bottomLeft = mapRawTouchPoint(250, 250, 320, 240, 0);
    const auto bottomRight = mapRawTouchPoint(3850, 250, 320, 240, 0);
    assert(topLeft.x == 0 && topLeft.y == 0);
    assert(topRight.x == 319 && topRight.y == 0);
    assert(bottomLeft.x == 0 && bottomLeft.y == 239);
    assert(bottomRight.x == 319 && bottomRight.y == 239);
}
