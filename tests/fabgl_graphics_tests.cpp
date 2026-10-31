#include "ql32/platform/fabgl_vga/graphics.hpp"

#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void testPaletteAndDrawingCommands() {
    fabgl::Canvas canvas;
    ql32::platform::fabgl_vga::FabglGraphics graphics(canvas);
    constexpr ql32::graphics::SessionId session = 12;

    graphics.setPaper(session, 1);
    require(canvas.clearCount == 1 && canvas.brushRed == 0 &&
                canvas.brushGreen == 0 && canvas.brushBlue == 1,
            "paper color did not use QL palette blue or initialize the surface");
    graphics.setInk(session, 2);
    require(canvas.penRed == 1 && canvas.penGreen == 0 && canvas.penBlue == 0,
            "ink color did not use QL palette red");

    graphics.line(session, {-3, 4}, {8, 9});
    require(canvas.lineCount == 1 && canvas.x1 == -3 && canvas.y1 == 4 &&
                canvas.x2 == 8 && canvas.y2 == 9,
            "line coordinates were not forwarded to FabGL");
    graphics.rectangle(session, {-2, 2}, 6, 4);
    require(canvas.rectCount == 1 && canvas.x1 == 0 && canvas.y1 == 2 &&
                canvas.x2 == 3 && canvas.y2 == 5,
            "rectangle was not clipped to the active display bounds");
    graphics.circle(session, {100, 80}, 12);
    require(canvas.ellipseCount == 1 && canvas.x1 == 100 && canvas.y1 == 80 &&
                canvas.widthArg == 24 && canvas.heightArg == 24,
            "circle radius was not translated to FabGL ellipse dimensions");
    graphics.text(session, {7, 11}, "QL/32");
    require(canvas.x1 == 7 && canvas.y1 == 11 && canvas.text == "QL/32",
            "text coordinates or content were not forwarded to FabGL");
    graphics.clear(session);
    require(canvas.clearCount == 2 && canvas.brushBlue == 1,
            "clear did not use the session paper color");
}

void testSessionOwnershipAndRelease() {
    fabgl::Canvas canvas;
    ql32::platform::fabgl_vga::FabglGraphics graphics(canvas);
    graphics.text(1, {0, 0}, "first");
    bool rejected = false;
    try {
        graphics.text(2, {0, 0}, "second");
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "adapter allowed two sessions to draw on the same VGA surface");
    graphics.closeSession(1);
    graphics.text(2, {1, 1}, "next");
    require(canvas.text == "next" && canvas.clearCount == 3,
            "released VGA surface was not claimed and reset by the next session");
}

} // namespace

int main() {
    try {
        testPaletteAndDrawingCommands();
        testSessionOwnershipAndRelease();
    } catch (const std::exception& error) {
        std::cerr << "FabGL graphics adapter test failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
