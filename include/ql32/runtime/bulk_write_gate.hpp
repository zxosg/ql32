#pragma once

#include <cstddef>
#include <mutex>

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

// Holds the gate for the duration of one flash-touching operation (open,
// write+flush, close, rename...). Released on every exit path, including
// exceptions. Adapters may linger the release so back-to-back operations of
// one transfer keep a single steady hold.
class BulkWriteScope {
public:
    explicit BulkWriteScope(BulkWriteGate* gate) : gate_(gate) {
        if (gate_) gate_->hold();
    }
    BulkWriteScope(const BulkWriteScope&) = delete;
    BulkWriteScope& operator=(const BulkWriteScope&) = delete;
    ~BulkWriteScope() {
        if (gate_) gate_->release();
    }

private:
    BulkWriteGate* gate_;
};

} // namespace ql32::runtime
