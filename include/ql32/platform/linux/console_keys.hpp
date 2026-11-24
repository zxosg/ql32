#pragma once
#include "ql32/input/session.hpp"
#include <termios.h>
namespace ql32::platform::linux_host {
class ConsoleKeys final : public input::SessionKeyboard {
public:
    ~ConsoleKeys() override;
    std::optional<std::int32_t> poll(std::uint64_t session) override;
    void release(std::uint64_t session) noexcept override;
private:
    input::KeyRouter router_;
    input::SerialDecoder decoder_;
    std::uint64_t owner_ = 0;
    termios saved_{};
    bool raw_ = false;
};
}
