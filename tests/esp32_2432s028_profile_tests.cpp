#include "capabilities.hpp"
#include "pins.hpp"

#include <cassert>

int main() {
    using namespace ql32::boards::esp32_2432s028;
    assert(tftSck == 14 && tftMiso == 12 && tftMosi == 13);
    assert(tftChipSelect == 15 && tftDataCommand == 2 && tftBacklight == 21);
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
    assert(!profile.has(ql32::platform::InputKind::Touch));
}
