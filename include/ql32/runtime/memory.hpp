#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <optional>

namespace ql32::runtime {

struct HeapFragmentationInfo {
    std::uint64_t largestFreeBlockBytes = 0;
    std::uint64_t freeBlockCount = 0;
    std::uint64_t allocatedBlockCount = 0;
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

    // A held heap block keeps memory available for cancellation, error
    // reporting, and storage recovery after user allocations fail. Platforms
    // choose the reserve size; zero disables it (for example, on host builds).
    bool initializeEmergencyReserve(std::size_t bytes) noexcept {
        if (bytes == 0) return true;
        if (emergencyReserve_) return emergencyReserveBytes_ == bytes;
        std::unique_ptr<std::byte[]> reserve(new (std::nothrow) std::byte[bytes]);
        if (!reserve) return false;
        emergencyReserve_ = std::move(reserve);
        emergencyReserveBytes_ = bytes;
        return true;
    }

    bool releaseEmergencyReserve() noexcept {
        if (!emergencyReserve_) return false;
        emergencyReserve_.reset();
        return true;
    }

    bool restoreEmergencyReserve() noexcept {
        if (emergencyReserveBytes_ == 0 || emergencyReserve_) return true;
        std::unique_ptr<std::byte[]> reserve(
            new (std::nothrow) std::byte[emergencyReserveBytes_]);
        if (!reserve) return false;
        emergencyReserve_ = std::move(reserve);
        return true;
    }

    bool emergencyReserveHeld() const noexcept {
        return emergencyReserve_ != nullptr || emergencyReserveBytes_ == 0;
    }

    std::size_t emergencyReserveBytes() const noexcept {
        return emergencyReserveBytes_;
    }

private:
    std::unique_ptr<std::byte[]> emergencyReserve_;
    std::size_t emergencyReserveBytes_ = 0;
};

class EmergencyReserveLease {
public:
    explicit EmergencyReserveLease(MemoryService* memory) noexcept
        : memory_(memory), released_(memory_ && memory_->releaseEmergencyReserve()) {}
    ~EmergencyReserveLease() {
        if (released_) (void)memory_->restoreEmergencyReserve();
    }
    EmergencyReserveLease(const EmergencyReserveLease&) = delete;
    EmergencyReserveLease& operator=(const EmergencyReserveLease&) = delete;

private:
    MemoryService* memory_ = nullptr;
    bool released_ = false;
};

} // namespace ql32::runtime
