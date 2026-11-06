#pragma once

#include "ql32/platform/capabilities.hpp"

namespace ql32::boards::tenstar_esp32s3 {

constexpr platform::Capabilities capabilities() {
    return {
        "tenstar_ts_esp32s3_st7789",
        platform::DisplayCapabilities{
            240, 135, platform::PixelFormat::Rgb565,
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
            platform::capabilityBit(platform::ServiceKind::Timer),
        platform::inputBit(platform::InputKind::Serial)
    };
}

} // namespace ql32::boards::tenstar_esp32s3
