#pragma once

#include <cstdlib>
#include <deque>

constexpr unsigned MALLOC_CAP_DMA = 1;
namespace fake_heap {
inline std::size_t freeDma = 200000;
inline std::deque<std::size_t> largestBlocks;
inline int liveAllocations = 0;
}
inline std::size_t heap_caps_get_free_size(unsigned) { return fake_heap::freeDma; }
inline std::size_t heap_caps_get_largest_free_block(unsigned) {
    if (fake_heap::largestBlocks.empty()) return fake_heap::freeDma;
    const auto result = fake_heap::largestBlocks.front();
    fake_heap::largestBlocks.pop_front();
    return result;
}
inline void* heap_caps_malloc(std::size_t size, unsigned) {
    auto* result = std::malloc(size);
    if (result) ++fake_heap::liveAllocations;
    return result;
}
inline void heap_caps_free(void* memory) {
    if (memory) --fake_heap::liveAllocations;
    std::free(memory);
}
