#include "ql32/platform/olimex_vga/switchable_display_modes.hpp"

#include <cassert>
#include <stdexcept>

int main() {
    using ql32::platform::olimex_vga::SwitchableDisplayModes;
    unsigned physical = 4;
    unsigned calls = 0;
    bool fail = false;
    SwitchableDisplayModes modes(4, [&](unsigned next, unsigned previous) {
        assert(previous == physical);
        ++calls;
        if (fail) throw std::runtime_error("allocation failed");
        physical = next;
    });
    assert(modes.availableModes().size() == 5);
    assert(modes.currentDisplayMode().id == 4);
    assert(modes.setMode("400X300").id == 2 && physical == 2 && calls == 1);
    assert(modes.setMode(2, 0).id == 2 && calls == 1);
    fail = true;
    try { modes.setMode(0, 0); assert(false); }
    catch (const std::runtime_error&) {}
    assert(modes.currentDisplayMode().id == 2 && physical == 2);
    try { modes.setMode(1, 1); assert(false); }
    catch (const std::invalid_argument&) {}
    try { modes.setMode(5, 0); assert(false); }
    catch (const std::invalid_argument&) {}
    assert(calls == 2);
}
