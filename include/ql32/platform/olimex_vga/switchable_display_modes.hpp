#pragma once

#include "ql32/runtime/display_mode.hpp"

#include <array>
#include <functional>
#include <stdexcept>

namespace ql32::platform::olimex_vga {

// The hardware callback must restore the old scanout before throwing on a
// failed transition. The selected mode is published only after it succeeds.
class SwitchableDisplayModes final : public runtime::DisplayModeService {
public:
    using Apply = std::function<void(std::uint32_t, std::uint32_t)>;
    explicit SwitchableDisplayModes(std::uint32_t initial, Apply apply)
        : current_(initial), apply_(std::move(apply)) {
        if (initial >= kModes.size() || !apply_)
            throw std::invalid_argument("invalid initial VGA mode");
    }

    std::vector<runtime::DisplayMode> availableModes() const override {
        return {kModes.begin(), kModes.end()};
    }
    std::string currentMode() const override { return kModes[current_].name; }
    runtime::DisplayMode currentDisplayMode() const override { return kModes[current_]; }
    runtime::DisplayMode setMode(std::string_view name) override {
        for (const auto& mode : kModes) {
            if (name.size() != mode.name.size()) continue;
            bool match = true;
            for (std::size_t i = 0; i < name.size(); ++i) {
                char c = name[i];
                if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
                if (c != mode.name[i]) { match = false; break; }
            }
            if (match) return setMode(mode.id, 0);
        }
        throw std::invalid_argument("unsupported Olimex DMA mode: " + std::string(name));
    }
    runtime::DisplayMode setMode(std::uint32_t id, std::uint32_t options) override {
        if (id >= kModes.size()) throw std::invalid_argument("unsupported Olimex DMA mode id");
        if (options != 0) throw std::invalid_argument("Olimex DMA mode options are unsupported");
        if (id != current_) {
            apply_(id, current_);
            current_ = id;
        }
        return kModes[current_];
    }

private:
#if defined(QL32_OLIMEX_SINGLE_FRAMEBUFFER)
    // The single-framebuffer retro family stops at 400x300; 640-wide timing
    // cannot be met on this path.
    inline static const std::array<runtime::DisplayMode, 3> kModes{{
#else
    inline static const std::array<runtime::DisplayMode, 5> kModes{{
#endif
        {"320x200", 320, 200, 70, 0, 0},
        {"320x240", 320, 240, 60, 1, 0},
        {"400x300", 400, 300, 60, 2, 0},
#if !defined(QL32_OLIMEX_SINGLE_FRAMEBUFFER)
        {"640x400", 640, 400, 70, 3, 0},
        {"640x480", 640, 480, 60, 4, 0},
#endif
    }};
    std::uint32_t current_;
    Apply apply_;
};

} // namespace ql32::platform::olimex_vga
