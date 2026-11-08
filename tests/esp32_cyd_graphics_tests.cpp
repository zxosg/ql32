#include "ql32/platform/esp32_cyd/graphics.hpp"
#include "ql32/platform/esp32_cyd/frame_presenter.hpp"
#include "ql32/graphics/windowed_service.hpp"

#include <algorithm>
#include <array>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

void testIndexedPaletteAndDrawing() {
    ql32::platform::esp32_cyd::CydIli9341Display display;
    ql32::platform::esp32_cyd::Ili9341Graphics graphics(display);
    graphics.setPaper(1, 0);
    graphics.clear(1);
    graphics.setInk(1, 2);
    graphics.line(1, {1, 2}, {3, 4});
    graphics.rectangle(1, {-2, 10}, 8, 5);
    graphics.circle(1, {20, 30}, 5);
    graphics.text(1, {6, 7}, "QL32");
    require(display.calls.size() >= 8, "expected TFT drawing operations");
    require(display.calls[0].name == "fill" && display.calls[0].values[0] == 0,
            "initial paper color was not QL black");
    require(display.calls[2].name == "line" && display.calls[2].values[4] == 0xF800,
            "QL red ink did not map to RGB565 red");
    require(display.calls[3].name == "rect" && display.calls[3].values[0] == 0 &&
                display.calls[3].values[2] == 6,
            "rectangle was not clipped to display bounds");
    require(display.calls.back().name == "text" && display.calls.back().text == "QL32",
            "TFT text output mismatch");
}

void testOwnershipAndValidation() {
    ql32::platform::esp32_cyd::CydIli9341Display display;
    ql32::platform::esp32_cyd::Ili9341Graphics graphics(display);
    graphics.setInk(1, 7);
    try {
        graphics.clear(2);
        throw std::runtime_error("graphics allowed a second session");
    } catch (const std::runtime_error& error) {
        require(std::string(error.what()).find("second session") == std::string::npos,
                "graphics session ownership failed");
    }
    try {
        graphics.setInk(1, 8);
        throw std::runtime_error("graphics accepted invalid palette index");
    } catch (const std::out_of_range&) {
    }
    try {
        graphics.line(1, {40000, 0}, {0, 0});
        throw std::runtime_error("graphics accepted an unrepresentable coordinate");
    } catch (const std::out_of_range&) {
    }
    graphics.closeSession(1);
    graphics.clear(2);
}

void testRotationAndWindowQueries() {
    ql32::platform::esp32_cyd::CydIli9341Display display;
    ql32::platform::esp32_cyd::Ili9341Graphics graphics(display);
    graphics.setRotation(3, 0);
    auto info = graphics.windowInfo(3);
    require(display.calls[display.calls.size() - 3].name == "rotation" &&
                display.calls[display.calls.size() - 3].values[0] == 0,
            "zero degrees did not select the CYD's landscape rotation index");
    require(display.calls[display.calls.size() - 2].name == "command" &&
                display.calls[display.calls.size() - 2].values[0] == 0x36 &&
                display.calls[display.calls.size() - 2].values[2] == 0x40,
            "CYD rotation 0 did not select RGB pixel order");
    require(info.width == 320 && info.height == 240 && info.colorDepth == 16 &&
                info.paletteSize == 8 && info.rotationDegrees == 0,
            "CYD landscape window metadata mismatch at zero degrees");
    graphics.rectangle(3, {310, 230}, 20, 20);
    const auto& clipped = display.calls.back();
    require(clipped.name == "rect" && clipped.values[0] == 310 &&
                clipped.values[1] == 230 && clipped.values[2] == 10 &&
                clipped.values[3] == 10,
            "landscape drawing bounds did not use the 320x240 CYD geometry");
    graphics.setRotation(3, 90);
    info = graphics.windowInfo(3);
    require(display.calls[display.calls.size() - 3].values[0] == 1 && info.width == 240 &&
                info.height == 320 && info.rotationDegrees == 90 &&
                (display.calls[display.calls.size() - 2].values[2] & 0x08) == 0,
            "quarter-turn rotation metadata mismatch");
    graphics.setRotation(3, 180);
    info = graphics.windowInfo(3);
    require(display.calls[display.calls.size() - 3].values[0] == 2 && info.width == 320 &&
                info.height == 240 && info.rotationDegrees == 180 &&
                (display.calls[display.calls.size() - 2].values[2] & 0x08) == 0,
            "half-turn landscape metadata mismatch");
    graphics.setRotation(3, 270);
    info = graphics.windowInfo(3);
    require(display.calls[display.calls.size() - 3].values[0] == 3 && info.width == 240 &&
                info.height == 320 && info.rotationDegrees == 270 &&
                (display.calls[display.calls.size() - 2].values[2] & 0x08) == 0,
            "three-quarter-turn portrait metadata mismatch");
    const auto red = graphics.paletteColor(3, 2);
    require(red.red == 255 && red.green == 0 && red.blue == 0,
            "palette query did not return QL red");
    try {
        graphics.setRotation(3, 45);
        throw std::runtime_error("unsupported rotation was accepted");
    } catch (const std::invalid_argument&) {
    }
}

