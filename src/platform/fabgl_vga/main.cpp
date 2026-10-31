#include <Arduino.h>
#include <driver/gpio.h>
#include <fabgl.h>

#include "capabilities.hpp"
#include "pins.hpp"
#include "ql32/platform/fabgl_vga/graphics.hpp"

namespace {

fabgl::VGAController display;
fabgl::Canvas canvas(&display);
fabgl::PS2Controller ps2;
ql32::platform::fabgl_vga::FabglGraphics graphics(canvas);

} // namespace

void setup() {
    Serial.begin(115200);

    if (!ql32::boards::lilygo_vga32::capabilities().valid()) {
        Serial.println("QL/32 VGA32 capability profile invalid");
        return;
    }

    display.begin(
        static_cast<gpio_num_t>(ql32::boards::lilygo_vga32::vgaRed),
        static_cast<gpio_num_t>(ql32::boards::lilygo_vga32::vgaGreen),
        static_cast<gpio_num_t>(ql32::boards::lilygo_vga32::vgaBlue),
        static_cast<gpio_num_t>(ql32::boards::lilygo_vga32::vgaHSync),
        static_cast<gpio_num_t>(ql32::boards::lilygo_vga32::vgaVSync));
    display.setResolution(VGA_320x200_70Hz);

    ps2.begin(fabgl::PS2Preset::KeyboardPort0);

    constexpr ql32::graphics::SessionId demoSession = 1;
    graphics.setPaper(demoSession, 0);
    graphics.clear(demoSession);
    graphics.setInk(demoSession, 7);
    graphics.text(demoSession, {8, 8}, "QL/32 VGA32 V1.4");
    graphics.text(demoSession, {8, 24}, "FabGL VGA + PS/2 ready");
    graphics.rectangle(demoSession, {8, 48}, 96, 48);
    graphics.line(demoSession, {8, 48}, {103, 95});
    graphics.circle(demoSession, {150, 72}, 24);

    Serial.println("QL/32 VGA32 V1.4 bring-up ready");
}

void loop() {
    delay(1000);
}
