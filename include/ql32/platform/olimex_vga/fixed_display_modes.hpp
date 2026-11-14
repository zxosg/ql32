#pragma once

#include "ql32/runtime/display_mode.hpp"

namespace ql32::platform::olimex_vga {

// The first DMA display target intentionally exposes only its qualified mode.
class FixedDisplayModes final : public runtime::DisplayModeService {
public:
    std::vector<runtime::DisplayMode> availableModes() const override {
        return {{"320x200", 320, 200, 70, 0, 0}};
    }

    std::string currentMode() const override { return "320x200"; }

    runtime::DisplayMode setMode(std::string_view name) override {
        if (name == "320x200" || name == "320X200") return {"320x200", 320, 200, 70, 0, 0};
        throw std::invalid_argument("unsupported Olimex DMA mode: " + std::string(name));
    }

    runtime::DisplayMode setMode(std::uint32_t id,
                                 std::uint32_t options) override {
        if (id != 0) throw std::invalid_argument("unsupported Olimex DMA mode id");
        if (options != 0) throw std::invalid_argument("Olimex DMA mode options are unsupported");
        return {"320x200", 320, 200, 70, 0, 0};
    }
};

} // namespace ql32::platform::olimex_vga
