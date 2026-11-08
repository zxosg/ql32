#pragma once

#include <optional>
#include <string>

namespace ql32::platform::esp32 {

enum class SerialKey : unsigned char {
    Character,
    Enter,
    Backspace,
    Delete,
    Up,
    Left,
    Right,
    Home,
    End
};

struct SerialKeyEvent {
    SerialKey key;
    char character = 0;
};

// Decodes printable input and common VT100/ANSI cursor-key sequences used by
// serial terminals, including both CSI and SS3 variants of Home and End.
class SerialKeyDecoder {
public:
    std::optional<SerialKeyEvent> feed(char byte) {
        switch (state_) {
        case State::Text:
            if (byte == '\x1b') {
                state_ = State::Escape;
                return std::nullopt;
            }
            if (byte == '\r' || byte == '\n') return event(SerialKey::Enter);
            if (byte == '\b' || byte == 127) return event(SerialKey::Backspace);
            if (byte >= 32 && byte <= 126)
                return SerialKeyEvent{SerialKey::Character, byte};
            return std::nullopt;

        case State::Escape:
            if (byte == '[') {
                state_ = State::Csi;
                parameters_.clear();
            } else if (byte == 'O') {
                state_ = State::Ss3;
            } else {
                state_ = State::Text;
            }
            return std::nullopt;

        case State::Csi:
            if (byte >= 0x40 && byte <= 0x7e) {
                state_ = State::Text;
                switch (byte) {
                case 'A': return event(SerialKey::Up);
                case 'D': return event(SerialKey::Left);
                case 'C': return event(SerialKey::Right);
                case 'H': return event(SerialKey::Home);
                case 'F': return event(SerialKey::End);
                case '~':
                    if (parameters_ == "1" || parameters_ == "7")
                        return event(SerialKey::Home);
                    if (parameters_ == "3") return event(SerialKey::Delete);
                    if (parameters_ == "4" || parameters_ == "8")
                        return event(SerialKey::End);
                    return std::nullopt;
                default: return std::nullopt;
                }
            }
            if (parameters_.size() < 8 &&
                ((byte >= '0' && byte <= '9') || byte == ';'))
                parameters_.push_back(byte);
            else
                state_ = State::Text;
            return std::nullopt;

        case State::Ss3:
            state_ = State::Text;
            switch (byte) {
            case 'A': return event(SerialKey::Up);
            case 'D': return event(SerialKey::Left);
            case 'C': return event(SerialKey::Right);
            case 'H': return event(SerialKey::Home);
            case 'F': return event(SerialKey::End);
            default: return std::nullopt;
            }
        }
        return std::nullopt;
    }

    void reset() noexcept {
        state_ = State::Text;
        parameters_.clear();
    }

private:
    enum class State : unsigned char { Text, Escape, Csi, Ss3 };

    static SerialKeyEvent event(SerialKey key) noexcept { return {key, 0}; }

    State state_ = State::Text;
    std::string parameters_;
};

} // namespace ql32::platform::esp32
