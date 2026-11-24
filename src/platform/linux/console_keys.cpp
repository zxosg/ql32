#include "ql32/platform/linux/console_keys.hpp"
#include "ql32/runtime/error.hpp"
#include <chrono>
#include <poll.h>
#include <unistd.h>
namespace ql32::platform::linux_host {
namespace {
std::uint64_t milliseconds() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
}
ConsoleKeys::~ConsoleKeys() { release(owner_); }
void ConsoleKeys::release(std::uint64_t session) noexcept {
    if (owner_ != session) return;
    if (raw_) (void)::tcsetattr(STDIN_FILENO, TCSANOW, &saved_);
    raw_ = false; owner_ = 0; router_.focus(0); decoder_.reset();
}
std::optional<std::int32_t> ConsoleKeys::poll(std::uint64_t session) {
    if (!raw_) {
        if (!::isatty(STDIN_FILENO) || ::tcgetattr(STDIN_FILENO, &saved_))
            throw runtime::Error(runtime::ErrorCode::ServiceUnavailable, "console key input requires a terminal");
        auto settings = saved_;
        settings.c_lflag &= ~(ICANON | ECHO);
        settings.c_cc[VMIN] = 0; settings.c_cc[VTIME] = 0;
        if (::tcsetattr(STDIN_FILENO, TCSANOW, &settings))
            throw runtime::Error(runtime::ErrorCode::ServiceUnavailable, "cannot enable terminal key input");
        raw_ = true; owner_ = session; router_.focus(session);
    }
    if (owner_ != session) return {};
    if (auto key = router_.poll(session)) return key;
    if (auto event = decoder_.flush(milliseconds())) router_.publish(*event);
    for (unsigned count = 0; count < 32; ++count) {
        pollfd descriptor{STDIN_FILENO, POLLIN, 0};
        const auto ready = ::poll(&descriptor, 1, 0);
        if (ready < 0) throw runtime::Error(runtime::ErrorCode::IoError, "terminal input poll failed");
        if (!ready || !(descriptor.revents & POLLIN)) break;
        unsigned char byte;
        const auto read = ::read(STDIN_FILENO, &byte, 1);
        if (read != 1) break;
        if (auto event = decoder_.feed(byte, milliseconds())) router_.publish(*event);
    }
    return router_.poll(session);
}
}