void testFramePresenterStreamsAddressedRows() {
    using namespace ql32::graphics;
    using namespace ql32::platform::esp32_cyd;
    CydIli9341Display display;
    Ili9341FramePresenter presenter(display, {}, {});
    std::array<RgbColor, 256> palette{};
    palette[1] = {255, 0, 0};
    palette[2] = {0, 255, 0};
    const Rect region{10, 20, 2, 2};
    const auto rowProvider = [](std::uint32_t y, std::vector<ColorIndex>& row) {
        row[0] = y == 20 ? 1 : 2;
        row[1] = 2;
    };

    require(presenter.presentRowsRegion(320, 240, palette, region, rowProvider),
            "CYD frame presenter declined row streaming");
    require(display.calls.size() == 5 && display.calls[0].name == "start_write" &&
                display.calls[1].name == "addr_window" &&
                display.calls[1].values == std::vector<std::int32_t>({10, 20, 2, 2}) &&
                display.calls[2].name == "write_pixels" &&
                display.calls[2].values[0] == 2 &&
                display.calls[3].name == "write_pixels" &&
                display.calls[3].values[0] == 2 &&
                display.calls[4].name == "end_write",
            "CYD rows were not sent through one address window and SPI transaction");
}

void testRotationReconfiguresOverlayAfterGeometryUpdate() {
    using namespace ql32::graphics;
    using namespace ql32::platform::esp32_cyd;
    CydIli9341Display display;
    std::weak_ptr<WindowedService> graphicsRef;
    Ili9341FramePresenter presenter(display, {}, [&] {
        const auto graphics = graphicsRef.lock();
        if (!graphics) return;
        const auto width = static_cast<std::uint32_t>(display.width());
        const auto height = static_cast<std::uint32_t>(display.height());
        const auto overlayHeight = std::min<std::uint32_t>(64, height);
        graphics->invalidateSystemRegion(
            {0, static_cast<std::int32_t>(height - overlayHeight), width, overlayHeight});
    });
    auto graphics = std::make_shared<WindowedService>(320, 240,
        std::shared_ptr<FramePresenter>(&presenter, [](FramePresenter*) {}));
    graphicsRef = graphics;

    for (const std::uint16_t degrees : {90, 180, 270, 0, 90, 180}) {
        graphics->setRotation(1, degrees);
        const auto info = graphics->windowInfo(1, 1);
        const bool landscape = degrees == 0 || degrees == 180;
        require(info.width == (landscape ? 320u : 240u) &&
                    info.height == (landscape ? 240u : 320u) &&
                    info.rotationDegrees == degrees,
                "CYD rotation did not synchronize display and compositor geometry");
    }
}

} // namespace

int main() {
    testIndexedPaletteAndDrawing();
    testOwnershipAndValidation();
    testRotationAndWindowQueries();
    testFramePresenterStreamsAddressedRows();
    testRotationReconfiguresOverlayAfterGeometryUpdate();
    return 0;
}
