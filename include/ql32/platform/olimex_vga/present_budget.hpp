#pragma once

#include <cstdint>

namespace ql32::platform::olimex_vga {

// Bounds the PSRAM bytes a presenter writes per video frame. The caller feeds
// the scanout frame counter; once the budget for the current frame is spent
// the presenter must wait for the counter to advance. A limit of zero means
// unlimited. Portable: no board or RTOS dependency.
class PresentBudget {
public:
    explicit PresentBudget(std::uint32_t bytesPerFrame = 0) noexcept
        : limit_(bytesPerFrame) {}

    std::uint32_t limit() const noexcept { return limit_; }
    void setLimit(std::uint32_t bytesPerFrame) noexcept { limit_ = bytesPerFrame; }

    // Records written bytes; returns true when the caller should wait for the
    // next frame before writing more.
    bool charge(std::uint32_t frame, std::uint32_t bytes) noexcept {
        if (limit_ == 0) return false;
        if (frame != frame_) {
            frame_ = frame;
            used_ = 0;
        }
        used_ += bytes;
        return used_ >= limit_;
    }

    // True once the budget of the frame most recently charged is spent and the
    // scanout has not yet moved on.
    bool exhausted(std::uint32_t frame) const noexcept {
        return limit_ != 0 && frame == frame_ && used_ >= limit_;
    }

private:
    std::uint32_t limit_ = 0;
    std::uint32_t frame_ = 0;
    std::uint32_t used_ = 0;
};

} // namespace ql32::platform::olimex_vga
