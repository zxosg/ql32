#include "ql32/platform/fabgl_vga/keyboard.hpp"

#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void testNonblockingAndCharacterEvents() {
    fabgl::Keyboard source;
    ql32::platform::fabgl_vga::FabglKeyboard keyboard(source);
    require(!keyboard.poll(), "empty FabGL keyboard queue should return no event");

    source.push(fabgl::VK_a, true, 'A', true, true, false, false);
    const auto event = keyboard.poll();
    require(event && event->key == ql32::input::Key::Character &&
                event->character == U'A' && event->pressed && event->shift &&
                event->control && !event->alt,
            "printable key event lost text or modifier state");
}

void testSpecialAndReleaseEvents() {
    fabgl::Keyboard source;
    ql32::platform::fabgl_vga::FabglKeyboard keyboard(source);
    source.push(fabgl::VK_F7, false, 0, false, false, false, false);
    const auto event = keyboard.poll();
    require(event && event->key == ql32::input::Key::Function &&
                event->functionNumber == 7 && !event->pressed,
            "function-key release was not represented correctly");

    source.push(fabgl::VK_KP_ENTER, true, 0, false, false, false, false);
    const auto enter = keyboard.poll();
    require(enter && enter->key == ql32::input::Key::Enter,
            "keypad Enter was not normalized to Enter");
}

} // namespace

int main() {
    try {
        testNonblockingAndCharacterEvents();
        testSpecialAndReleaseEvents();
    } catch (const std::exception& error) {
        std::cerr << "FabGL keyboard adapter test failed: " << error.what() << '\n';
        return 1;
    }
}
