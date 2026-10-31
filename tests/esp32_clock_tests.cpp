#include "ql32/platform/esp32/clock.hpp"

#include <iostream>
#include <stdexcept>

std::int64_t ql32_test_esp_timer_microseconds = 0;
namespace {

std::int64_t fakeWallTime = 0;

std::int64_t wallTimeSource() {
    return fakeWallTime;
}

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void testMonotonicMilliseconds() {
    ql32::platform::esp32::Clock clock;
    ql32_test_esp_timer_microseconds = 1'234'999;
    require(clock.monotonicMilliseconds() == 1234,
            "ESP32 monotonic clock did not convert microseconds to milliseconds");
    ql32_test_esp_timer_microseconds = 1'235'000;
    require(clock.monotonicMilliseconds() == 1235,
            "ESP32 monotonic clock did not advance with its microsecond source");
    ql32_test_esp_timer_microseconds = -1;
    require(clock.monotonicMilliseconds() == 0,
            "ESP32 monotonic clock should clamp an invalid negative source to zero");
}

void testWallClockAvailability() {
    ql32::platform::esp32::Clock clock(wallTimeSource);
    fakeWallTime = 123;
    require(!clock.unixTimeSeconds(),
            "ESP32 wall clock should be unavailable before synchronization");
    fakeWallTime = 1'577'836'800;
    require(clock.unixTimeSeconds() == fakeWallTime,
            "ESP32 wall clock should expose timestamps at the synchronization threshold");
}

} // namespace

int main() {
    try {
        testMonotonicMilliseconds();
        testWallClockAvailability();
    } catch (const std::exception& error) {
        std::cerr << "ESP32 clock adapter test failed: " << error.what() << '\n';
        return 1;
    }
}
