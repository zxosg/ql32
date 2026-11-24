#pragma once
#include "ql32/input/service.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

namespace ql32::input {
inline constexpr std::int32_t specialKeyBase = 0x110000;
inline std::int32_t keyCode(const KeyEvent& event) noexcept {
    if (!event.pressed) return 0;
    switch (event.key) {
    case Key::Character:
        return event.character >= 32 && event.character != 127 &&
            event.character <= 0x10ffff && !(event.character >= 0xd800 && event.character <= 0xdfff)
            ? static_cast<std::int32_t>(event.character) : 0;
    case Key::Enter: return specialKeyBase;
    case Key::Escape: return specialKeyBase + 1;
    case Key::Backspace: return specialKeyBase + 2;
    case Key::Tab: return specialKeyBase + 3;
    case Key::Insert: return specialKeyBase + 4;
    case Key::Delete: return specialKeyBase + 5;
    case Key::Home: return specialKeyBase + 6;
    case Key::End: return specialKeyBase + 7;
    case Key::PageUp: return specialKeyBase + 8;
    case Key::PageDown: return specialKeyBase + 9;
    case Key::Left: return specialKeyBase + 10;
    case Key::Right: return specialKeyBase + 11;
    case Key::Up: return specialKeyBase + 12;
    case Key::Down: return specialKeyBase + 13;
    case Key::Function: return event.functionNumber >= 1 && event.functionNumber <= 12
        ? specialKeyBase + 13 + event.functionNumber : 0;
    default: return 0;
    }
}
inline std::string keyText(std::int32_t key) {
    if (key <= 0 || key >= specialKeyBase) return {};
    std::string text;
    if (key < 0x80) text += static_cast<char>(key);
    else if (key < 0x800) {
        text += static_cast<char>(0xc0 | key >> 6); text += static_cast<char>(0x80 | (key & 63));
    } else if (key < 0x10000) {
        text += static_cast<char>(0xe0 | key >> 12);
        text += static_cast<char>(0x80 | ((key >> 6) & 63)); text += static_cast<char>(0x80 | (key & 63));
    } else {
        text += static_cast<char>(0xf0 | key >> 18);
        text += static_cast<char>(0x80 | ((key >> 12) & 63));
        text += static_cast<char>(0x80 | ((key >> 6) & 63)); text += static_cast<char>(0x80 | (key & 63));
    }
    return text;
}
class SessionKeyboard {
public:
    virtual ~SessionKeyboard() = default;
    virtual std::optional<std::int32_t> poll(std::uint64_t session) = 0;
    virtual void endWait(std::uint64_t) noexcept {}
    virtual void release(std::uint64_t session) noexcept { endWait(session); }
};
// One queue per focused input endpoint, shared by all sessions. Physical input
// is published by the existing event loop, never polled by competing VM tasks.
class KeyRouter final : public SessionKeyboard {
public:
    bool focus(std::uint64_t owner) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (owner_ == owner) return false;
        owner_ = owner; head_ = count_ = 0; return true;
    }
    void publish(const KeyEvent& event) {
        const auto code = keyCode(event);
        if (!code) return;
        std::lock_guard<std::mutex> lock(mutex_);
        if (!owner_) return;
        if (count_ == queue_.size()) { ++dropped_; return; }
        queue_[(head_ + count_++) % queue_.size()] = code;
    }
    std::optional<std::int32_t> poll(std::uint64_t session) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (session != owner_ || !count_) return std::nullopt;
        const auto code = queue_[head_]; head_ = (head_ + 1) % queue_.size(); --count_; return code;
    }
    void release(std::uint64_t session) noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (owner_ == session) { owner_ = 0; head_ = count_ = 0; }
    }
    std::uint64_t dropped() const { std::lock_guard<std::mutex> lock(mutex_); return dropped_; }
private:
    mutable std::mutex mutex_;
    std::array<std::int32_t, 16> queue_{};
    std::uint64_t owner_ = 0, dropped_ = 0;
    std::size_t head_ = 0, count_ = 0;
};

