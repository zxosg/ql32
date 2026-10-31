#include "ql32/platform/esp32_cyd/graphics.hpp"

#include <stdexcept>

namespace {

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

void testIndexedPaletteAndDrawing() {
    Adafruit_ILI9341 display;
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
    Adafruit_ILI9341 display;
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

} // namespace

int main() {
    testIndexedPaletteAndDrawing();
    testOwnershipAndValidation();
    return 0;
}
