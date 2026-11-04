#pragma once

#include "ql32/graphics/service.hpp"

#include <Adafruit_ILI9341.h>
#include <functional>
#include <utility>

namespace ql32::platform::esp32_cyd {

// Adafruit's ILI9341 driver uses the controller's portrait-native dimensions
// even though the CYD's glass is mounted landscape at rotation index 0.
// Keep its drawing bounds in the CYD's physical pixel orientation.
class CydIli9341Display final : public Adafruit_ILI9341 {
public:
    using Adafruit_ILI9341::Adafruit_ILI9341;

    void setCydRotation(std::uint8_t rotation) {
        const auto index = static_cast<std::uint8_t>(rotation % 4);
        Adafruit_ILI9341::setRotation(index);

        // Adafruit's ILI9341 driver always sets MADCTL_BGR. The tested CYD
        // panel presents RGB565 red as blue with that flag set, so retain the
        // orientation bits while selecting RGB pixel order.
        static constexpr std::uint8_t madctlByRotation[] = {0x40, 0x20, 0x80, 0xE0};
        auto madctl = madctlByRotation[index];
        sendCommand(0x36, &madctl, 1);

        const bool landscape = index == 0 || index == 2;
        _width = landscape ? 320 : 240;
        _height = landscape ? 240 : 320;
    }

    void setRotationDegrees(std::uint16_t degrees) {
        setCydRotation(static_cast<std::uint8_t>(degrees / 90));
    }

};

// ILI9341 adapter that keeps the BASIC graphics contract's indexed QL colors.
class Ili9341Graphics final : public graphics::Service {
public:
    explicit Ili9341Graphics(CydIli9341Display& display) : display_(display) {}
    void setTextClearCallback(std::function<void()> callback) {
        clearText_ = std::move(callback);
    }

    void setInk(graphics::SessionId session, graphics::ColorIndex color) override;
    void setPaper(graphics::SessionId session, graphics::ColorIndex color) override;
    void line(graphics::SessionId session, graphics::Point from,
              graphics::Point to) override;
    void rectangle(graphics::SessionId session, graphics::Point topLeft,
                   std::int32_t width, std::int32_t height) override;
    void circle(graphics::SessionId session, graphics::Point center,
                std::int32_t radius) override;
    void text(graphics::SessionId session, graphics::Point topLeft,
              const std::string& value) override;
    void clear(graphics::SessionId session) override;
    void closeSession(graphics::SessionId session) override;
    graphics::WindowInfo windowInfo(graphics::SessionId session) override;
    graphics::RgbColor paletteColor(graphics::SessionId session, std::uint16_t index) override;
    void setRotation(graphics::SessionId session, std::uint16_t degrees) override;

private:
    void claim(graphics::SessionId session);
    std::uint16_t color(graphics::ColorIndex index) const;

    CydIli9341Display& display_;
    graphics::SessionId activeSession_ = 0;
    bool hasActiveSession_ = false;
    graphics::ColorIndex ink_ = 7;
    graphics::ColorIndex paper_ = 0;
    std::uint16_t rotationDegrees_ = 0;
    std::function<void()> clearText_;
};

} // namespace ql32::platform::esp32_cyd
