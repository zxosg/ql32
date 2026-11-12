#include "ql32/platform/fabgl_vga/display_modes.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <stdexcept>

namespace ql32::platform::fabgl_vga {
namespace {

struct ModePreset {
    const char* name;
    const char* modeline;
    std::uint32_t width;
    std::uint32_t height;
    std::uint16_t refreshHz;
};

constexpr std::array<ModePreset, 5> presets{{
    {"320x200", VGA_320x200_70Hz, 320, 200, 70},
    {"320x240", QVGA_320x240_60Hz, 320, 240, 60},
    {"400x300", VGA_400x300_60Hz, 400, 300, 60},
    {"640x400", VGA_640x400_70Hz, 640, 400, 70},
    {"640x480", VGA_640x480_60Hz, 640, 480, 60},
}};

std::string normalized(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (const unsigned char ch : value)
        result.push_back(static_cast<char>(std::toupper(ch)));
    return result;
}

} // namespace

DisplayModes::DisplayModes(fabgl::VGAController& display, fabgl::Canvas& canvas)
    : display_(display), canvas_(canvas) {}

std::vector<runtime::DisplayMode> DisplayModes::availableModes() const {
    std::vector<runtime::DisplayMode> result;
    result.reserve(presets.size());
    for (std::size_t index = 0; index < presets.size(); ++index) {
        const auto& preset = presets[index];
        result.push_back({preset.name, preset.width, preset.height,
                          preset.refreshHz, static_cast<std::uint32_t>(index), 0});
    }
    return result;
}

std::string DisplayModes::currentMode() const {
    return presets[current_].name;
}

void DisplayModes::apply(std::size_t index) {
    switch (index) {
    case 0: display_.setResolution(VGA_320x200_70Hz); break;
    case 1: display_.setResolution(QVGA_320x240_60Hz); break;
    case 2: display_.setResolution(VGA_400x300_60Hz); break;
    case 3: display_.setResolution(VGA_640x400_70Hz); break;
    case 4: display_.setResolution(VGA_640x480_60Hz); break;
    default: throw std::out_of_range("unknown VGA display mode");
    }
}

runtime::DisplayMode DisplayModes::setMode(std::string_view name) {
    const auto key = normalized(name);
    const auto selected = std::find_if(presets.begin(), presets.end(), [&key](const auto& preset) {
        return normalized(preset.name) == key;
    });
    if (selected == presets.end())
        throw std::invalid_argument("unsupported VGA mode: " + std::string(name));
    const auto next = static_cast<std::size_t>(selected - presets.begin());
    const auto previous = current_;
    if (next != previous) {
        try {
            apply(next);
            current_ = next;
        } catch (...) {
            try { apply(previous); } catch (...) {}
            throw;
        }
    }
    return {selected->name,
            static_cast<std::uint32_t>(std::max(0, canvas_.getWidth())),
            static_cast<std::uint32_t>(std::max(0, canvas_.getHeight())),
            selected->refreshHz, static_cast<std::uint32_t>(next), 0};
}

} // namespace ql32::platform::fabgl_vga
