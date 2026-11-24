#include "ql32/platform/fabgl_vga/display_modes.hpp"
#include <esp_heap_caps.h>

#include <stdexcept>
#include <string>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void testFragmentedHeapRefusesSameModeDoubleBufferAndRestores() {
    fabgl::VGAController display;
    display.setDoubleBuffered(false);
    fabgl::Canvas canvas;
    canvas.display = &display;
    ql32::platform::fabgl_vga::DisplayModes modes(display, canvas);
    fake_heap::largestBlocks = {106484, 90000, 40000};
    bool rejected = false;
    try {
        (void)modes.setMode(0, 1);
    } catch (const std::runtime_error& error) {
        rejected = std::string(error.what()).find("contiguous DMA memory") != std::string::npos;
    }
    require(rejected && display.resolutionCount == 1 && display.endCount == 1,
            "partial double buffering was attempted instead of restoring the old mode");
    require(modes.currentDisplayMode().id == 0 && modes.currentDisplayMode().options == 0 &&
                canvas.getHeight() == 200 && fake_heap::liveAllocations == 0,
            "failed probe leaked memory or changed the active mode");
}

void testMultiplePoolsAllowGrowthAndSubsequentShrink() {
    fabgl::VGAController display;
    display.setDoubleBuffered(false);
    fabgl::Canvas canvas;
    canvas.display = &display;
    ql32::platform::fabgl_vga::DisplayModes modes(display, canvas);
    fake_heap::largestBlocks = {106484, 100000, 70000};
    const auto grown = modes.setMode(1, 0);
    require(grown.id == 1 && grown.width == 320 && grown.height == 240 &&
                fake_heap::liveAllocations == 0,
            "multi-pool probe rejected a complete viewport or leaked memory");
    require(modes.setMode(0, 0).height == 200, "shrinking did not apply the old geometry");
    const auto calls = display.resolutionCount;
    (void)modes.setMode(0, 0);
    require(display.resolutionCount == calls, "repeated mode request reallocated the display");
}

void testDiagnosticsAndRollbackFailure() {
    fabgl::VGAController display;
    display.setDoubleBuffered(false);
    fabgl::Canvas canvas;
    canvas.display = &display;
    ql32::platform::fabgl_vga::DisplayModes modes(display, canvas);
    bool explained = false;
    try {
        (void)modes.setMode(5, 0);
    } catch (const std::invalid_argument& error) {
        explained = std::string(error.what()).find("0 (320x200)") != std::string::npos &&
                    std::string(error.what()).find("4 (640x480)") != std::string::npos;
    }
    require(explained, "invalid mode did not identify this build's supported presets");
    fake_heap::largestBlocks = {106484, 40000};
    display.allocationResults = {{320, 100}};
    bool rollbackReported = false;
    try {
        (void)modes.setMode(0, 1);
    } catch (const std::runtime_error& error) {
        rollbackReported = std::string(error.what()).find("VGA rollback failed") != std::string::npos;
    }
    require(rollbackReported && fake_heap::liveAllocations == 0,
            "failed restoration was swallowed or leaked probe memory");
}
}

int main() {
    testFragmentedHeapRefusesSameModeDoubleBufferAndRestores();
    testMultiplePoolsAllowGrowthAndSubsequentShrink();
    testDiagnosticsAndRollbackFailure();
}
