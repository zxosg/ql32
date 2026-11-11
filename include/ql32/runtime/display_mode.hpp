#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ql32::runtime {

struct DisplayMode {
    std::string name;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint16_t refreshHz = 0;
};

// Platform-neutral control for system-wide display modes. Implementations
// translate names to hardware timings and report the usable viewport size.
class DisplayModeService {
public:
    virtual ~DisplayModeService() = default;
    virtual std::vector<DisplayMode> availableModes() const = 0;
    virtual std::string currentMode() const = 0;
    virtual DisplayMode setMode(std::string_view name) = 0;
};

} // namespace ql32::runtime
