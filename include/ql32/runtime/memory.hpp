#pragma once

#include <cstdint>
#include <optional>

namespace ql32::runtime {

struct HeapFragmentationInfo {
    std::uint64_t largestFreeBlockBytes = 0;
    std::uint64_t freeBlockCount = 0;
};

// Reports memory currently available to the platform runtime. The exact pool
// is platform-specific and documented by each adapter.
class MemoryService {
public:
    virtual ~MemoryService() = default;
    virtual std::uint64_t freeBytes() const = 0;
    // Some platforms can report allocator-level fragmentation details. Other
    // adapters return nullopt rather than estimate from system-wide memory.
    virtual std::optional<HeapFragmentationInfo> fragmentationInfo() const {
        return std::nullopt;
    }
};

} // namespace ql32::runtime
