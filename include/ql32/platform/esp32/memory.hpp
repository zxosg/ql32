#pragma once

#include "ql32/runtime/memory.hpp"

#include <cstddef>
#include <optional>

namespace ql32::platform::esp32 {

inline constexpr std::size_t defaultEmergencyReserveBytes = 8 * 1024;

// Reports ESP32 heap memory available to ordinary byte-addressable allocations.
class MemoryService final : public runtime::MemoryService {
public:
    std::uint64_t freeBytes() const override;
    std::optional<runtime::HeapFragmentationInfo> fragmentationInfo() const override;
};

} // namespace ql32::platform::esp32
