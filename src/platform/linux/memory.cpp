#include "ql32/platform/linux/memory.hpp"

#include <fstream>
#include <string>
#include <unistd.h>

namespace ql32::platform::linux_host {

std::uint64_t MemoryService::freeBytes() const {
    std::ifstream memoryInfo("/proc/meminfo");
    std::string label;
    std::uint64_t value = 0;
    std::string unit;
    while (memoryInfo >> label >> value >> unit) {
        if (label == "MemAvailable:") return value * 1024;
    }

    const long pages = ::sysconf(_SC_AVPHYS_PAGES);
    const long pageSize = ::sysconf(_SC_PAGESIZE);
    if (pages <= 0 || pageSize <= 0) return 0;
    return static_cast<std::uint64_t>(pages) * static_cast<std::uint64_t>(pageSize);
}

} // namespace ql32::platform::linux_host
