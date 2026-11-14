#pragma once

#include <array>
#include <atomic>
#include <cstdint>

namespace ql32::platform::olimex_vga {

class Palette {
public:
    static constexpr std::size_t size = 16;
    using Entries = std::array<std::uint8_t, size>;

    enum class Direction : std::uint8_t { Left, Right };

    Palette() noexcept {
        for (std::size_t i = 0; i < size; ++i) {
            const auto color = static_cast<std::uint8_t>(i & 7u);
            // Match QL/32's 16-color order: blue, red, green bits are
            // represented by index bits 0, 1, and 2 respectively.
            const auto red = (color & 2u) ? 3u : 0u;
            const auto green = (color & 4u) ? 3u : 0u;
            const auto blue = (color & 1u) ? 3u : 0u;
            entries_[i] = static_cast<std::uint8_t>((red << 4) | (green << 2) | blue);
        }
        saved_ = entries_;
    }

    const Entries& entries() const noexcept { return entries_; }
    std::uint8_t operator[](std::size_t index) const noexcept {
        return entries_[index % size];
    }

    bool set(std::size_t index, std::uint8_t rgb222) noexcept {
        if (index >= size) return false;
        entries_[index] = rgb222 & 0x3Fu;
        return true;
    }

    void load(const Entries& values) noexcept {
        for (std::size_t i = 0; i < size; ++i) entries_[i] = values[i] & 0x3Fu;
    }

    void rotate(std::int32_t amount) noexcept {
        auto offset = amount % static_cast<std::int32_t>(size);
        if (offset < 0) offset += static_cast<std::int32_t>(size);
        if (offset == 0) return;
        const auto old = entries_;
        for (std::size_t i = 0; i < size; ++i)
            entries_[i] = old[(i + static_cast<std::size_t>(offset)) % size];
    }

    void shift(Direction direction, std::uint8_t insertedRgb222) noexcept {
        insertedRgb222 &= 0x3Fu;
        if (direction == Direction::Left) {
            for (std::size_t i = 0; i + 1 < size; ++i) entries_[i] = entries_[i + 1];
            entries_[size - 1] = insertedRgb222;
        } else {
            for (std::size_t i = size - 1; i > 0; --i) entries_[i] = entries_[i - 1];
            entries_[0] = insertedRgb222;
        }
    }

    void fadeTo(const Entries& target, std::uint32_t step,
                std::uint32_t steps) noexcept {
        if (steps == 0) return;
        if (step > steps) step = steps;
        for (std::size_t i = 0; i < size; ++i) {
            const auto from = entries_[i];
            const auto to = static_cast<std::uint8_t>(target[i] & 0x3Fu);
            std::uint8_t result = 0;
            for (unsigned channel = 0; channel < 3; ++channel) {
                const auto shift = 4u - channel * 2u;
                const auto a = (from >> shift) & 0x03u;
                const auto b = (to >> shift) & 0x03u;
                const auto mixed = (a * (steps - step) + b * step + steps / 2) / steps;
                result |= static_cast<std::uint8_t>(mixed << shift);
            }
            entries_[i] = result;
        }
    }

    void fadeToColor(std::uint8_t rgb222, std::uint32_t step,
                     std::uint32_t steps) noexcept {
        Entries target{};
        target.fill(rgb222 & 0x3Fu);
        fadeTo(target, step, steps);
    }

    void save() noexcept { saved_ = entries_; }
    void restore() noexcept { entries_ = saved_; }

private:
    Entries entries_{};
    Entries saved_{};
};

// A single producer edits a private copy. Scanout applies published palettes
// at a frame boundary before encoding rows for that frame.
class PaletteExchange {
public:
    PaletteExchange() noexcept : banks_{Palette{}, Palette{}} {}
    PaletteExchange(const PaletteExchange&) = delete;
    PaletteExchange& operator=(const PaletteExchange&) = delete;

    const Palette& active() const noexcept {
        return banks_[activeIndex_.load(std::memory_order_acquire)];
    }

    Palette* beginEdit() noexcept {
        if (pending_.load(std::memory_order_acquire)) return nullptr;
        const auto active = activeIndex_.load(std::memory_order_acquire);
        editIndex_ = static_cast<std::uint8_t>(active ^ 1u);
        banks_[editIndex_] = banks_[active];
        editing_ = true;
        return &banks_[editIndex_];
    }

    bool publish() noexcept {
        if (!editing_ || pending_.load(std::memory_order_acquire)) return false;
        pendingIndex_.store(editIndex_, std::memory_order_relaxed);
        pending_.store(true, std::memory_order_release);
        editing_ = false;
        return true;
    }

    bool applyAtFrameBoundary() noexcept {
        if (!pending_.load(std::memory_order_acquire)) return false;
        activeIndex_.store(pendingIndex_.load(std::memory_order_relaxed),
                           std::memory_order_release);
        pending_.store(false, std::memory_order_release);
        return true;
    }

    bool hasPending() const noexcept { return pending_.load(std::memory_order_acquire); }

private:
    std::array<Palette, 2> banks_;
    std::atomic<std::uint8_t> activeIndex_{0};
    std::atomic<std::uint8_t> pendingIndex_{1};
    std::atomic<bool> pending_{false};
    std::uint8_t editIndex_ = 1;
    bool editing_ = false;
};

} // namespace ql32::platform::olimex_vga
