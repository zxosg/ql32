#include "ql32/platform/tenstar_esp32s3/frame_presenter.hpp"

#include <cassert>
#include <stdexcept>

using namespace ql32;
using namespace ql32::platform::tenstar_esp32s3;

int main() {
    SPIClass spi;
    TenstarSt7789Display display(&spi, 39, 7, 40);
    display.beginTenstar();
    assert(display.width() == 240 && display.height() == 135);

    St7789Graphics graphicsAdapter(display);
    graphicsAdapter.setInk(1, 2);
    graphicsAdapter.line(1, {2, 3}, {10, 12});
    assert(display.calls.back().name == "line");
    assert(display.calls.back().values.back() == 0xF800);

    graphicsAdapter.setRotation(1, 90);
    assert(display.width() == 135 && display.height() == 240);
    assert(graphicsAdapter.windowInfo(1).rotationDegrees == 90);
    bool rejectedRotation = false;
    try {
        graphicsAdapter.setRotation(1, 45);
    } catch (const std::invalid_argument&) {
        rejectedRotation = true;
    }
    assert(rejectedRotation);

    graphicsAdapter.setRotation(1, 0);
    St7789FramePresenter presenter(display, {}, {});
    std::array<graphics::RgbColor, 256> palette{};
    bool suppliedRow = false;
    const auto presented = presenter.presentRowsRegion(
        240, 135, palette, {3, 4, 2, 1},
        [&](std::uint32_t y, std::vector<graphics::ColorIndex>& row) {
            assert(y == 4);
            row = {1, 2};
            suppliedRow = true;
        });
    assert(presented && suppliedRow);
    assert(display.calls.back().name == "end");

    bool rejectedGeometry = false;
    try {
        presenter.presentRows(135, 240, palette,
            [](std::uint32_t, std::vector<graphics::ColorIndex>&) {});
    } catch (const std::invalid_argument&) {
        rejectedGeometry = true;
    }
    assert(rejectedGeometry);
}
