#pragma once

#include <cstdint>

namespace ql32::platform::olimex_vga {

// Describes refill work that can still reach the ring before scanout needs it.
struct RefillWindow {
    std::uint32_t firstSequence = 0;
    std::uint32_t completedSequence = 0;
    std::uint32_t skippedRows = 0;

    constexpr std::uint32_t rowCount() const noexcept {
        return firstSequence <= completedSequence
            ? completedSequence - firstSequence + 1 : 0;
    }
};

// A consumed row is copied to the ring slot one full ring ahead. Keep only
// rows whose destination sequence is beyond the next refill deadline
// (completed + group). Older work has already missed its slot and must not be
// replayed over scanout or delay useful rows.
constexpr RefillWindow makeRefillWindow(std::uint32_t nextSequence,
                                       std::uint32_t completedSequence,
                                       std::uint32_t ringRows,
                                       std::uint32_t refillGroupRows) noexcept {
    const auto leadRows = ringRows > refillGroupRows
        ? ringRows - refillGroupRows : 0;
    const auto earliestUseful = completedSequence >= leadRows
        ? completedSequence - leadRows + 1 : 0;
    const auto first = nextSequence > earliestUseful
        ? nextSequence : earliestUseful;
    return {first, completedSequence,
            first > nextSequence ? first - nextSequence : 0};
}

} // namespace ql32::platform::olimex_vga
