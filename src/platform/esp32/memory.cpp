#include "ql32/platform/esp32/memory.hpp"

#include <esp_heap_caps.h>

namespace ql32::platform::esp32 {

std::uint64_t MemoryService::freeBytes() const {
    return static_cast<std::uint64_t>(heap_caps_get_free_size(MALLOC_CAP_8BIT));
}

std::optional<runtime::HeapFragmentationInfo> MemoryService::fragmentationInfo() const {
    multi_heap_info_t info{};
    heap_caps_get_info(&info, MALLOC_CAP_8BIT);
    return runtime::HeapFragmentationInfo{
        static_cast<std::uint64_t>(info.largest_free_block),
        static_cast<std::uint64_t>(info.free_blocks),
        static_cast<std::uint64_t>(info.allocated_blocks)
    };
}

} // namespace ql32::platform::esp32
