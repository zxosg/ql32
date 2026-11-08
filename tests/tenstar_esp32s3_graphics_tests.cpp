#include "ql32/platform/tenstar_esp32s3/frame_presenter.hpp"
#include "ql32/graphics/windowed_service.hpp"

#include <algorithm>
#include <cassert>
#include <memory>
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

    std::weak_ptr<graphics::WindowedService> graphicsRef;
    St7789FramePresenter overlayPresenter(display, {}, [&] {
        const auto service = graphicsRef.lock();
        if (!service) return;
        const auto width = static_cast<std::uint32_t>(display.width());
        const auto height = static_cast<std::uint32_t>(display.height());
        const auto overlayHeight = std::min<std::uint32_t>(40, height);
        service->invalidateSystemRegion({
            0, static_cast<std::int32_t>(height - overlayHeight),
            width, overlayHeight});
    });
    auto service = std::make_shared<graphics::WindowedService>(240, 135,
        std::shared_ptr<graphics::FramePresenter>(&overlayPresenter,
                                                  [](graphics::FramePresenter*) {}));
    graphicsRef = service;
    for (const std::uint16_t degrees : {90, 180, 270, 0, 90, 180}) {
        service->setRotation(1, degrees);
        const auto info = service->windowInfo(1, 1);
        const bool landscape = degrees == 0 || degrees == 180;
        assert(info.width == (landscape ? 240u : 135u));
        assert(info.height == (landscape ? 135u : 240u));
        assert(info.rotationDegrees == degrees);
    }
}
