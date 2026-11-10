#pragma once

#include "ql32/graphics/font.hpp"

#include <cstdint>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

namespace ql32::graphics {

using SessionId = std::uint64_t;
using ChannelId = std::uint64_t;
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

// Screen-space viewport shared by all retained windows owned by one task.
// Window bounds are task-relative; composition translates and clips them here.
struct TaskViewport {
    std::int32_t originX = 0;
    std::int32_t originY = 0;
    std::uint32_t canvasWidth = 0;
    std::uint32_t canvasHeight = 0;
    // Display translation applied to task-relative window coordinates inside
    // the fixed screen-space viewport. Positive values move content right/down.
    std::int32_t offsetX = 0;
    std::int32_t offsetY = 0;
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
enum class OverMode : std::uint8_t { Set = 0, Clear = 1, Invert = 2 };

// Platform-neutral drawing contract. An implementation owns pixel windows and
// drawing state per session/channel. A task viewport translates and clips its
// windows during composition. Resources are released from closeSession().
class Service {
public:
    virtual ~Service() = default;

    // Channel-aware defaults preserve adapters that expose one surface per
    // session. Multi-window services override these to keep channel surfaces
    // independent.
    virtual void setInk(SessionId session, ColorIndex color) = 0;
    virtual void setInk(SessionId session, ChannelId channel, ColorIndex color) {
        (void)channel;
        setInk(session, color);
    }
    virtual void setPaper(SessionId session, ColorIndex color) = 0;
    virtual void setPaper(SessionId session, ChannelId channel, ColorIndex color) {
        (void)channel;
        setPaper(session, color);
    }
    virtual void setOverMode(SessionId, OverMode) {}
    virtual void setOverMode(SessionId session, ChannelId channel, OverMode mode) {
        (void)channel;
        setOverMode(session, mode);
    }
    virtual void line(SessionId session, Point from, Point to) = 0;
    virtual void line(SessionId session, ChannelId channel, Point from, Point to) {
        (void)channel;
        line(session, from, to);
    }
    virtual void pixel(SessionId session, Point position) {
        line(session, position, position);
    }
    virtual void pixel(SessionId session, ChannelId channel, Point position) {
        line(session, channel, position, position);
    }
    virtual void rectangle(SessionId session, Point topLeft,
                           std::int32_t width, std::int32_t height) = 0;
    virtual void rectangle(SessionId session, ChannelId channel, Point topLeft,
                           std::int32_t width, std::int32_t height) {
        (void)channel;
        rectangle(session, topLeft, width, height);
    }
    virtual void fillRectangle(SessionId session, Point topLeft,
                               std::int32_t width, std::int32_t height) {
        fillRectangle(session, 1, topLeft, width, height);
    }
    virtual void fillRectangle(SessionId session, ChannelId channel, Point topLeft,
                               std::int32_t width, std::int32_t height) {
        if (width <= 0 || height <= 0)
            throw std::invalid_argument("rectangle dimensions must be positive");
        const auto info = windowInfo(session, channel);
        if (info.width == 0 || info.height == 0) return;
        const auto left = std::max<std::int64_t>(0, topLeft.x);
        const auto right = std::min<std::int64_t>(info.width - 1,
            static_cast<std::int64_t>(topLeft.x) + width - 1);
        const auto top = std::max<std::int64_t>(0, topLeft.y);
        const auto bottom = std::min<std::int64_t>(info.height - 1,
            static_cast<std::int64_t>(topLeft.y) + height - 1);
        for (auto y = top; y <= bottom && left <= right; ++y)
            line(session, channel,
                {static_cast<std::int32_t>(left), static_cast<std::int32_t>(y)},
                {static_cast<std::int32_t>(right), static_cast<std::int32_t>(y)});
    }
    virtual void circle(SessionId session, Point center,
                        std::int32_t radius) = 0;
    virtual void circle(SessionId session, ChannelId channel, Point center,
                        std::int32_t radius) {
        (void)channel;
        circle(session, center, radius);
    }
    virtual void fillCircle(SessionId session, Point center, std::int32_t radius) {
        fillCircle(session, 1, center, radius);
    }
    virtual void fillCircle(SessionId session, ChannelId channel, Point center,
                            std::int32_t radius) {
        if (radius < 0) throw std::invalid_argument("circle radius must be nonnegative");
        const auto info = windowInfo(session, channel);
        if (info.width == 0 || info.height == 0) return;
        const auto top = std::max<std::int64_t>(0,
            static_cast<std::int64_t>(center.y) - radius);
        const auto bottom = std::min<std::int64_t>(info.height - 1,
            static_cast<std::int64_t>(center.y) + radius);
        const auto radiusSquared = static_cast<std::int64_t>(radius) * radius;
        for (auto y = top; y <= bottom; ++y) {
            const auto dy = y - center.y;
            const auto remaining = radiusSquared - dy * dy;
            if (remaining < 0) continue;
            auto span = static_cast<std::int64_t>(std::sqrt(
                static_cast<long double>(remaining)));
            while ((span + 1) * (span + 1) <= remaining) ++span;
            while (span * span > remaining) --span;
            const auto left = std::max<std::int64_t>(0,
                static_cast<std::int64_t>(center.x) - span);
            const auto right = std::min<std::int64_t>(info.width - 1,
                static_cast<std::int64_t>(center.x) + span);
            if (left <= right)
                line(session, channel,
                    {static_cast<std::int32_t>(left), static_cast<std::int32_t>(y)},
                    {static_cast<std::int32_t>(right), static_cast<std::int32_t>(y)});
        }
    }
    virtual void text(SessionId session, Point topLeft,
                      const std::string& value) = 0;
    virtual void text(SessionId session, ChannelId channel, Point topLeft,
                      const std::string& value) {
        (void)channel;
        text(session, topLeft, value);
    }
    virtual void setTextFont(SessionId, ChannelId, const BitmapFont*, FontStyle) {}
    virtual void setGraphicsFont(SessionId, ChannelId, const BitmapFont*, FontStyle) {}
    virtual void clearFontSelections(SessionId, ChannelId) {}
    virtual void writeText(SessionId session, const std::string& value) {
        text(session, {0, 0}, value);
    }
    virtual void writeText(SessionId session, ChannelId channel,
                           const std::string& value) {
        (void)channel;
        writeText(session, value);
    }
    virtual bool setTextPosition(SessionId, ChannelId, std::uint32_t,
                                 std::uint32_t) { return false; }
    virtual void clear(SessionId session) = 0;
    virtual void clear(SessionId session, ChannelId channel) {
        (void)channel;
        clear(session);
    }
    virtual void closeWindow(SessionId) {
        throw std::runtime_error("closing graphics windows unsupported");
    }
    virtual void closeWindow(SessionId session, ChannelId channel) {
        (void)channel;
        closeWindow(session);
    }
    virtual void closeSession(SessionId session) = 0;
    // Discard session-owned graphics state when the task is cancelled or
    // terminated. Services that retain completed output may override this;
    // the default treats close as a full release.
    virtual void discardSession(SessionId session) { closeSession(session); }

