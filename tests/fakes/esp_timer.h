#pragma once

#include <cstdint>

extern std::int64_t ql32_test_esp_timer_microseconds;

inline std::int64_t esp_timer_get_time() {
    return ql32_test_esp_timer_microseconds;
}
