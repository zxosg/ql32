#pragma once

#include <cstdint>
#include <string>

namespace ql32::graphics {

using SessionId = std::uint64_t;
using ColorIndex = std::uint8_t;

struct Point {
    std::int32_t x = 0;
    std::int32_t y = 0;
};

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
    virtual void clear(SessionId session) = 0;
    virtual void closeSession(SessionId session) = 0;
};

} // namespace ql32::graphics
