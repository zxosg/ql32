#pragma once

#include "ql32/graphics/service.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ql32::graphics {

using WindowId = std::uint64_t;
enum class PixelDepth : std::uint8_t { One = 1, Four = 4, Eight = 8 };

struct WindowState {
    WindowId id = 0;
    SessionId owner = 0;
    Rect bounds{};
    Rect screenClip{};
    PixelDepth pixelDepth = PixelDepth::Four;
    bool visible = true;
};

// Retained indexed canvas with packed 1-, 4-, or 8-bit pixels.
class IndexedCanvas {
public:
    IndexedCanvas(std::uint32_t width, std::uint32_t height,
                  PixelDepth depth = PixelDepth::Four);

    std::uint32_t width() const noexcept { return width_; }
    std::uint32_t height() const noexcept { return height_; }
    PixelDepth depth() const noexcept { return depth_; }
    std::uint16_t paletteSize() const noexcept;
    ColorIndex ink() const noexcept { return ink_; }
    ColorIndex paper() const noexcept { return paper_; }
    std::size_t storageBytes() const noexcept { return pixels_.size(); }

    void setDepth(PixelDepth depth);
    void setInk(ColorIndex color);
    void setPaper(ColorIndex color);
    void clear();
    void scrollUp(std::uint32_t pixelRows);
    void setPixel(std::int32_t x, std::int32_t y, ColorIndex color);
    ColorIndex pixel(std::uint32_t x, std::uint32_t y) const;
    void line(Point from, Point to);
    void rectangle(Point topLeft, std::int32_t width, std::int32_t height);
    void circle(Point center, std::int32_t radius);
    void text(Point topLeft, const std::string& value);
    void resize(std::uint32_t width, std::uint32_t height);

private:
    friend class WindowManager;
    ColorIndex readPixel(std::size_t index) const noexcept;
    void writePixel(std::size_t index, ColorIndex color) noexcept;
    std::uint32_t width_;
    std::uint32_t height_;
    PixelDepth depth_;
    ColorIndex ink_ = 7;
    ColorIndex paper_ = 0;
    std::vector<ColorIndex> pixels_;
};

// Portable retained-canvas manager. Platform presenters consume composed
// frames after changes, restoring content that was previously covered.
class WindowManager {
public:
    WindowManager(std::uint32_t screenWidth, std::uint32_t screenHeight,
                  std::size_t maximumWindows,
                  std::size_t maximumCanvasBytes = 0);

    WindowId create(SessionId owner, Rect bounds,
                    PixelDepth depth = PixelDepth::Four);
    void close(WindowId id);
    void closeSession(SessionId owner);
    WindowState windowState(WindowId id) const;

    void setBounds(WindowId id, Rect bounds);
    void setPixelDepth(WindowId id, PixelDepth depth);
    void setScreenSize(std::uint32_t width, std::uint32_t height);
    void setVisible(WindowId id, bool visible);
    void raise(WindowId id);
    void tile();

    IndexedCanvas& canvas(WindowId id);
    const IndexedCanvas& canvas(WindowId id) const;
    // Returns a row-major 8-bit palette-index frame, composited bottom-to-top.
    std::vector<ColorIndex> compose(ColorIndex desktopPaper = 0) const;
    void composeInto(std::vector<ColorIndex>& frame, ColorIndex desktopPaper = 0) const;

    // Snapshot is ordered from back to front; invisible and fully offscreen
    // windows are omitted. screenClip is the visible part in physical pixels.
    std::vector<WindowState> compositionOrder() const;
    std::size_t size() const noexcept { return windows_.size(); }
    std::uint32_t screenWidth() const noexcept { return screenWidth_; }
    std::uint32_t screenHeight() const noexcept { return screenHeight_; }

private:
    struct Entry {
        WindowId id;
        SessionId owner;
        Rect bounds;
        bool visible;
        IndexedCanvas canvas;
    };

    Entry& find(WindowId id);
    const Entry& find(WindowId id) const;
    static void validate(Rect bounds);
    Rect clip(Rect bounds) const;

    std::uint32_t screenWidth_;
    std::uint32_t screenHeight_;
    std::size_t maximumWindows_;
    std::size_t maximumCanvasBytes_;
    WindowId nextId_ = 1;
    std::vector<Entry> windows_; // bottom-to-top stacking order
};

} // namespace ql32::graphics
