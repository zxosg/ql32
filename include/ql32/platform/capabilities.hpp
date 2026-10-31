#pragma once

#include <cmath>
#include <cstdint>
#include <optional>
#include <string_view>

namespace ql32::platform {

enum class ServiceKind : std::uint8_t {
    Display, Input, Storage, Audio, Network, Gpio, Clock, Channels
};

enum class InputKind : std::uint8_t { Keyboard, Touch, Buttons, Ps2, UsbHid, Serial };

enum class PixelFormat : std::uint8_t { Indexed1, Indexed2, Indexed3, Indexed4, Rgb565, Rgb888 };

enum class Orientation : std::uint8_t {
    Landscape, Portrait, InvertedLandscape, InvertedPortrait
};

constexpr std::uint32_t capabilityBit(ServiceKind kind) {
    return std::uint32_t{1} << static_cast<std::uint8_t>(kind);
}

constexpr std::uint32_t inputBit(InputKind kind) {
    return std::uint32_t{1} << static_cast<std::uint8_t>(kind);
}

struct DisplayCapabilities {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    PixelFormat pixelFormat = PixelFormat::Rgb565;
    Orientation orientation = Orientation::Landscape;
    float scaleX = 1.0F;
    float scaleY = 1.0F;
};

// Filled by a board or host adapter during startup. The portable core uses
// this descriptor to avoid assuming that optional hardware services exist.
struct Capabilities {
    std::string_view profile;
    std::optional<DisplayCapabilities> display;
    std::uint32_t services = 0;
    std::uint32_t inputs = 0;

    constexpr bool has(ServiceKind kind) const {
        return (services & capabilityBit(kind)) != 0;
    }

    constexpr bool has(InputKind kind) const {
        return (inputs & inputBit(kind)) != 0;
    }

    bool valid() const {
        if (profile.empty()) return false;
        constexpr std::uint32_t knownServices = (std::uint32_t{1} << 8) - 1;
        constexpr std::uint32_t knownInputs = (std::uint32_t{1} << 6) - 1;
        if ((services & ~knownServices) != 0 || (inputs & ~knownInputs) != 0) return false;
        if (has(ServiceKind::Display) != display.has_value()) return false;
        if (display && (display->width == 0 || display->height == 0 ||
                        !std::isfinite(display->scaleX) ||
                        !std::isfinite(display->scaleY) ||
                        display->scaleX <= 0 || display->scaleY <= 0)) return false;
        if (has(ServiceKind::Input) != (inputs != 0)) return false;
        return true;
    }
};

class CapabilitiesProvider {
public:
    virtual ~CapabilitiesProvider() = default;
    virtual const Capabilities& capabilities() const noexcept = 0;
};

} // namespace ql32::platform
