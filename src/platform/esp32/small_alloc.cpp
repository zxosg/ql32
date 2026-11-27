// Small-object region for the global operator new.
//
// Compiling a long program allocates thousands of short-lived objects interleaved with the
// few long-lived ones (segments, names, pool nodes). Freed, they leave the heap as many
// small holes pinned by tiny live blocks, so the largest free block collapses even though
// plenty is free in total. This replaces operator new/delete so objects up to 512 B come
// from one contiguous region reserved at boot (size-class pages, reclaimed per page),
// keeping the general heap's large blocks intact. Overflow falls back to the heap.
// Compile-time arena for the global operator new.
//
// Compiling a long program allocates thousands of short-lived objects interleaved with the
// few long-lived ones. Freed, they leave the heap as many small holes pinned by tiny live
// blocks, so the largest free block collapses even though plenty is free in total. While a
// compile scope is open, the compiling task's allocations up to 512 B come from one block
// reserved for the scope (size-class pages). When the scope has ended and every object in
// it is freed, the block goes back to the heap whole. Overflow falls back to the heap.
#if defined(QL32_SMALL_ALLOC_BYTES)

#include "ql32/platform/esp32/small_alloc.hpp"

#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>

namespace {

constexpr std::size_t PageBytes = 1024;
constexpr std::size_t MaxPages = 64;
constexpr std::size_t ClassBytes[] = {16, 32, 48, 64, 96, 128, 192, 256, 384, 512};
constexpr int ClassCount = sizeof(ClassBytes) / sizeof(ClassBytes[0]);
constexpr std::uint8_t Unassigned = 0xFF;

struct FreeNode { FreeNode* next; };

portMUX_TYPE gLock = portMUX_INITIALIZER_UNLOCKED;
char* gBase = nullptr;
std::size_t gPages = 0;
bool gScopeOpen = false;
void* gOwner = nullptr;
std::uint8_t gPageClass[MaxPages];
std::uint16_t gPageUsed[MaxPages];
FreeNode* gFree[ClassCount];
std::size_t gUsed = 0, gPeak = 0, gFallbacks = 0, gReleases = 0, gLeftover = 0;

int classFor(std::size_t size) {
    for (int i = 0; i < ClassCount; ++i)
        if (size <= ClassBytes[i]) return i;
    return -1;
}

void* takeLocked(int cls) {
    if (!gFree[cls]) {
        std::size_t page = 0;
        while (page < gPages && gPageClass[page] != Unassigned) ++page;
        if (page == gPages) return nullptr;
        gPageClass[page] = static_cast<std::uint8_t>(cls);
        const std::size_t per = PageBytes / ClassBytes[cls];
        char* start = gBase + page * PageBytes;
        for (std::size_t i = per; i-- > 0;) {
            auto* node = reinterpret_cast<FreeNode*>(start + i * ClassBytes[cls]);
            node->next = gFree[cls];
            gFree[cls] = node;
        }
    }
    FreeNode* node = gFree[cls];
    gFree[cls] = node->next;
    const std::size_t page = (reinterpret_cast<char*>(node) - gBase) / PageBytes;
    ++gPageUsed[page];
    gUsed += ClassBytes[cls];
    if (gUsed > gPeak) gPeak = gUsed;
    return node;
}

// Returns the block to free (outside the lock) once the arena is closed and empty.
char* releaseLocked(void* ptr) {
    const std::size_t page = (static_cast<char*>(ptr) - gBase) / PageBytes;
    const int cls = gPageClass[page];
    auto* node = static_cast<FreeNode*>(ptr);
    node->next = gFree[cls];
    gFree[cls] = node;
    gUsed -= ClassBytes[cls];
    if (--gPageUsed[page] != 0) return nullptr;
    char* start = gBase + page * PageBytes;
    FreeNode** link = &gFree[cls];
    while (*link) {
        char* address = reinterpret_cast<char*>(*link);
        if (address >= start && address < start + PageBytes) *link = (*link)->next;
        else link = &(*link)->next;
    }
    gPageClass[page] = Unassigned;
    if (gScopeOpen || gUsed != 0) return nullptr;
    char* block = gBase;
    gBase = nullptr;
    gPages = 0;
    ++gReleases;
    return block;
}

void arenaBegin() {
    portENTER_CRITICAL(&gLock);
    const bool have = gBase != nullptr;
    portEXIT_CRITICAL(&gLock);
    char* block = nullptr;
    if (!have) {
        const std::size_t bytes = QL32_SMALL_ALLOC_BYTES;
        if (heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) >= 2 * bytes)
            block = static_cast<char*>(heap_caps_malloc(bytes, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL));
    }
    portENTER_CRITICAL(&gLock);
    if (block && !gBase) {
        gBase = block;
        gPages = QL32_SMALL_ALLOC_BYTES / PageBytes;
        if (gPages > MaxPages) gPages = MaxPages;
        for (std::size_t i = 0; i < gPages; ++i) { gPageClass[i] = Unassigned; gPageUsed[i] = 0; }
        for (auto& list : gFree) list = nullptr;
        gUsed = 0;
        block = nullptr;
    }
    gOwner = xTaskGetCurrentTaskHandle();
    gScopeOpen = gBase != nullptr;
    portEXIT_CRITICAL(&gLock);
    if (block) heap_caps_free(block);
}

