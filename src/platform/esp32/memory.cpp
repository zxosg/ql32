#include "ql32/platform/esp32/diagnostics.hpp"
#include <esp_rom_sys.h>
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

std::optional<std::uint64_t> MemoryService::minimumFreeBytes() const {
    return static_cast<std::uint64_t>(heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT));
}

namespace {
void onFailedAlloc(std::size_t size, std::uint32_t caps, const char* function) {
    auto& d = diagnostics();
    d.failedAllocations.fetch_add(1);
    d.lastFailedAllocBytes.store(static_cast<std::uint32_t>(size));
    esp_rom_printf("ALLOCFAIL|%u|0x%x|%s|free=%u|largest=%u\n",
                   static_cast<unsigned>(size), static_cast<unsigned>(caps),
                   function ? function : "?",
                   static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_8BIT)),
                   static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
}
} // namespace

void installFailedAllocLogger() { heap_caps_register_failed_alloc_callback(onFailedAlloc); }

} // namespace ql32::platform::esp32
