#pragma once

#include <cstdint>
#include <optional>

namespace ql32::input {

enum class Key : std::uint8_t {
    Character, Enter, Escape, Backspace, Tab, Insert, Delete, Home, End,
    PageUp, PageDown, Left, Right, Up, Down, Function, Other
};

// A non-blocking, platform-neutral keyboard event. Character values are
// Unicode code points when the platform keyboard can provide them; zero means
// the key has no text representation.
struct KeyEvent {
    Key key = Key::Other;
    char32_t character = 0;
    std::uint8_t functionNumber = 0;
    bool pressed = true;
    bool shift = false;
    bool control = false;
    bool alt = false;
    bool capsLock = false;
};

class KeyboardService {
public:
    virtual ~KeyboardService() = default;
    virtual std::optional<KeyEvent> poll() = 0;
};

} // namespace ql32::input