void arenaEnd() {
    char* release = nullptr;
    portENTER_CRITICAL(&gLock);
    gScopeOpen = false;
    gOwner = nullptr;
    if (gBase && gUsed == 0) {
        release = gBase;
        gBase = nullptr;
        gPages = 0;
        ++gReleases;
    } else if (gBase) {
        gLeftover = gUsed;
    }
    portEXIT_CRITICAL(&gLock);
    if (release) heap_caps_free(release);
}

} // namespace

namespace ql32::platform::esp32 {
SmallAllocStats smallAllocStats() {
    portENTER_CRITICAL(&gLock);
    SmallAllocStats stats{gPages * PageBytes, gUsed, gPeak, gFallbacks, gLeftover, gReleases};
    portEXIT_CRITICAL(&gLock);
    return stats;
}
void smallAllocDump(void (*emit)(const char*)) {
    static char lines[48][100];
    std::size_t lineCount = 0;
    portENTER_CRITICAL(&gLock);
    static std::uint64_t freeBits[MaxPages];
    for (std::size_t i = 0; i < MaxPages; ++i) freeBits[i] = 0;
    for (int c = 0; c < ClassCount; ++c)
        for (FreeNode* n = gFree[c]; n; n = n->next) {
            const std::size_t off = reinterpret_cast<char*>(n) - gBase;
            freeBits[off / PageBytes] |= 1ull << ((off % PageBytes) / ClassBytes[c]);
        }
    for (std::size_t page = 0; gBase && page < gPages; ++page) {
        if (gPageClass[page] == Unassigned) continue;
        const std::size_t size = ClassBytes[gPageClass[page]];
        for (std::size_t i = 0; i < PageBytes / size; ++i) {
            if (freeBits[page] & (1ull << i)) continue;
            const unsigned char* b = reinterpret_cast<const unsigned char*>(gBase + page * PageBytes + i * size);
            if (lineCount == 48) continue;
            char* line = lines[lineCount++];
            int len = std::snprintf(line, 100, "ARENA|%u|%u|", static_cast<unsigned>(size), static_cast<unsigned>(page));
            for (int k = 0; k < 24 && k < static_cast<int>(size); ++k)
                len += std::snprintf(line + len, 100 - len, "%02x", b[k]);
        }
    }
    portEXIT_CRITICAL(&gLock);
    for (std::size_t i = 0; i < lineCount; ++i) emit(lines[i]);
}
void smallAllocHooks(void (**begin)(), void (**end)()) {
    *begin = arenaBegin;
    *end = arenaEnd;
}
} // namespace ql32::platform::esp32

void* operator new(std::size_t size) {
    if (size <= ClassBytes[ClassCount - 1] && gScopeOpen && gOwner == xTaskGetCurrentTaskHandle()) {
        const int cls = classFor(size);
        portENTER_CRITICAL(&gLock);
        void* p = (gScopeOpen && gBase) ? takeLocked(cls) : nullptr;
        if (!p) ++gFallbacks;
        portEXIT_CRITICAL(&gLock);
        if (p) return p;
    }
    void* p = std::malloc(size ? size : 1);
    if (!p) throw std::bad_alloc();
    return p;
}

void operator delete(void* ptr) noexcept {
    if (!ptr) return;
    char* p = static_cast<char*>(ptr);
    if (gBase && p >= gBase && p < gBase + gPages * PageBytes) {
        portENTER_CRITICAL(&gLock);
        char* release = releaseLocked(ptr);
        portEXIT_CRITICAL(&gLock);
        if (release) heap_caps_free(release);
        return;
    }
    std::free(ptr);
}

void operator delete(void* ptr, std::size_t) noexcept { operator delete(ptr); }

#endif