// Fixed-state UTF-8 / ANSI decoder for raw application input. A lone Escape
// completes after 30ms, independently of an application's INKEY deadline.
class SerialDecoder {
public:
    void reset() noexcept { state_ = remaining_ = used_ = parameter_ = 0; scalar_ = minimum_ = 0; sawCr_ = false; }
    std::optional<KeyEvent> flush(std::uint64_t now) {
        if (state_ && now - escapeAt_ >= 30) {
            const bool escape = state_ == 1; reset();
            if (escape) return event(Key::Escape);
        }
        return std::nullopt;
    }
    std::optional<KeyEvent> feed(unsigned char byte, std::uint64_t now) {
        if (state_ == 1) {
            if (byte == '[') { state_ = 2; return {}; }
            if (byte == 'O') { state_ = 3; return {}; }
            reset(); return event(Key::Escape); // malformed prefix consumes its following byte
        }
        if (state_ == 2 || state_ == 3) {
            if (byte >= 0x40 && byte <= 0x7e) {
                const auto parameter = parameter_; const auto kind = state_; reset();
                switch (byte) {
                case 'A': return event(Key::Up); case 'B': return event(Key::Down);
                case 'C': return event(Key::Right); case 'D': return event(Key::Left);
                case 'H': return event(Key::Home); case 'F': return event(Key::End);
                case 'P': if (kind == 3) return function(1); break;
                case 'Q': if (kind == 3) return function(2); break;
                case 'R': if (kind == 3) return function(3); break;
                case 'S': if (kind == 3) return function(4); break;
                case '~':
                    switch (parameter) {
                    case 1: case 7: return event(Key::Home); case 4: case 8: return event(Key::End);
                    case 2: return event(Key::Insert); case 3: return event(Key::Delete);
                    case 5: return event(Key::PageUp); case 6: return event(Key::PageDown);
                    case 11: case 12: case 13: case 14: return function(parameter - 10);
                    case 15: return function(5); case 17: case 18: case 19: case 20: case 21: return function(parameter - 11);
                    case 23: case 24: return function(parameter - 12);
                    } break;
                } return {};
            }
            if (byte >= '0' && byte <= '9' && used_++ < 3) parameter_ = parameter_ * 10 + byte - '0';
            else reset();
            return {};
        }
        if (remaining_) {
            if ((byte & 0xc0) != 0x80) { reset(); return {}; }
            scalar_ = (scalar_ << 6) | (byte & 63);
            if (--remaining_) return {};
            if (scalar_ < minimum_ || scalar_ > 0x10ffff || (scalar_ >= 0xd800 && scalar_ <= 0xdfff)) return {};
            auto result = event(Key::Character); result->character = scalar_; return result;
        }
        if (byte == 27) { state_ = 1; escapeAt_ = now; parameter_ = used_ = 0; return {}; }
        if (byte == '\r') { sawCr_ = true; return event(Key::Enter); }
        if (byte == '\n') { if (sawCr_) { sawCr_ = false; return {}; } return event(Key::Enter); }
        sawCr_ = false;
        if (byte == 8 || byte == 127) return event(Key::Backspace);
        if (byte == 9) return event(Key::Tab);
        if (byte >= 32 && byte < 127) { auto result = event(Key::Character); result->character = byte; return result; }
        if (byte >= 0xc2 && byte <= 0xdf) { scalar_ = byte & 31; remaining_ = 1; minimum_ = 0x80; }
        else if (byte >= 0xe0 && byte <= 0xef) { scalar_ = byte & 15; remaining_ = 2; minimum_ = 0x800; }
        else if (byte >= 0xf0 && byte <= 0xf4) { scalar_ = byte & 7; remaining_ = 3; minimum_ = 0x10000; }
        return {};
    }
private:
    static std::optional<KeyEvent> event(Key key) { KeyEvent result; result.key = key; return result; }
    static std::optional<KeyEvent> function(unsigned number) { auto result = event(Key::Function); result->functionNumber = number; return result; }
    unsigned state_ = 0, remaining_ = 0, used_ = 0, parameter_ = 0;
    char32_t scalar_ = 0, minimum_ = 0;
    std::uint64_t escapeAt_ = 0;
    bool sawCr_ = false;
};
} // namespace ql32::input
