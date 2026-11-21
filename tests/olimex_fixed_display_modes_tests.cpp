#include <string>
#include "ql32/platform/olimex_vga/fixed_display_modes.hpp"

#include <cassert>
#include <stdexcept>

int main() {
    using ql32::platform::olimex_vga::FixedDisplayModes;
#if defined(QL32_OLIMEX_DMA_TEST_MODE_640X480)
    constexpr unsigned expectedId = 4;
#elif defined(QL32_OLIMEX_DMA_TEST_MODE_640X400)
    constexpr unsigned expectedId = 3;
#elif defined(QL32_OLIMEX_DMA_TEST_MODE_400X300)
    constexpr unsigned expectedId = 2;
#else
    constexpr unsigned expectedId = 0;
#endif
    FixedDisplayModes modes;
    const auto available = modes.availableModes();
    assert(available.size() == 1);
    assert(available[0].id == expectedId);
    assert(modes.currentDisplayMode().id == expectedId);
    assert(modes.setMode(FixedDisplayModes::kModeId, 0).width ==
           FixedDisplayModes::kModeWidth);
    assert(modes.setMode(FixedDisplayModes::kModeName).height ==
           FixedDisplayModes::kModeHeight);
    bool rejected = false;
    try { modes.setMode((FixedDisplayModes::kModeId + 1) % 5, 0); }
    catch (const std::invalid_argument& error) {
        rejected = true;
        const std::string message = error.what();
        assert(message.find("unavailable on this fixed-mode build") != std::string::npos);
        assert(message.find(FixedDisplayModes::kModeName) != std::string::npos);
    }
    assert(rejected);
    rejected = false;
    try { modes.setMode(FixedDisplayModes::kModeId, 1); }
    catch (const std::invalid_argument&) { rejected = true; }
    assert(rejected);
}
