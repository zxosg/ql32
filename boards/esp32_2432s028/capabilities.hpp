#pragma once

#include "ql32/platform/capabilities.hpp"

namespace ql32::boards::esp32_2432s028 {

constexpr platform::Capabilities capabilities() {
    return {
        "esp32_2432s028_yellow_tft",
        platform::DisplayCapabilities{
            320, 240, platform::PixelFormat::Rgb565,
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
        platform::inputBit(platform::InputKind::Serial)
    };
}

} // namespace ql32::boards::esp32_2432s028
