#pragma once

#include "ql32/graphics/service.hpp"

#include <Adafruit_ILI9341.h>
#include <functional>
#include <utility>

namespace ql32::platform::esp32_cyd {

// ILI9341 adapter that keeps the BASIC graphics contract's indexed QL colors.
class Ili9341Graphics final : public graphics::Service {
public:
    explicit Ili9341Graphics(Adafruit_ILI9341& display) : display_(display) {}
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

private:
    void claim(graphics::SessionId session);
    std::uint16_t color(graphics::ColorIndex index) const;

    Adafruit_ILI9341& display_;
    graphics::SessionId activeSession_ = 0;
    bool hasActiveSession_ = false;
    graphics::ColorIndex ink_ = 7;
    graphics::ColorIndex paper_ = 0;
    std::function<void()> clearText_;
};

} // namespace ql32::platform::esp32_cyd
