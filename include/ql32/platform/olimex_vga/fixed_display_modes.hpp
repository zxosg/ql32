#pragma once

#include "ql32/runtime/display_mode.hpp"

#include <cstddef>
#include <string>

namespace ql32::platform::olimex_vga {

// Each DMA firmware profile exposes its build-selected timing as the only
// available mode until the scanout engine supports live modeline changes.
class FixedDisplayModes final : public runtime::DisplayModeService {
public:
#if defined(QL32_OLIMEX_DMA_TEST_MODE_640X480)
    static constexpr char kModeName[] = "640x480";
    static constexpr std::uint32_t kModeWidth = 640;
    static constexpr std::uint32_t kModeHeight = 480;
    static constexpr std::uint16_t kModeRefresh = 60;
    static constexpr std::uint32_t kModeId = 4;
#elif defined(QL32_OLIMEX_DMA_TEST_MODE_640X400)
    static constexpr char kModeName[] = "640x400";
    static constexpr std::uint32_t kModeWidth = 640;
    static constexpr std::uint32_t kModeHeight = 400;
    static constexpr std::uint16_t kModeRefresh = 70;
    static constexpr std::uint32_t kModeId = 3;
#elif defined(QL32_OLIMEX_DMA_TEST_MODE_400X300)
    static constexpr char kModeName[] = "400x300";
    static constexpr std::uint32_t kModeWidth = 400;
    static constexpr std::uint32_t kModeHeight = 300;
    static constexpr std::uint16_t kModeRefresh = 60;
    static constexpr std::uint32_t kModeId = 2;
#else
    static constexpr char kModeName[] = "320x200";
    static constexpr std::uint32_t kModeWidth = 320;
    static constexpr std::uint32_t kModeHeight = 200;
    static constexpr std::uint16_t kModeRefresh = 70;
    static constexpr std::uint32_t kModeId = 0;
#endif

    std::vector<runtime::DisplayMode> availableModes() const override {
        return {{kModeName, kModeWidth, kModeHeight, kModeRefresh, kModeId, 0}};
    }

    std::string currentMode() const override { return kModeName; }

    runtime::DisplayMode setMode(std::string_view name) override {
        bool matches = name.size() == sizeof(kModeName) - 1;
        for (std::size_t index = 0; matches && index < name.size(); ++index) {
            auto requested = name[index];
            auto supported = kModeName[index];
            if (requested >= 'a' && requested <= 'z') requested -= 'a' - 'A';
            if (supported >= 'a' && supported <= 'z') supported -= 'a' - 'A';
            matches = requested == supported;
        }
        if (matches) {
            return {kModeName, kModeWidth, kModeHeight, kModeRefresh, kModeId, 0};
        }
        throw std::invalid_argument("unsupported Olimex DMA mode: " + std::string(name));
    }

    runtime::DisplayMode setMode(std::uint32_t id,
                                 std::uint32_t options) override {
        if (id != kModeId)
            throw std::invalid_argument(
                "mode " + std::to_string(id) + " is unavailable on this fixed-mode build; only mode " +
                std::to_string(kModeId) + " (" + kModeName + ") is supported");
        if (options != 0) throw std::invalid_argument("Olimex DMA mode options are unsupported");
        return {kModeName, kModeWidth, kModeHeight, kModeRefresh, kModeId, 0};
    }
};

} // namespace ql32::platform::olimex_vga
