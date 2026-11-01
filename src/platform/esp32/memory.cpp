#include "ql32/platform/esp32/memory.hpp"

#include <esp_heap_caps.h>

namespace ql32::platform::esp32 {

std::uint64_t MemoryService::freeBytes() const {
    return static_cast<std::uint64_t>(heap_caps_get_free_size(MALLOC_CAP_8BIT));
}

} // namespace ql32::platform::esp32