    // Update suppression controls the shared physical display. Drawing still
    // updates retained canvases while automatic presentation is disabled.
    virtual void setUpdateMode(SessionId, UpdateMode) {}
    // Force the current composed screen to be presented without changing mode.
    virtual void update(SessionId) {}

    virtual WindowInfo windowInfo(SessionId session) = 0;
    virtual WindowInfo windowInfo(SessionId session, ChannelId channel) {
        (void)channel;
        return windowInfo(session);
    }
    virtual RgbColor paletteColor(SessionId session, std::uint16_t index) = 0;
    virtual RgbColor paletteColor(SessionId session, ChannelId channel,
                                  std::uint16_t index) {
        (void)channel;
        return paletteColor(session, index);
    }
    virtual void setWindowBounds(SessionId, Rect) {
        throw std::runtime_error("window positioning unsupported");
    }
    virtual void setWindowBounds(SessionId session, ChannelId channel, Rect bounds) {
        (void)channel;
        setWindowBounds(session, bounds);
    }
    virtual void setTaskViewport(SessionId, TaskViewport) {
        throw std::runtime_error("task viewport positioning unsupported");
    }
    virtual TaskViewport taskViewport(SessionId) const { return {}; }
    virtual std::pair<std::uint32_t, std::uint32_t> displaySize() const noexcept {
        return {0, 0};
    }
    virtual void setRotation(SessionId, std::uint16_t degrees) {
        throw std::runtime_error("display rotation unsupported");
    }
    // Platform status text is composed above application graphics and UI.
    virtual void setDebugOverlayEnabled(bool) {
        throw std::runtime_error("debug overlay unsupported");
    }
    virtual bool debugOverlayEnabled() const noexcept { return false; }
    virtual void setDebugOverlayText(const std::string&) {}
};

} // namespace ql32::graphics
