#pragma once

#include <cstdint>

namespace ql32::platform::olimex_vga {

// Portable counters for presenter calls. Times are microseconds supplied by
// the caller, so the accumulator has no board dependency.
struct PresentStats {
    std::uint32_t calls = 0;
    std::uint32_t fullWindowCalls = 0;
    std::uint32_t partialCalls = 0;
    std::uint32_t rejectedCalls = 0;
    std::uint64_t rows = 0;
    std::uint64_t pixels = 0;
    std::uint64_t totalUs = 0;
    std::uint64_t beginUs = 0;
    std::uint64_t rowsUs = 0;
    std::uint64_t publishUs = 0;
    std::uint64_t providerUs = 0;
    std::uint64_t writeUs = 0;
    std::uint32_t maxUs = 0;
    std::uint32_t maxRows = 0;
    std::uint32_t paletteChanges = 0;
    std::uint32_t throttleWaits = 0;
    std::uint64_t throttleUs = 0;

    void recordAccepted(std::uint32_t width, std::uint32_t height,
                        std::uint32_t regionWidth, std::uint32_t regionHeight,
                        std::uint32_t begin, std::uint32_t rowWork,
                        std::uint32_t publish) noexcept {
        ++calls;
        if (regionWidth == width && regionHeight == height) ++fullWindowCalls;
        else ++partialCalls;
        rows += regionHeight;
        pixels += static_cast<std::uint64_t>(regionWidth) * regionHeight;
        const std::uint32_t total = begin + rowWork + publish;
        totalUs += total;
        beginUs += begin;
        rowsUs += rowWork;
        publishUs += publish;
        if (total > maxUs) maxUs = total;
        if (regionHeight > maxRows) maxRows = regionHeight;
    }

    void recordRejected() noexcept {
        ++calls;
        ++rejectedCalls;
    }
};

} // namespace ql32::platform::olimex_vga
