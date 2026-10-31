#include "ql32/platform/linux/system_clock.hpp"

#include <chrono>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void testLinuxClock() {
    ql32::platform::linux_host::SystemClock clock;
    const auto wallBefore = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const auto wall = clock.unixTimeSeconds();
    const auto wallAfter = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    require(wall && *wall >= wallBefore && *wall <= wallAfter,
            "wall clock result fell outside the surrounding system-clock samples");

    const auto first = clock.monotonicMilliseconds();
    const auto second = clock.monotonicMilliseconds();
    require(second >= first, "monotonic clock moved backwards");
}

} // namespace

int main() {
    try {
        testLinuxClock();
    } catch (const std::exception& error) {
        std::cerr << "clock service test failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
