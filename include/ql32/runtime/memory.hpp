#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <string_view>

namespace ql32::runtime {

struct HeapFragmentationInfo {
    std::uint64_t largestFreeBlockBytes = 0;
    std::uint64_t freeBlockCount = 0;
    std::uint64_t allocatedBlockCount = 0;
};

// Minimum memory a platform wants available before it admits new work. Zero
// fields disable the corresponding check (the default, e.g. on host builds).
struct AdmissionPolicy {
    std::uint64_t workerTotalBytes = 0;       // new task: free bytes needed
    std::uint64_t workerContiguousBytes = 0;  // new task: largest free block needed
    std::uint64_t musicStartTotalBytes = 0;   // first audio start (task stacks + driver)
    std::uint64_t musicStartContiguousBytes = 0;
    std::uint64_t musicPlayTotalBytes = 0;    // loading a song once audio is running
    std::uint64_t musicPlayContiguousBytes = 0;
    std::uint64_t musicDrumsTotalBytes = 0;   // free bytes needed to load YM digi-drums at full budget
    std::uint64_t musicDrumsContiguousBytes = 0;
    bool displayModeGuard = false;            // refuse MODE growth that cannot fit
    bool freezeWindowsAfterRun = false;       // finished RUN leaves a reclaimable last frame, not live canvases
    std::string programSpillPath;             // non-empty: RUN parks the editable program here to free RAM
    bool canvasGuard = false;                 // refuse a new canvas that cannot fit
    std::uint64_t canvasHeadroomBytes = 0;    // free bytes that must remain after a canvas
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

    // Returns a message when `totalBytes` free or a `contiguousBytes` block is not
    // available. Zero skips a check; the block check needs fragmentationInfo().
    std::optional<std::string> admissionFailure(std::string_view what,
                                                std::uint64_t totalBytes,
                                                std::uint64_t contiguousBytes = 0) const {
        auto failure = checkAdmission(what, totalBytes, contiguousBytes);
        // Frozen last-frame canvases are dead weight once memory is short.
        if (failure && reclaim()) failure = checkAdmission(what, totalBytes, contiguousBytes);
        return failure;
    }

    // Optional hook that frees caches the platform can rebuild (for example the
    // retained image of a finished task). Returns nothing; callers re-measure.
    void setReclaimer(std::function<void()> reclaimer) { reclaimer_ = std::move(reclaimer); }
    bool reclaim() const {
        if (!reclaimer_) return false;
        reclaimer_();
        return true;
    }

    // Canvases are 4 bits per pixel and old and new buffers coexist during a resize,
    // so growing the display needs (newArea - oldArea) / 2 bytes more.
    std::optional<std::string> displayModeFailure(std::uint64_t oldWidth, std::uint64_t oldHeight,
                                                  std::uint64_t newWidth, std::uint64_t newHeight,
                                                  std::string_view modeName) const {
        if (!admissionPolicy_.displayModeGuard) return std::nullopt;
        const std::uint64_t oldArea = oldWidth * oldHeight;
        const std::uint64_t newArea = newWidth * newHeight;
        if (newArea <= oldArea) return std::nullopt;
        return admissionFailure("display mode " + std::string(modeName), (newArea - oldArea) / 2);
    }
    // A new canvas needs one contiguous block of `canvasBytes` plus headroom.
    std::optional<std::string> canvasFailure(std::uint64_t canvasBytes) const {
        if (!admissionPolicy_.canvasGuard) return std::nullopt;
        return admissionFailure("a " + std::to_string(canvasBytes) + " byte canvas",
                                canvasBytes + admissionPolicy_.canvasHeadroomBytes,
                                canvasBytes);
    }
    bool canAdmit(std::uint64_t totalBytes, std::uint64_t contiguousBytes = 0) const {
        return !admissionFailure("request", totalBytes, contiguousBytes);
    }
    // Lowest free heap seen since boot, when the platform tracks it.
    virtual std::optional<std::uint64_t> minimumFreeBytes() const { return std::nullopt; }

    const AdmissionPolicy& admissionPolicy() const noexcept { return admissionPolicy_; }
    void setAdmissionPolicy(const AdmissionPolicy& policy) noexcept { admissionPolicy_ = policy; }

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
    std::optional<std::string> checkAdmission(std::string_view what, std::uint64_t totalBytes,
                                              std::uint64_t contiguousBytes) const {
        if (totalBytes == 0 && contiguousBytes == 0) return std::nullopt;
        const std::uint64_t free = freeBytes();
        const auto info = fragmentationInfo();
        const bool freeShort = totalBytes != 0 && free < totalBytes;
        const bool blockShort = contiguousBytes != 0 && info &&
                                info->largestFreeBlockBytes < contiguousBytes;
        if (!freeShort && !blockShort) return std::nullopt;
        std::string message = "not enough memory for " + std::string(what) + ": need " +
            std::to_string(totalBytes) + " bytes free";
        if (contiguousBytes != 0)
            message += " (" + std::to_string(contiguousBytes) + " contiguous)";
        message += ", have " + std::to_string(free) + " free";
        if (info) message += ", " + std::to_string(info->largestFreeBlockBytes) + " largest";
        return message;
    }

    std::function<void()> reclaimer_;
    std::unique_ptr<std::byte[]> emergencyReserve_;
    std::size_t emergencyReserveBytes_ = 0;
    AdmissionPolicy admissionPolicy_;
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
