#include "ql32/platform/esp32_cyd/touch_input.hpp"
#include "ql32/platform/esp32_cyd/touch_transform.hpp"

#include <Arduino.h>

namespace ql32::platform::esp32_cyd {

void Xpt2046Touch::begin() {
    pinMode(clock_, OUTPUT);
    pinMode(mosi_, OUTPUT);
    pinMode(miso_, INPUT);
    pinMode(chipSelect_, OUTPUT);
    pinMode(interrupt_, INPUT);
    digitalWrite(clock_, LOW);
    digitalWrite(mosi_, LOW);
    digitalWrite(chipSelect_, HIGH);
}

std::uint16_t Xpt2046Touch::readAxis(std::uint8_t command) {
    digitalWrite(chipSelect_, LOW);
    for (int bit = 7; bit >= 0; --bit) {
        digitalWrite(clock_, LOW);
        digitalWrite(mosi_, (command >> bit) & 1U);
        delayMicroseconds(1);
        digitalWrite(clock_, HIGH);
        delayMicroseconds(1);
    }
    std::uint16_t sample = 0;
    for (unsigned bit = 0; bit < 16; ++bit) {
        digitalWrite(clock_, LOW);
        delayMicroseconds(1);
        digitalWrite(clock_, HIGH);
        delayMicroseconds(1);
        sample = static_cast<std::uint16_t>((sample << 1) |
                                             (digitalRead(miso_) ? 1 : 0));
    }
    digitalWrite(chipSelect_, HIGH);
    return static_cast<std::uint16_t>((sample >> 3) & 0x0fff);
}

bool Xpt2046Touch::readPoint(graphics::Point& point, std::uint32_t width,
                             std::uint32_t height, std::uint16_t rotationDegrees) {
    if (digitalRead(interrupt_) != LOW) return false;
    // XPT2046 pressure readings are sufficient to reject an idle panel.
    const auto z1 = readAxis(0xb0);
    const auto z2 = readAxis(0xc0);
    if (z1 < 20 || z2 >= 4080 || width == 0 || height == 0) return false;

    // Sample both axes twice and average to reduce visible jitter without
    // allocating buffers or depending on a board-specific graphics library.
    const auto rawX = static_cast<std::uint16_t>(
        (static_cast<unsigned>(readAxis(0xd0)) + readAxis(0xd0)) / 2);
    const auto rawY = static_cast<std::uint16_t>(
        (static_cast<unsigned>(readAxis(0x90)) + readAxis(0x90)) / 2);

    point = mapRawTouchPoint(rawX, rawY, width, height, rotationDegrees);
    return true;
}

} // namespace ql32::platform::esp32_cyd
