#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace ql32::runtime {

struct DisplayMode {
    std::string name;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint16_t refreshHz = 0;
    std::uint32_t id = 0;
    std::uint32_t options = 0;
};

// Platform-neutral control for system-wide display modes. Implementations
// translate names to hardware timings and report the usable viewport size.
class DisplayModeService {
public:
    virtual ~DisplayModeService() = default;
    virtual std::vector<DisplayMode> availableModes() const = 0;
    virtual std::string currentMode() const = 0;
    virtual DisplayMode setMode(std::string_view name) = 0;

    virtual DisplayMode currentDisplayMode() const {
        const std::string name = currentMode();
        for (auto mode : availableModes())
            if (mode.name == name) return mode;
        throw std::runtime_error("current display mode is not in the available mode list");
    }

    virtual DisplayMode setMode(std::uint32_t id, std::uint32_t options) {
        if (options != 0)
            throw std::invalid_argument("display options are unsupported by this platform");
        for (const auto& mode : availableModes()) {
            if (mode.id == id) {
                auto selected = setMode(mode.name);
                selected.id = id;
                selected.options = 0;
                return selected;
            }
        }
        throw std::invalid_argument("unsupported display mode id: " + std::to_string(id));
    }
};

} // namespace ql32::runtime
