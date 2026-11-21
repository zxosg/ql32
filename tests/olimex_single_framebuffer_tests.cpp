// Compiled with QL32_PACKED4_SHARED_WRITES: the single-framebuffer mode.
#include "ql32/platform/olimex_vga/framebuffer.hpp"

#include <array>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
using namespace ql32::platform::olimex_vga;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

constexpr std::uint32_t kWidth = 400;
constexpr std::uint32_t kHeight = 300;

FramebufferView make(std::vector<std::uint8_t>& storage) {
    storage.assign(kWidth / 2 * kHeight, 0);
    return {storage.data(), kWidth, kHeight, kWidth / 2, PixelFormat::Indexed4};
}

// Two producers own the even and odd columns of the same bytes. Any lost
// read-modify-write shows up as a wrong nibble.
void testConcurrentNeighbourNibbles() {
    std::vector<std::uint8_t> storage;
    const auto fb = make(storage);
    auto producer = [&](std::uint32_t parity, std::uint8_t base) {
        for (int pass = 0; pass < 20; ++pass)
            for (std::uint32_t y = 0; y < kHeight; ++y)
                for (std::uint32_t x = parity; x < kWidth; x += 2)
                    writePixel(fb, x, y, static_cast<std::uint8_t>(base + (pass & 1)));
    };
    std::vector<std::thread> threads;
    threads.emplace_back(producer, 0, 2);
    threads.emplace_back(producer, 1, 8);
    for (auto& t : threads) t.join();
    for (std::uint32_t y = 0; y < kHeight; y += 7)
        for (std::uint32_t x = 0; x < kWidth; ++x)
            require(readPixel(fb, x, y) == ((x & 1) ? 9 : 3), "lost nibble update");
}

// Concurrent rectangles sharing an odd column boundary and mapped-row writes
// must not disturb each other or adjacent rows.
void testConcurrentRectanglesAndRows() {
    std::vector<std::uint8_t> storage;
    const auto fb = make(storage);
    std::array<std::uint8_t, 16> identity{};
    for (std::size_t i = 0; i < identity.size(); ++i) identity[i] = static_cast<std::uint8_t>(i);
    std::thread a([&] {
        for (int i = 0; i < 200; ++i) fillRect(fb, 0, 0, 201, kHeight, 5);
    });
    std::thread b([&] {
        std::vector<std::uint8_t> row(kWidth - 201, 11);
        for (int i = 0; i < 200; ++i)
            for (std::uint32_t y = 0; y < kHeight; ++y)
                writeMappedRowChunked(fb, 201, y, row.data(),
                                      static_cast<std::uint32_t>(row.size()),
                                      identity.data());
    });
    a.join();
    b.join();
    for (std::uint32_t y = 0; y < kHeight; y += 13) {
        for (std::uint32_t x = 0; x < kWidth; ++x)
            require(readPixel(fb, x, y) == (x < 201 ? 5 : 11), "rectangle/row race");
    }
}

void testRowBoundaryDrawing() {
    std::vector<std::uint8_t> storage;
    const auto fb = make(storage);
    writePixel(fb, kWidth - 1, 10, 7);
    writePixel(fb, 0, 11, 9);
    require(readPixel(fb, kWidth - 1, 10) == 7 && readPixel(fb, 0, 11) == 9,
            "row boundary pixels");
    require(readPixel(fb, kWidth - 2, 10) == 0 && readPixel(fb, 1, 11) == 0,
            "row boundary neighbours changed");
    require(!writePixel(fb, kWidth, 0, 1) && !writePixel(fb, 0, kHeight, 1),
            "out-of-range write accepted");
    clear(fb, 3);
    require(readPixel(fb, 399, 299) == 3 && readPixel(fb, 0, 0) == 3, "clear");
    require(fb.storageBytes() == 60000, "single framebuffer is 60,000 bytes");
}

} // namespace

int main() {
    testConcurrentNeighbourNibbles();
    testConcurrentRectanglesAndRows();
    testRowBoundaryDrawing();
}
