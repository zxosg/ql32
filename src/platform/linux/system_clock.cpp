#include "ql32/platform/linux/system_clock.hpp"

#include <chrono>

namespace ql32::platform::linux_host {

std::optional<std::int64_t> SystemClock::unixTimeSeconds() const {
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

std::uint64_t SystemClock::monotonicMilliseconds() const {
    using namespace std::chrono;
    return static_cast<std::uint64_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

} // namespace ql32::platform::linux_host
