#pragma once

#include <cstdint>

namespace ql32::platform::fabgl_vga {

// FabGL's VGAController uses internal DMA RAM even on PSRAM builds. A partial
// double-buffer allocation can corrupt its row-pointer array before returning.
// The previous viewport is released first; the empirical 40 KB reserve covers
// descriptors, task stacks and fragmentation, not framebuffer pixels.
constexpr std::uint32_t kVgaModeMemoryReserve = 40u * 1024u;

constexpr std::uint32_t vgaModeBytes(std::uint32_t width, std::uint32_t height) noexcept {
    return width * height;
}

constexpr bool vgaModeFitsMemory(std::uint32_t width, std::uint32_t height,
                                 std::uint32_t currentBytes,
                                 std::uint32_t freeDmaBytes,
                                 bool doubleBuffered = false) noexcept {
    const std::uint64_t needed = static_cast<std::uint64_t>(width) * height *
                                (doubleBuffered ? 2u : 1u);
    // Shrinking or staying the same size reuses memory already held.
    if (needed <= currentBytes) return true;
    return static_cast<std::uint64_t>(freeDmaBytes) + currentBytes >=
           needed + kVgaModeMemoryReserve;
}

constexpr std::uint32_t vgaAllocatableRows(std::uint32_t width,
                                          std::uint32_t remainingRows,
                                          std::uint32_t largestDmaBlock) noexcept {
    if (width == 0 || largestDmaBlock <= kVgaModeMemoryReserve) return 0;
    const auto rows = (largestDmaBlock - kVgaModeMemoryReserve) / width;
    return rows < remainingRows ? rows : remainingRows;
}

} // namespace ql32::platform::fabgl_vga
