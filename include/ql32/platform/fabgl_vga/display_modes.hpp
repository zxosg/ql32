#pragma once

#include "ql32/runtime/display_mode.hpp"

#include <fabgl.h>

namespace ql32::platform::fabgl_vga {

class DisplayModes final : public runtime::DisplayModeService {
public:
    DisplayModes(fabgl::VGAController& display, fabgl::Canvas& canvas);

    std::vector<runtime::DisplayMode> availableModes() const override;
    std::string currentMode() const override;
    runtime::DisplayMode setMode(std::string_view name) override;

private:
    void apply(std::size_t index);

    fabgl::VGAController& display_;
    fabgl::Canvas& canvas_;
    std::size_t current_ = 0;
};

} // namespace ql32::platform::fabgl_vga
