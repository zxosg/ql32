#pragma once

#include <cstddef>

namespace ql32::platform::esp32 {

// Usage of the small-object region behind operator new (all zero when it is disabled).
struct SmallAllocStats {
    std::size_t regionBytes;
    std::size_t usedBytes;
    std::size_t peakBytes;
    std::size_t fallbacks; // arena requests that did not fit and went to the general heap
    std::size_t leftover;  // bytes still live in the arena when the last scope closed
    std::size_t releases;  // times the whole arena went back to the heap
};

#if defined(QL32_SMALL_ALLOC_BYTES)
SmallAllocStats smallAllocStats();
void smallAllocDump(void (*emit)(const char*));
void smallAllocHooks(void (**begin)(), void (**end)());
#else
inline SmallAllocStats smallAllocStats() { return {}; }
inline void smallAllocDump(void (*)(const char*)) {}
inline void smallAllocHooks(void (**begin)(), void (**end)()) { *begin = nullptr; *end = nullptr; }
#endif

} // namespace ql32::platform::esp32
