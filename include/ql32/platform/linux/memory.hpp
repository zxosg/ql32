#pragma once

#include "ql32/runtime/memory.hpp"

namespace ql32::platform::linux_host {

// Reports host memory available for new allocations, as provided by Linux.
class MemoryService final : public runtime::MemoryService {
public:
    std::uint64_t freeBytes() const override;
};

} // namespace ql32::platform::linux_host
