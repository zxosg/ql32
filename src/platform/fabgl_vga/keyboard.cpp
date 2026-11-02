#include "ql32/platform/fabgl_vga/keyboard.hpp"

namespace ql32::platform::fabgl_vga {
namespace {

input::Key mapKey(fabgl::VirtualKey key, std::uint8_t& functionNumber) {
    using input::Key;
    switch (key) {
    case fabgl::VK_RETURN:
    case fabgl::VK_KP_ENTER: return Key::Enter;
    case fabgl::VK_ESCAPE: return Key::Escape;
    case fabgl::VK_BACKSPACE: return Key::Backspace;
    case fabgl::VK_TAB: return Key::Tab;
    case fabgl::VK_INSERT:
    case fabgl::VK_KP_INSERT: return Key::Insert;
    case fabgl::VK_DELETE:
    case fabgl::VK_KP_DELETE: return Key::Delete;
    case fabgl::VK_PAUSE: return Key::Pause;
    case fabgl::VK_BREAK: return Key::Break;
    case fabgl::VK_HOME:
    case fabgl::VK_KP_HOME: return Key::Home;
    case fabgl::VK_END:
    case fabgl::VK_KP_END: return Key::End;
    case fabgl::VK_PAGEUP:
    case fabgl::VK_KP_PAGEUP: return Key::PageUp;
    case fabgl::VK_PAGEDOWN:
    case fabgl::VK_KP_PAGEDOWN: return Key::PageDown;
    case fabgl::VK_LEFT:
    case fabgl::VK_KP_LEFT: return Key::Left;
    case fabgl::VK_RIGHT:
    case fabgl::VK_KP_RIGHT: return Key::Right;
    case fabgl::VK_UP:
    case fabgl::VK_KP_UP: return Key::Up;
    case fabgl::VK_DOWN:
    case fabgl::VK_KP_DOWN: return Key::Down;
    case fabgl::VK_F1: functionNumber = 1; return Key::Function;
    case fabgl::VK_F2: functionNumber = 2; return Key::Function;
    case fabgl::VK_F3: functionNumber = 3; return Key::Function;
    case fabgl::VK_F4: functionNumber = 4; return Key::Function;
    case fabgl::VK_F5: functionNumber = 5; return Key::Function;
    case fabgl::VK_F6: functionNumber = 6; return Key::Function;
    case fabgl::VK_F7: functionNumber = 7; return Key::Function;
    case fabgl::VK_F8: functionNumber = 8; return Key::Function;
    case fabgl::VK_F9: functionNumber = 9; return Key::Function;
    case fabgl::VK_F10: functionNumber = 10; return Key::Function;
    case fabgl::VK_F11: functionNumber = 11; return Key::Function;
    case fabgl::VK_F12: functionNumber = 12; return Key::Function;
    default: return Key::Other;
    }
}

} // namespace

std::optional<input::KeyEvent> FabglKeyboard::poll() {
    fabgl::VirtualKeyItem item{};
    if (!keyboard_.getNextVirtualKey(&item, 0)) return std::nullopt;

    input::KeyEvent event;
    event.pressed = item.down != 0;
    event.shift = item.SHIFT;
    event.control = item.CTRL;
    event.alt = item.LALT || item.RALT;
    event.capsLock = item.CAPSLOCK;
    event.key = mapKey(item.vk, event.functionNumber);
    if (item.vk == fabgl::VK_SPACE && item.CTRL)
        event.key = input::Key::Break;
    if (event.key == input::Key::Other && item.ASCII > 0 && item.ASCII < 0x80) {
        event.key = input::Key::Character;
        event.character = item.ASCII;
    }
    return event;
}

} // namespace ql32::platform::fabgl_vga
