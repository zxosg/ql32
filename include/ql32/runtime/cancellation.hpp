#pragma once

#include <atomic>

namespace ql32::runtime {

// Small C++17 cancellation primitive shared by the portable VM and its host
// or embedded driver. Tokens are non-owning; keep their source alive while the
// operation runs.
class CancellationToken {
public:
    CancellationToken() = default;
    bool stopRequested() const noexcept {
        return state_ && state_->load(std::memory_order_relaxed);
    }

private:
    friend class CancellationSource;
    explicit CancellationToken(const std::atomic_bool* state) : state_(state) {}
    const std::atomic_bool* state_ = nullptr;
};

class CancellationSource {
public:
    CancellationToken token() const noexcept { return CancellationToken(&state_); }
    void requestStop() noexcept { state_.store(true, std::memory_order_relaxed); }
    bool stopRequested() const noexcept { return state_.load(std::memory_order_relaxed); }

private:
    std::atomic_bool state_{false};
};

} // namespace ql32::runtime
