#pragma once

#include "ql32/platform/capabilities.hpp"

namespace ql32::boards::olimex_esp32_sbc_fabgl {

constexpr platform::Capabilities capabilities() {
    return {
        "olimex_esp32_sbc_fabgl_rev_b",
        platform::DisplayCapabilities{
            320, 200, platform::PixelFormat::Indexed3,
            platform::Orientation::Landscape, 1.0F, 1.0F
        },
        platform::capabilityBit(platform::ServiceKind::Display) |
            platform::capabilityBit(platform::ServiceKind::Input) |
            platform::capabilityBit(platform::ServiceKind::Storage) |
            platform::capabilityBit(platform::ServiceKind::Network) |
            platform::capabilityBit(platform::ServiceKind::Clock) |
            platform::capabilityBit(platform::ServiceKind::Channels),
        platform::inputBit(platform::InputKind::Ps2)
    };
}

} // namespace ql32::boards::olimex_esp32_sbc_fabgl
