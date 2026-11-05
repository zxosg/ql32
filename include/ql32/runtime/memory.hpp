#pragma once

#include <cstdint>
#include <cstddef>
#include <limits>
#include <optional>
#include <string_view>

namespace ql32::runtime {

struct HeapFragmentationInfo {
    std::uint64_t largestFreeBlockBytes = 0;
    std::uint64_t freeBlockCount = 0;
};

// Defaults preserve the language's current host limits. Embedded adapters may
// tighten these only when the limit has a measured platform basis.
struct PlatformLimits {
    std::size_t maxArrayElements = 1'000'000;
    std::size_t maxBufferBytes = 16 * 1024 * 1024;
    std::size_t maxTransferBytes = 16 * 1024 * 1024;
    std::size_t maxStringBytes = std::numeric_limits<std::size_t>::max();
    std::uint64_t reserveBytes = 0;
};

// Reports memory currently available to the platform runtime. The exact pool
// is platform-specific and documented by each adapter.
class MemoryService {
public:
    virtual ~MemoryService() = default;
    virtual std::uint64_t freeBytes() const = 0;
    virtual PlatformLimits platformLimits() const { return {}; }
    // Allocation checkpoint is a deterministic failure-injection seam. Real
    // platform services leave it empty; MemoryBudget still treats freeBytes
    // as an estimate, never as a reservation.
    virtual void allocationCheckpoint(std::size_t, std::string_view) const {}
    // Some platforms can report allocator-level fragmentation details. Other
    // adapters return nullopt rather than estimate from system-wide memory.
    virtual std::optional<HeapFragmentationInfo> fragmentationInfo() const {
        return std::nullopt;
    }
};

} // namespace ql32::runtime
