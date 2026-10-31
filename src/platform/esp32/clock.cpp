#include "ql32/platform/esp32/clock.hpp"

#include <esp_timer.h>

#include <ctime>

namespace ql32::platform::esp32 {
namespace {

// ESP32 starts with an unset wall clock near the Unix epoch. Treat dates
// before 2020 as unsynchronized; monotonic time is available from boot.
constexpr std::int64_t kEarliestSynchronizedTime = 1'577'836'800;

} // namespace

std::optional<std::int64_t> Clock::unixTimeSeconds() const {
    const std::int64_t current = wallTimeSource_ ? wallTimeSource_() :
        static_cast<std::int64_t>(std::time(nullptr));
    if (current < kEarliestSynchronizedTime) return std::nullopt;
    return current;
}

std::uint64_t Clock::monotonicMilliseconds() const {
    const std::int64_t microseconds = esp_timer_get_time();
    if (microseconds <= 0) return 0;
    return static_cast<std::uint64_t>(microseconds / 1000);
}

} // namespace ql32::platform::esp32
