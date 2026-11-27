#include "bump_region.hpp"

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <thread>
#if defined(ARDUINO)
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

namespace ql32::detail {
namespace {
// Plain globals (no TLS): operator new runs before the runtime sets up thread-local storage.
std::atomic<BumpRegion*> gActive{nullptr};
std::atomic<std::uintptr_t> gOwner{0};
std::uintptr_t currentThreadKey() {
#if defined(ARDUINO)
    return reinterpret_cast<std::uintptr_t>(xTaskGetCurrentTaskHandle());
#else
    return std::hash<std::thread::id>()(std::this_thread::get_id());
#endif
}
constexpr int MaxRegions = 8;
struct Range { std::atomic<std::uintptr_t> begin{0}; std::atomic<std::uintptr_t> end{0}; };
Range gRanges[MaxRegions];
}

BumpRegion::BumpRegion(std::size_t capacity) : capacity_(capacity) {
    if (capacity == 0) return;
    base_ = static_cast<unsigned char*>(std::malloc(capacity));
    if (!base_) return;
    for (auto& r : gRanges) {
        std::uintptr_t expected = 0;
        if (r.begin.compare_exchange_strong(expected, reinterpret_cast<std::uintptr_t>(base_))) {
            r.end.store(reinterpret_cast<std::uintptr_t>(base_) + capacity);
            return;
        }
    }
    std::free(base_);  // no free registry slot
    base_ = nullptr;
}

BumpRegion::~BumpRegion() {
    if (!base_) return;
    for (auto& r : gRanges)
        if (r.begin.load() == reinterpret_cast<std::uintptr_t>(base_)) { r.end.store(0); r.begin.store(0); }
    std::free(base_);
}

BumpScope::BumpScope(BumpRegion& region) : previous_(gActive.load()) {
    gOwner.store(currentThreadKey());
    gActive.store(&region);
}
BumpScope::~BumpScope() { gActive.store(previous_); }

void* bumpAllocate(std::size_t n) {
    BumpRegion* r = gActive.load();
    if (!r || gOwner.load() != currentThreadKey()) return nullptr;
    n = (n + 7u) & ~std::size_t{7};
    if (r->capacity_ == 0) { r->used_ += n; return nullptr; }  // measuring pass
    if (!r->base_ || r->used_ + n > r->capacity_) return nullptr;
    void* p = r->base_ + r->used_;
    r->used_ += n;
    return p;
}

bool bumpOwns(const void* p) noexcept {
    const auto v = reinterpret_cast<std::uintptr_t>(p);
    for (auto& r : gRanges) {
        const auto b = r.begin.load(std::memory_order_relaxed);
        if (b && v >= b && v < r.end.load(std::memory_order_relaxed)) return true;
    }
    return false;
}

} // namespace ql32::detail

#if (defined(ARDUINO) || defined(QL32_BUMP_NEW)) && !defined(QL32_HEAP_TRACK)
namespace {
void* allocate(std::size_t n) {
    if (void* p = ql32::detail::bumpAllocate(n)) return p;
    return std::malloc(n ? n : 1);
}
void release(void* p) noexcept {
    if (p && !ql32::detail::bumpOwns(p)) std::free(p);
}
}
void* operator new(std::size_t n) { void* p = allocate(n); if (!p) throw std::bad_alloc(); return p; }
void* operator new[](std::size_t n) { void* p = allocate(n); if (!p) throw std::bad_alloc(); return p; }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept { return allocate(n); }
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { return allocate(n); }
void operator delete(void* p) noexcept { release(p); }
void operator delete[](void* p) noexcept { release(p); }
void operator delete(void* p, std::size_t) noexcept { release(p); }
void operator delete[](void* p, std::size_t) noexcept { release(p); }
#endif
