#pragma once

#include <atomic>
#include <cstdint>

namespace ql32::platform::esp32 {

// Heap/stack diagnostics shared between services and the serial monitor.
// Task handles are published as opaque pointers so portable code never sees RTOS types.
struct Diagnostics {
    std::atomic<void*> musicTask{nullptr};
    std::atomic<void*> musicIoTask{nullptr};
    std::atomic<std::uint32_t> failedAllocations{0};
    std::atomic<std::uint32_t> lastFailedAllocBytes{0};
};

inline Diagnostics& diagnostics() {
    static Diagnostics instance;
    return instance;
}

// Logs failed heap allocations (ROM printf, no heap use) and counts them.
void installFailedAllocLogger();

} // namespace ql32::platform::esp32
