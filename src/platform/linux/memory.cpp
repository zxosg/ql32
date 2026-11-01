#include "ql32/platform/linux/memory.hpp"

#include <fstream>
#include <string>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_host.h>
#endif

namespace ql32::platform::linux_host {

std::uint64_t MemoryService::freeBytes() const {
    std::ifstream memoryInfo("/proc/meminfo");
    std::string label;
    std::uint64_t value = 0;
    std::string unit;
    while (memoryInfo >> label >> value >> unit) {
        if (label == "MemAvailable:") return value * 1024;
    }

    const long pageSize = ::sysconf(_SC_PAGESIZE);
    if (pageSize <= 0) return 0;

#if defined(__APPLE__)
    vm_statistics64_data_t vmStats{};
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    const kern_return_t result = host_statistics64(
        mach_host_self(), HOST_VM_INFO64,
        reinterpret_cast<host_info64_t>(&vmStats), &count);
    if (result != KERN_SUCCESS) return 0;

    // macOS already includes speculative pages in free_count. Inactive pages
    // can be reclaimed, so count them with free pages as available memory.
    const std::uint64_t pages =
        static_cast<std::uint64_t>(vmStats.free_count) + vmStats.inactive_count;
#else
    const long availablePages = ::sysconf(_SC_AVPHYS_PAGES);
    if (availablePages <= 0) return 0;
    const std::uint64_t pages = static_cast<std::uint64_t>(availablePages);
#endif

    return pages * static_cast<std::uint64_t>(pageSize);
}

} // namespace ql32::platform::linux_host
