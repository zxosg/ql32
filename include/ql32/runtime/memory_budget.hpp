#pragma once

#include "ql32/runtime/error.hpp"
#include "ql32/runtime/memory.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace ql32::runtime {

class MemoryBudget {
public:
    explicit MemoryBudget(const MemoryService* service = nullptr)
        : service_(service), limits_(service ? service->platformLimits() : PlatformLimits{}) {}

    const PlatformLimits& limits() const noexcept { return limits_; }

    void preflight(std::size_t bytes, std::string_view operation) const {
        if (service_) service_->allocationCheckpoint(bytes, operation);
        if (!service_) return;
        const std::uint64_t available = service_->freeBytes();
        if (bytes > std::numeric_limits<std::uint64_t>::max() - limits_.reserveBytes ||
            available < static_cast<std::uint64_t>(bytes) + limits_.reserveBytes) {
            throw Error(ErrorCode::OutOfMemory,
                        "insufficient reported memory for " + std::string(operation));
        }
    }

    void enforce(std::size_t requested, std::size_t maximum,
                 std::string_view resource) const {
        if (requested > maximum)
            throw Error(ErrorCode::OutOfMemory,
                        std::string(resource) + " allocation limit exceeded");
    }

private:
    const MemoryService* service_;
    PlatformLimits limits_;
};

} // namespace ql32::runtime
