#pragma once

// Relocation slab for retained program data. While a BumpScope is active on a
// thread, operator new serves from the region; deleting a region pointer is a
// no-op and the whole slab is freed with the region. This lets a compiled
// program be copied into one contiguous block so the scattered originals free
// and coalesce (heap-fragmentation work).
#include <cstddef>
#include <memory>

namespace ql32::detail {

class BumpRegion {
public:
    // capacity 0 only measures: allocations use the normal heap and are summed.
    explicit BumpRegion(std::size_t capacity);
    ~BumpRegion();
    BumpRegion(const BumpRegion&) = delete;
    BumpRegion& operator=(const BumpRegion&) = delete;
    std::size_t used() const noexcept { return used_; }
    bool valid() const noexcept { return capacity_ == 0 || base_ != nullptr; }

private:
    friend class BumpScope;
    friend void* bumpAllocate(std::size_t n);
    unsigned char* base_ = nullptr;
    std::size_t capacity_ = 0;
    std::size_t used_ = 0;
};

class BumpScope {
public:
    explicit BumpScope(BumpRegion& region);
    ~BumpScope();
    BumpScope(const BumpScope&) = delete;
    BumpScope& operator=(const BumpScope&) = delete;

private:
    BumpRegion* previous_;
};

// Returns nullptr when no scope is active or the region cannot serve `n`.
void* bumpAllocate(std::size_t n);
bool bumpOwns(const void* p) noexcept;

} // namespace ql32::detail
