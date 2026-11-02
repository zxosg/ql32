#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>

namespace ql32::graphics {

using SessionId = std::uint64_t;
using ColorIndex = std::uint8_t;

struct Point {
    std::int32_t x = 0;
    std::int32_t y = 0;
};

struct Rect {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

struct WindowInfo {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint8_t colorDepth = 0;
    std::uint16_t paletteSize = 0;
    std::uint16_t rotationDegrees = 0;
    std::int32_t originX = 0;
    std::int32_t originY = 0;
};

struct RgbColor {
    std::uint8_t red = 0;
    std::uint8_t green = 0;
    std::uint8_t blue = 0;
};

enum class UpdateMode : std::uint8_t { Auto, On, Off };

// Platform-neutral drawing contract. An implementation owns a default pixel
// window and drawing state for each session. It creates the window on first
// use, clips drawing to its bounds, and releases it from closeSession().
class Service {
public:
    virtual ~Service() = default;

    virtual void setInk(SessionId session, ColorIndex color) = 0;
    virtual void setPaper(SessionId session, ColorIndex color) = 0;
    virtual void line(SessionId session, Point from, Point to) = 0;
    virtual void rectangle(SessionId session, Point topLeft,
                           std::int32_t width, std::int32_t height) = 0;
    virtual void circle(SessionId session, Point center,
                        std::int32_t radius) = 0;
    virtual void text(SessionId session, Point topLeft,
                      const std::string& value) = 0;
    virtual void writeText(SessionId session, const std::string& value) {
        text(session, {0, 0}, value);
    }
    virtual void clear(SessionId session) = 0;
    virtual void closeWindow(SessionId) {
        throw std::runtime_error("closing graphics windows unsupported");
    }
    virtual void closeSession(SessionId session) = 0;

    // Update suppression controls the shared physical display. Drawing still
    // updates retained canvases while automatic presentation is disabled.
    virtual void setUpdateMode(SessionId, UpdateMode) {}
    // Force the current composed screen to be presented without changing mode.
    virtual void update(SessionId) {}

    virtual WindowInfo windowInfo(SessionId session) = 0;
    virtual RgbColor paletteColor(SessionId session, std::uint16_t index) = 0;
    virtual void setWindowBounds(SessionId, Rect) {
        throw std::runtime_error("window positioning unsupported");
    }
    virtual void setRotation(SessionId, std::uint16_t degrees) {
        throw std::runtime_error("display rotation unsupported");
    }
};

} // namespace ql32::graphics
