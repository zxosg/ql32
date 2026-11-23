#pragma once

#include <cstddef>
#include <mutex>
#include <utility>

namespace ql32::runtime {

// Platform-neutral notification that a slow, display-disturbing storage write
// is in progress (for example internal flash, which stalls both CPU cores).
// A platform adapter decides what to do, such as holding a steady picture.
class BulkWriteGate {
public:
    virtual ~BulkWriteGate() = default;
    virtual void hold() = 0;
    virtual void release() = 0;
};

// Collapses nested/concurrent hold/release requests into one begin/end pair.
class CountedBulkWriteGate final : public BulkWriteGate {
public:
    using Hook = void (*)(void* context);

    CountedBulkWriteGate(Hook begin, Hook end, void* context)
        : begin_(begin), end_(end), context_(context) {}

    void hold() override {
        std::lock_guard lock(mutex_);
        if (count_++ == 0 && begin_) begin_(context_);
    }
    void release() override {
        std::lock_guard lock(mutex_);
        if (count_ > 0 && --count_ == 0 && end_) end_(context_);
    }
    std::size_t activeHolds() const {
        std::lock_guard lock(mutex_);
        return count_;
    }

private:
    Hook begin_;
    Hook end_;
    void* context_;
    mutable std::mutex mutex_;
    std::size_t count_ = 0;
};

// Follows one write stream. The gate is held once the stream has written
// `threshold` bytes (one SPIFFS block, so small saves never hold) and is
// released by finish() or destruction, including on error paths.
class BulkWriteTracker {
public:
    static constexpr std::size_t kDefaultThreshold = 4096;

    BulkWriteTracker() = default;
    explicit BulkWriteTracker(BulkWriteGate* gate,
                              std::size_t threshold = kDefaultThreshold)
        : gate_(gate), threshold_(threshold) {}
    BulkWriteTracker(const BulkWriteTracker&) = delete;
    BulkWriteTracker& operator=(const BulkWriteTracker&) = delete;
    BulkWriteTracker(BulkWriteTracker&& other) noexcept { *this = std::move(other); }
    BulkWriteTracker& operator=(BulkWriteTracker&& other) noexcept {
        if (this != &other) {
            finish();
            gate_ = std::exchange(other.gate_, nullptr);
            threshold_ = other.threshold_;
            written_ = other.written_;
            held_ = std::exchange(other.held_, false);
        }
        return *this;
    }
    ~BulkWriteTracker() { finish(); }

    // Call before the write that adds `bytes`, so the hold is in place before
    // the flash operation starts.
    void add(std::size_t bytes) {
        written_ += bytes;
        if (gate_ && !held_ && written_ >= threshold_) {
            held_ = true;
            gate_->hold();
        }
    }
    void finish() noexcept {
        if (held_) {
            held_ = false;
            if (gate_) gate_->release();
        }
    }
    bool held() const noexcept { return held_; }

private:
    BulkWriteGate* gate_ = nullptr;
    std::size_t threshold_ = kDefaultThreshold;
    std::size_t written_ = 0;
    bool held_ = false;
};

} // namespace ql32::runtime
