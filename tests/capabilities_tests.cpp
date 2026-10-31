#include "ql32/platform/capabilities.hpp"

#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void testCompleteProfile() {
    const ql32::platform::Capabilities profile{
        "test_tft",
        ql32::platform::DisplayCapabilities{480, 320,
            ql32::platform::PixelFormat::Rgb565,
            ql32::platform::Orientation::Landscape, 1.0F, 1.0F},
        ql32::platform::capabilityBit(ql32::platform::ServiceKind::Display) |
            ql32::platform::capabilityBit(ql32::platform::ServiceKind::Input) |
            ql32::platform::capabilityBit(ql32::platform::ServiceKind::Gpio),
        ql32::platform::inputBit(ql32::platform::InputKind::Touch) |
            ql32::platform::inputBit(ql32::platform::InputKind::UsbHid)
    };
    require(profile.valid(), "valid display/input capability profile was rejected");
    require(profile.has(ql32::platform::ServiceKind::Gpio),
            "available GPIO service was not reported");
    require(profile.has(ql32::platform::InputKind::Touch) &&
                !profile.has(ql32::platform::InputKind::Ps2),
            "input capability query returned the wrong inputs");
}

void testMissingHardwareAndInvalidDisplay() {
    const ql32::platform::Capabilities headless{
        "linux_test", std::nullopt,
        ql32::platform::capabilityBit(ql32::platform::ServiceKind::Storage), 0
    };
    require(headless.valid() && !headless.has(ql32::platform::ServiceKind::Display),
            "headless profile did not represent unavailable display hardware");

    auto invalid = headless;
    invalid.services |= ql32::platform::capabilityBit(
        ql32::platform::ServiceKind::Display);
    require(!invalid.valid(), "display service without display details was accepted");

    invalid = {
        "bad_scale",
        ql32::platform::DisplayCapabilities{320, 240,
            ql32::platform::PixelFormat::Rgb565,
            ql32::platform::Orientation::Landscape, 0.0F, 1.0F},
        ql32::platform::capabilityBit(ql32::platform::ServiceKind::Display), 0
    };
    require(!invalid.valid(), "zero display scale was accepted");
}

} // namespace

int main() {
    try {
        testCompleteProfile();
        testMissingHardwareAndInvalidDisplay();
    } catch (const std::exception& error) {
        std::cerr << "capabilities test failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
