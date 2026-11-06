#pragma once

#include "ql32/graphics/service.hpp"

#include <cstddef>
#include <array>
#include <cstdint>
#include <functional>
#include <limits>
#include <utility>
#include <unordered_map>
#include <vector>

namespace ql32::graphics {

// Five-column bitmap glyph used by retained text canvases.
std::array<std::uint8_t, 5> classicGlyphColumns(char character);

using WindowId = std::uint64_t;
enum class PixelDepth : std::uint8_t { One = 1, Four = 4, Eight = 8 };

struct WindowState {
    WindowId id = 0;
    SessionId owner = 0;
    Rect bounds{}; // effective absolute screen bounds after task-origin translation
    Rect screenClip{};
    PixelDepth pixelDepth = PixelDepth::Four;
    bool visible = true;
};

enum class CanvasResizePhase : std::uint8_t {
    BeforeAllocation,
    BothBuffersLive,
    AfterSwap
};
using CanvasResizeObserver =
    std::function<void(CanvasResizePhase, std::size_t, std::size_t)>;

// Independently allocated retained canvas pixels. Handles remain stable when
// one canvas is resized or released.
class CanvasArena {
public:
    explicit CanvasArena(std::size_t maximumBytes = 0) noexcept
        : maximumBytes_(maximumBytes == 0 ? std::numeric_limits<std::size_t>::max()
                                          : maximumBytes) {}
    CanvasArena(const CanvasArena&) = delete;
    CanvasArena& operator=(const CanvasArena&) = delete;

    std::uint64_t allocate(std::size_t bytes);
    void release(std::uint64_t handle) noexcept;
    void replace(std::uint64_t handle, std::vector<std::uint8_t>&& bytes);
    void setResizeObserver(CanvasResizeObserver observer) {
        resizeObserver_ = std::move(observer);
    }
    void reportResize(CanvasResizePhase phase, std::size_t oldBytes,
                      std::size_t newBytes) const noexcept;
    std::uint8_t* data(std::uint64_t handle) noexcept;
    const std::uint8_t* data(std::uint64_t handle) const noexcept;
    std::size_t size(std::uint64_t handle) const noexcept;

private:
    friend class IndexedCanvas;
    struct Block { std::uint64_t handle; std::vector<std::uint8_t> bytes; };
    Block& find(std::uint64_t handle);
    const Block& find(std::uint64_t handle) const;
    std::size_t maximumBytes_;
    std::uint64_t nextHandle_ = 1;
    std::vector<Block> blocks_;
    CanvasResizeObserver resizeObserver_;
};

// Retained indexed canvas with packed 1-, 4-, or 8-bit pixels.
class IndexedCanvas {
public:
    IndexedCanvas(std::uint32_t width, std::uint32_t height,
                  PixelDepth depth = PixelDepth::Four);
    ~IndexedCanvas();
    IndexedCanvas(const IndexedCanvas&) = delete;
    IndexedCanvas& operator=(const IndexedCanvas&) = delete;
    IndexedCanvas(IndexedCanvas&& other) noexcept;
    IndexedCanvas& operator=(IndexedCanvas&& other) noexcept;

    std::uint32_t width() const noexcept { return width_; }
    std::uint32_t height() const noexcept { return height_; }
    PixelDepth depth() const noexcept { return depth_; }
    std::uint16_t paletteSize() const noexcept;
    ColorIndex ink() const noexcept { return ink_; }
    ColorIndex paper() const noexcept { return paper_; }
    std::size_t storageBytes() const noexcept;

    void setDepth(PixelDepth depth);
    void setInk(ColorIndex color);
    void setPaper(ColorIndex color);
    void clear();
    void scrollUp(std::uint32_t pixelRows);
    void setPixel(std::int32_t x, std::int32_t y, ColorIndex color);
    ColorIndex pixel(std::uint32_t x, std::uint32_t y) const;
    void line(Point from, Point to);
    void rectangle(Point topLeft, std::int32_t width, std::int32_t height,
                   bool filled = false);
    void circle(Point center, std::int32_t radius, bool filled = false);
    void text(Point topLeft, const std::string& value);
    void resize(std::uint32_t width, std::uint32_t height);

private:
    friend class WindowManager;
    ColorIndex readPixel(std::size_t index) const noexcept;
    void writePixel(std::size_t index, ColorIndex color) noexcept;
    std::uint8_t* pixelData() noexcept;
    const std::uint8_t* pixelData() const noexcept;
    IndexedCanvas(std::uint32_t width, std::uint32_t height, PixelDepth depth,
                  CanvasArena* arena);
    std::uint32_t width_;
    std::uint32_t height_;
    PixelDepth depth_;
    ColorIndex ink_ = 7;
    ColorIndex paper_ = 0;
    CanvasArena* arena_ = nullptr;
    std::uint64_t arenaHandle_ = 0;
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
    void setCanvasResizeObserver(CanvasResizeObserver observer) {
        arena_.setResizeObserver(std::move(observer));
    }
    void close(WindowId id);
    void closeSession(SessionId owner);
    WindowState windowState(WindowId id) const;
    void setTaskViewport(SessionId owner, TaskViewport viewport);
    TaskViewport taskViewport(SessionId owner) const;
    void releaseTaskViewportIfUnused(SessionId owner);

    // Bounds passed to create/setBounds are relative to the owning task origin.
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
    void composeRowInto(std::uint32_t y, std::vector<ColorIndex>& row,
                        ColorIndex desktopPaper = 0) const;
    void composeRegionRowInto(std::uint32_t y, std::uint32_t x,
                              std::uint32_t width,
                              std::vector<ColorIndex>& row,
                              ColorIndex desktopPaper = 0) const;

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
        Rect bounds; // task-relative; WindowState exposes translated screen bounds
        bool visible;
        IndexedCanvas canvas;
    };

    Entry& find(WindowId id);
    const Entry& find(WindowId id) const;
    static void validate(Rect bounds);
    static void validate(TaskViewport viewport);
    Rect screenBounds(const Entry& entry) const;
    Rect clipToTask(const Entry& entry, Rect screenBounds) const;

    std::uint32_t screenWidth_;
    std::uint32_t screenHeight_;
    std::size_t maximumWindows_;
    std::size_t maximumCanvasBytes_;
    CanvasArena arena_;
    WindowId nextId_ = 1;
    std::vector<Entry> windows_; // bottom-to-top stacking order
    std::unordered_map<SessionId, TaskViewport> taskViewports_;
};

} // namespace ql32::graphics
