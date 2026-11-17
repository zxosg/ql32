#include "pins.hpp"
#include "capabilities.hpp"

#include <array>
#include <cassert>
#include <cstddef>

int main() {
    using namespace ql32::boards::olimex_esp32_sbc_fabgl;

    constexpr std::array vgaPins{vgaRed, vgaGreen, vgaBlue, vgaHSync, vgaVSync};
    static_assert(vgaRed == 21 && vgaGreen == 18 && vgaBlue == 5);
    static_assert(vgaHSync == 23 && vgaVSync == 15);
    for (std::size_t i = 0; i < vgaPins.size(); ++i) {
        for (std::size_t j = i + 1; j < vgaPins.size(); ++j)
            assert(vgaPins[i] != vgaPins[j]);
    }

    static_assert(keyboardData == 32 && keyboardClock == 33);
    static_assert(mouseData == 27 && mouseClock == 26);
    static_assert(audio == 25);
    static_assert(sdMosi == 12 && sdMiso == 35 && sdClock == 14 && sdChipSelect == 13);

    constexpr auto profile = capabilities();
    static_assert(profile.display->width == 320 && profile.display->height == 200);
    static_assert(profile.display->pixelFormat == ql32::platform::PixelFormat::Indexed3);
    assert(profile.profile == "olimex_esp32_sbc_fabgl_rev_b");
    assert(profile.valid());
    assert(profile.has(ql32::platform::ServiceKind::Display));
    assert(profile.has(ql32::platform::ServiceKind::Input));
    assert(profile.has(ql32::platform::ServiceKind::Storage));
    assert(profile.has(ql32::platform::ServiceKind::Audio));
    assert(profile.has(ql32::platform::ServiceKind::Network));
    assert(profile.has(ql32::platform::ServiceKind::Clock));
    assert(profile.has(ql32::platform::ServiceKind::Channels));
    assert(profile.has(ql32::platform::ServiceKind::Gpio));
    assert(profile.has(ql32::platform::InputKind::Ps2));
    assert(!profile.has(ql32::platform::InputKind::Touch));
}
