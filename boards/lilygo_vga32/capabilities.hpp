#pragma once

#include "ql32/platform/capabilities.hpp"

namespace ql32::boards::lilygo_vga32 {

constexpr platform::Capabilities capabilities() {
    return {
        "lilygo_fabgl_vga32_v1_4",
        platform::DisplayCapabilities{
            320, 200, platform::PixelFormat::Indexed3,
            platform::Orientation::Landscape, 1.0F, 1.0F
        },
        platform::capabilityBit(platform::ServiceKind::Display) |
            platform::capabilityBit(platform::ServiceKind::Input) |
            platform::capabilityBit(platform::ServiceKind::Storage) |
            platform::capabilityBit(platform::ServiceKind::Network) |
            platform::capabilityBit(platform::ServiceKind::Clock) |
            platform::capabilityBit(platform::ServiceKind::Channels) |
            platform::capabilityBit(platform::ServiceKind::Gpio) |
            platform::capabilityBit(platform::ServiceKind::Analog) |
            platform::capabilityBit(platform::ServiceKind::Buses) |
            platform::capabilityBit(platform::ServiceKind::Serial) |
            platform::capabilityBit(platform::ServiceKind::Parallel) |
            platform::capabilityBit(platform::ServiceKind::CapTouch) |
            platform::capabilityBit(platform::ServiceKind::Timer),
        platform::inputBit(platform::InputKind::Ps2)
    };
}

} // namespace ql32::boards::lilygo_vga32
