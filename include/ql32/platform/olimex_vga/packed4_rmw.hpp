#pragma once

#include <atomic>
#include <cstdint>

#if defined(QL32_OLIMEX_SINGLE_FRAMEBUFFER) && !defined(QL32_PACKED4_SHARED_WRITES)
#define QL32_PACKED4_SHARED_WRITES 1
#endif

namespace ql32::platform::olimex_vga {

// Packed-4 pixels share a byte, so a nibble store is a read-modify-write.
// In the single-framebuffer mode several graphics producers (sessions, cores)
// may write neighbouring pixels while the refill worker reads. ESP32 cannot
// run S32C1I on PSRAM, so producers serialise only these one-byte updates
// with a tiny spin flag held for a few instructions. Whole-byte stores never
// take it, and neither the refill worker nor any ISR ever does.
class Packed4NibbleLock {
public:
    static void lock() noexcept {
        while (flag().test_and_set(std::memory_order_acquire)) {}
    }
    static void unlock() noexcept { flag().clear(std::memory_order_release); }

private:
    static std::atomic_flag& flag() noexcept {
        static std::atomic_flag instance = ATOMIC_FLAG_INIT;
        return instance;
    }
};

// Replace one nibble of *byte without losing a concurrent update of the
// other nibble. `high` selects the even-x (high) nibble.
inline void storePackedNibbleShared(std::uint8_t* byte, bool high,
                                    std::uint8_t value) noexcept {
    Packed4NibbleLock::lock();
    const std::uint8_t keep = high ? 0x0Fu : 0xF0u;
    const std::uint8_t bits = high ? static_cast<std::uint8_t>((value & 0x0Fu) << 4)
                                   : static_cast<std::uint8_t>(value & 0x0Fu);
    *static_cast<volatile std::uint8_t*>(byte) =
        static_cast<std::uint8_t>((*static_cast<volatile std::uint8_t*>(byte) & keep) | bits);
    Packed4NibbleLock::unlock();
}

// Compile with QL32_PACKED4_SHARED_WRITES for the single-framebuffer mode.
inline void storePackedNibble(std::uint8_t* byte, bool high,
                              std::uint8_t value) noexcept {
#if defined(QL32_PACKED4_SHARED_WRITES)
    storePackedNibbleShared(byte, high, value);
#else
    const std::uint8_t keep = high ? 0x0Fu : 0xF0u;
    const std::uint8_t bits = high ? static_cast<std::uint8_t>((value & 0x0Fu) << 4)
                                   : static_cast<std::uint8_t>(value & 0x0Fu);
    *byte = static_cast<std::uint8_t>((*byte & keep) | bits);
#endif
}

} // namespace ql32::platform::olimex_vga
