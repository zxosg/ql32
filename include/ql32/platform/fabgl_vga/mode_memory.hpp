#pragma once

#include <cstdint>

namespace ql32::platform::fabgl_vga {

// FabGL allocates one byte per pixel from DMA-capable internal RAM and calls
// abort() when it cannot. The previous viewport is released first, so it counts
// as available. The 40 KB reserve (descriptors, task stacks, heap fragmentation)
// is empirical and used only for the no-PSRAM build (about 25-45 KB free in
// mode 0), which must refuse 320x240 and 400x300. PSRAM builds place FabGL
// buffers differently and are not preflighted.
constexpr std::uint32_t kVgaModeMemoryReserve = 40u * 1024u;

constexpr std::uint32_t vgaModeBytes(std::uint32_t width, std::uint32_t height) noexcept {
    return width * height;
}

constexpr bool vgaModeFitsMemory(std::uint32_t width, std::uint32_t height,
                                 std::uint32_t currentBytes,
                                 std::uint32_t freeDmaBytes) noexcept {
    const std::uint32_t needed = vgaModeBytes(width, height);
    // Shrinking or staying the same size reuses memory already held.
    if (needed <= currentBytes) return true;
    return freeDmaBytes + currentBytes >= needed + kVgaModeMemoryReserve;
}

} // namespace ql32::platform::fabgl_vga
