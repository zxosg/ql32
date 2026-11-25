// Translation-time heap budget: measures peak, retained, and the largest single
// allocation while compiling BASIC source. The largest single allocation is what
// must fit the ESP32's biggest free block, so it must not grow by doubling.
#include "language/bytecode.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <new>
#include <string>

namespace {

std::atomic<std::size_t> live{0};
std::atomic<std::size_t> peak{0};
std::atomic<std::size_t> largest{0};
constexpr std::size_t header = 16;

void* track(std::size_t size) {
    void* raw = std::malloc(size + header);
    if (!raw) throw std::bad_alloc();
    *static_cast<std::size_t*>(raw) = size;
    const std::size_t now = live += size;
    std::size_t seen = peak.load();
    while (now > seen && !peak.compare_exchange_weak(seen, now)) {}
    seen = largest.load();
    while (size > seen && !largest.compare_exchange_weak(seen, size)) {}
    return static_cast<char*>(raw) + header;
}

void untrack(void* pointer) noexcept {
    if (!pointer) return;
    void* raw = static_cast<char*>(pointer) - header;
    live -= *static_cast<std::size_t*>(raw);
    std::free(raw);
}

} // namespace

void* operator new(std::size_t size) { return track(size); }
void* operator new[](std::size_t size) { return track(size); }
void operator delete(void* pointer) noexcept { untrack(pointer); }
void operator delete[](void* pointer) noexcept { untrack(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { untrack(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { untrack(pointer); }

namespace {

struct Measurement {
    std::size_t peak = 0, retained = 0, largest = 0, instructions = 0;
};

// 14 tokens per generated line (including the line number and newline).
std::string program(int lines) {
    std::string source;
    for (int i = 1; i <= lines; ++i)
        source += std::to_string(i * 10) + " v = v + i * 2 : PRINT \"x\"; v\n";
    return source;
}

Measurement measure(const std::string& source) {
    const std::size_t base = live.load();
    peak = base;
    largest = 0;
    Measurement result;
    {
        const auto code = ql32::detail::compileSource(source);
        result.instructions = code.size();
        result.retained = live.load() - base;
    }
    result.peak = peak.load() - base;
    result.largest = largest.load();
    return result;
}

void check(const char* name, int lines, std::size_t largestBound, std::size_t peakBound) {
    const std::string source = program(lines);
    const Measurement m = measure(source);
    std::printf("%s: %zu source B, %zu instr, peak %zu B, retained %zu B, "
                "largest single allocation %zu B\n",
                name, source.size(), m.instructions, m.peak, m.retained, m.largest);
    if (m.peak > peakBound) {
        std::cerr << "memory budget test failed: " << name << " peak " << m.peak
                  << " exceeds " << peakBound << '\n';
        std::exit(1);
    }
    if (m.largest > largestBound) {
        std::cerr << "memory budget test failed: " << name << " largest allocation "
                  << m.largest << " exceeds " << largestBound << '\n';
        std::exit(1);
    }
}

} // namespace

int main() {
#if INTPTR_MAX == INT64_MAX && !defined(__SANITIZE_ADDRESS__)
    // Compact tokens in a chunked deque: no allocation above one node. Before P1
    // the token vector alone needed 119 KB / 262 KB; peak was 179 KB / 393 KB.
    check("~1190 tokens (85 lines)", 85, 8192, 100000);
    check("~2800 tokens (200 lines)", 200, 8192, 220000);
#endif
    return 0;
}
