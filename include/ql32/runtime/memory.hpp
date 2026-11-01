#pragma once

#include <cstdint>

namespace ql32::runtime {

// Reports memory currently available to the platform runtime. The exact pool
// is platform-specific and documented by each adapter.
class MemoryService {
public:
    virtual ~MemoryService() = default;
    virtual std::uint64_t freeBytes() const = 0;
};

} // namespace ql32::runtime
