#include "ql32/platform/fabgl_vga/frame_presenter.hpp"

#include <array>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::uint32_t scanlineOffset(std::uint32_t x) {
    return (x & ~3u) | ((x + 2u) & 3u);
}

void testPartialDoubleBufferedPresentKeepsBuffersSynchronized() {
    fabgl::VGAController display(8, 3);
    fabgl::Canvas canvas;
    canvas.width = 8;
    canvas.height = 3;
    canvas.display = &display;
    ql32::platform::fabgl_vga::FabglFramePresenter presenter(display, canvas);

    std::array<ql32::graphics::RgbColor, 256> palette{};
    palette[1] = {255, 0, 0};
    palette[2] = {0, 255, 0};
    palette[3] = {0, 0, 255};
    std::vector<std::vector<ql32::graphics::ColorIndex>> pixels(
        3, std::vector<ql32::graphics::ColorIndex>(8, 0));
    pixels[1][1] = 1;
    pixels[1][2] = 2;

    const auto rowProvider = [&pixels](std::uint32_t y,
                                       std::vector<ql32::graphics::ColorIndex>& row) {
        row = pixels[y];
    };
    const ql32::graphics::Rect region{1, 1, 2, 1};
    int regionProviderCalls = 0;
    const auto regionProvider = [&pixels, region, &regionProviderCalls](
        std::uint32_t y, std::vector<ql32::graphics::ColorIndex>& row) {
        ++regionProviderCalls;
        row.assign(pixels[y].begin() + region.x,
                   pixels[y].begin() + region.x + region.width);
    };
    require(presenter.presentRows(8, 3, palette, rowProvider),
            "initial full frame was rejected");
    require(display.swapCount == 1 && canvas.swapCount == 1,
            "full present did not swap exactly once");
    require(display.drawBuffer() != display.visibleBuffer(),
            "initial full present unexpectedly synchronized an unused buffer");
    require(!presenter.presentRowsRegion(8, 3, palette, region, regionProvider),
            "partial frame was accepted before both buffers were initialized");
    require(presenter.presentRows(8, 3, palette, rowProvider),
            "full present could not synchronize buffers after a partial request");
    require(display.swapCount == 2 && canvas.swapCount == 2,
            "buffer synchronization did not use exactly one additional swap");
    require(display.drawBuffer() == display.visibleBuffer(),
            "full synchronization left double buffers out of sync");

    pixels[1][1] = 3;
    pixels[1][2] = 1;
    require(presenter.presentRowsRegion(8, 3, palette, region, regionProvider),
            "partial frame was rejected");
    require(regionProviderCalls == 1,
            "partial region was recomposed instead of staged once");
    require(display.swapCount == 3 && canvas.swapCount == 3,
            "partial present did not swap exactly once");
    require(display.drawBuffer() == display.visibleBuffer(),
            "partial present left double buffers out of sync");

    const auto& visible = display.visibleBuffer();
    require(visible[1][scanlineOffset(1)] == 0x03 &&
            visible[1][scanlineOffset(2)] == 0x30,
            "partial present wrote pixels to the wrong raw scanline positions");
    require(visible[1][scanlineOffset(0)] == 0 &&
            visible[1][scanlineOffset(3)] == 0,
            "partial present changed pixels outside its region");

    display.failOnScanlineCall = display.scanlineCallCount + 2;
    bool failed = false;
    try {
        (void)presenter.presentRowsRegion(8, 3, palette, region, regionProvider);
    } catch (const std::runtime_error&) {
        failed = true;
    }
    require(failed, "simulated second-buffer write did not fail");
    require(!presenter.presentRowsRegion(8, 3, palette, region, regionProvider),
            "partial update was accepted after buffer synchronization failed");
    require(presenter.presentRows(8, 3, palette, rowProvider),
            "full present could not recover after a partial update failure");
    require(display.drawBuffer() == display.visibleBuffer(),
            "full recovery did not resynchronize both buffers");
}

void testLargeRegionFallbackThreshold() {
    fabgl::VGAController display(8, 20);
    fabgl::Canvas canvas;
    canvas.width = 8;
    canvas.height = 20;
    canvas.display = &display;
    ql32::platform::fabgl_vga::FabglFramePresenter presenter(display, canvas);
    std::array<ql32::graphics::RgbColor, 256> palette{};
    palette[1] = {255, 0, 0};
    palette[2] = {0, 255, 0};
    palette[3] = {0, 0, 255};
    const auto rowProvider = [](std::uint32_t y,
                                std::vector<ql32::graphics::ColorIndex>& row) {
        row.resize(8);
        for (std::uint32_t x = 0; x < 8; ++x)
            row[x] = static_cast<ql32::graphics::ColorIndex>((x + y) % 3 + 1);
    };
    require(presenter.presentRows(8, 20, palette, rowProvider),
            "initial full frame was rejected");
    require(!presenter.presentRowsRegion(8, 20, palette, {0, 0, 1, 1},
                                         [](std::uint32_t,
                                            std::vector<ql32::graphics::ColorIndex>& row) {
                                             row.assign(1, 0);
                                         }),
            "partial update was accepted before buffer synchronization");
    require(presenter.presentRows(8, 20, palette, rowProvider),
            "full frame did not synchronize buffers");
    require(presenter.presentRowsRegion(8, 20, palette, {0, 0, 8, 10},
                                         rowProvider),
            "partial update below half the frame incorrectly requested a full frame");
    const auto& visible = display.visibleBuffer();
    require(visible[0][scanlineOffset(0)] == 0x30 &&
            visible[0][scanlineOffset(1)] == 0x0c &&
            visible[0][scanlineOffset(2)] == 0x03 &&
            visible[0][scanlineOffset(3)] == 0x30,
            "contiguous row copy did not preserve FabGL scanline byte order");
    require(!presenter.presentRowsRegion(8, 20, palette, {0, 0, 8, 11},
                                          rowProvider),
            "greater-than-half-frame update did not request a full frame");
}

} // namespace

int main() {
    testPartialDoubleBufferedPresentKeepsBuffersSynchronized();
    testLargeRegionFallbackThreshold();
}
