#pragma once

#include "ql32/runtime/display_mode.hpp"

#include <fabgl.h>

namespace ql32::platform::fabgl_vga {

class DisplayModes final : public runtime::DisplayModeService {
public:
    DisplayModes(fabgl::VGAController& display, fabgl::Canvas& canvas);

    std::vector<runtime::DisplayMode> availableModes() const override;
    std::string currentMode() const override;
    runtime::DisplayMode currentDisplayMode() const override;
    runtime::DisplayMode setMode(std::string_view name) override;
    runtime::DisplayMode setMode(std::uint32_t id,
                                std::uint32_t options) override;

private:
    void apply(std::size_t index, bool doubleBuffered);
    runtime::DisplayMode describe(std::size_t index) const;

    fabgl::VGAController& display_;
    fabgl::Canvas& canvas_;
    std::size_t current_ = 0;
    std::uint32_t currentOptions_ = 0;
};

} // namespace ql32::platform::fabgl_vga
