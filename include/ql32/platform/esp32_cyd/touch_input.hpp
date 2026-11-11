#pragma once

#include "ql32/graphics/service.hpp"

namespace ql32::platform::esp32_cyd {

// Polled XPT2046 resistive touch controller used by the ESP32-2432S028 CYD.
class Xpt2046Touch {
public:
    Xpt2046Touch(int clock, int miso, int mosi, int chipSelect, int interrupt)
        : clock_(clock), miso_(miso), mosi_(mosi), chipSelect_(chipSelect),
          interrupt_(interrupt) {}
    void begin();
    bool readPoint(graphics::Point& point, std::uint32_t width,
                   std::uint32_t height, std::uint16_t rotationDegrees);

private:
    std::uint16_t readAxis(std::uint8_t command);
    int clock_;
    int miso_;
    int mosi_;
    int chipSelect_;
    int interrupt_;
};

} // namespace ql32::platform::esp32_cyd
