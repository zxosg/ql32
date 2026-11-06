#pragma once

#include "ql32/graphics/service.hpp"

#include <Adafruit_ST7789.h>
#include <functional>
#include <utility>

namespace ql32::platform::tenstar_esp32s3 {

class TenstarSt7789Display final : public Adafruit_ST7789 {
public:
    TenstarSt7789Display(SPIClass* spi, std::int8_t dc, std::int8_t cs,
                         std::int8_t reset)
        : Adafruit_ST7789(spi, cs, dc, reset) {}

    void beginTenstar() {
        // The panel is 135x240 in portrait-native orientation. Rotation 1
        // provides QL/32's default 240x135 landscape coordinate space.
        init(135, 240);
        setRotation(1);
    }

    void setRotationDegrees(std::uint16_t degrees) {
        // Rotation zero is the panel's landscape orientation.
        setRotation(static_cast<std::uint8_t>((degrees / 90 + 1) % 4));
    }
};

class St7789Graphics final : public graphics::Service {
public:
    explicit St7789Graphics(TenstarSt7789Display& display) : display_(display) {}

    void setInk(graphics::SessionId, graphics::ColorIndex) override;
    void setPaper(graphics::SessionId, graphics::ColorIndex) override;
    void line(graphics::SessionId, graphics::Point, graphics::Point) override;
    void rectangle(graphics::SessionId, graphics::Point, std::int32_t,
                   std::int32_t) override;
    void circle(graphics::SessionId, graphics::Point, std::int32_t) override;
    void text(graphics::SessionId, graphics::Point, const std::string&) override;
    void clear(graphics::SessionId) override;
    void closeSession(graphics::SessionId) override;
    graphics::WindowInfo windowInfo(graphics::SessionId) override;
    graphics::RgbColor paletteColor(graphics::SessionId, std::uint16_t) override;
    void setRotation(graphics::SessionId, std::uint16_t) override;

private:
    void claim(graphics::SessionId);
    std::uint16_t color(graphics::ColorIndex) const;

    TenstarSt7789Display& display_;
    graphics::SessionId activeSession_ = 0;
    bool hasActiveSession_ = false;
    graphics::ColorIndex ink_ = 7;
    graphics::ColorIndex paper_ = 0;
    std::uint16_t rotationDegrees_ = 0;
};

} // namespace ql32::platform::tenstar_esp32s3
