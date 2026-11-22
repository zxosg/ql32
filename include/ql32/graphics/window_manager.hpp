#pragma once

#include "ql32/graphics/service.hpp"
#include "ql32/graphics/font.hpp"

#include <cstddef>
#include <array>
#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <utility>
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

struct HitTarget {
    WindowId window = 0;
    SessionId owner = 0;
    Point local{};
};

enum class CanvasResizePhase : std::uint8_t {
    BeforeAllocation,
    BothBuffersLive,
    AfterSwap
};
using CanvasResizeObserver =
    std::function<void(CanvasResizePhase, std::size_t, std::size_t)>;

// Optional low-level raster hooks for a platform adapter. The view describes
// the canvas's existing packed storage; implementations must return false
// without changing it when an operation is unsupported.
struct CanvasBufferView {
    std::uint8_t* pixels = nullptr;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    // Byte stride between rows, or zero when this canvas packs rows across a
    // byte boundary and cannot be represented as a row-aligned view.
    std::uint32_t stride = 0;
    PixelDepth depth = PixelDepth::Four;
    ColorIndex ink = 0;
    ColorIndex paper = 0;
    OverMode overMode = OverMode::Set;
};

class CanvasRasterOps {
public:
    virtual ~CanvasRasterOps() = default;
    virtual bool clear(CanvasBufferView) const noexcept { return false; }
    virtual bool setPixel(CanvasBufferView, std::int32_t, std::int32_t,
                          ColorIndex) const noexcept { return false; }
    virtual bool plotPixel(CanvasBufferView, std::int32_t,
                           std::int32_t) const noexcept { return false; }
    virtual bool line(CanvasBufferView, Point, Point) const noexcept { return false; }
    virtual bool fillRectangle(CanvasBufferView, Point, std::int32_t,
                               std::int32_t) const noexcept { return false; }
    virtual bool circle(CanvasBufferView, Point, std::int32_t,
                        bool) const noexcept { return false; }
    virtual bool scrollUp(CanvasBufferView, std::uint32_t) const noexcept {
        return false;
    }
};

// Independently allocated retained canvas pixels. Handles remain stable when
// one canvas is resized or released.
class CanvasArena {
public:
    CanvasArena() = default;
    CanvasArena(const CanvasArena&) = delete;
    CanvasArena& operator=(const CanvasArena&) = delete;

    std::uint64_t allocate(std::size_t bytes);
    void release(std::uint64_t handle) noexcept;
    void replace(std::uint64_t handle, std::vector<std::uint8_t>&& bytes);
    std::size_t capacity(std::uint64_t handle) const noexcept;
    // Resizes within the existing allocation; never reallocates.
    bool resizeInPlace(std::uint64_t handle, std::size_t bytes) noexcept;
    // Returns spare capacity to the allocator when it is worth the transient copy.
    void trim(std::uint64_t handle) noexcept;
    // Frees the block and allocates a zeroed one; contents are lost. On
    // failure the previous size is restored (zeroed) and the error rethrown.
    void reallocateDiscard(std::uint64_t handle, std::size_t bytes);
    // Platform probe returning the largest free allocation in bytes.
    void setMemoryProbe(std::function<std::size_t()> probe) {
        memoryProbe_ = std::move(probe);
    }
    bool lowMemoryFor(std::size_t bytes) const;
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
    std::uint64_t nextHandle_ = 1;
    std::list<Block> blocks_;
    CanvasResizeObserver resizeObserver_;
    std::function<std::size_t()> memoryProbe_;
};

// Retained indexed canvas with packed 1-, 4-, or 8-bit pixels.
class IndexedCanvas {
public:
    IndexedCanvas(std::uint32_t width, std::uint32_t height,
                  PixelDepth depth = PixelDepth::Four,
                  // Non-owning; the operations object must outlive this canvas.
                  const CanvasRasterOps* rasterOps = nullptr);
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
    OverMode overMode() const noexcept { return overMode_; }
    std::size_t storageBytes() const noexcept;
    CanvasBufferView bufferView() noexcept;

    void setDepth(PixelDepth depth);
    void setInk(ColorIndex color);
    void setPaper(ColorIndex color);
    void setOverMode(OverMode mode) noexcept { overMode_ = mode; }
    void clear();
    void scrollUp(std::uint32_t pixelRows);
    void setPixel(std::int32_t x, std::int32_t y, ColorIndex color);
    void plotPixel(std::int32_t x, std::int32_t y);
    ColorIndex pixel(std::uint32_t x, std::uint32_t y) const;
    void line(Point from, Point to);
    void rectangle(Point topLeft, std::int32_t width, std::int32_t height,
                   bool filled = false);
    void circle(Point center, std::int32_t radius, bool filled = false);
    Rect text(Point topLeft, const std::string& value);
    Rect text(Point topLeft, const std::string& value, const BitmapFont& font,
              FontStyle style = FontStyle::Normal);
    Rect printText(Point topLeft, const std::string& value);
    Rect printText(Point topLeft, const std::string& value, const BitmapFont& font,
                   FontStyle style = FontStyle::Normal);
    void resize(std::uint32_t width, std::uint32_t height);

private:
    friend class WindowManager;
    ColorIndex readPixel(std::size_t index) const noexcept;
    void writePixel(std::size_t index, ColorIndex color) noexcept;
    void copyPixelsTo(std::uint32_t y, std::uint32_t x, std::uint32_t width,
                      ColorIndex* destination) const noexcept;
    void plotSpan(std::size_t firstPixel, std::size_t pixelCount);
    Rect renderText(Point topLeft, const std::string& value, bool fillBackground,
                    bool useOverMode, const BitmapFont* font, FontStyle style);
    std::uint8_t* pixelData() noexcept;
    const std::uint8_t* pixelData() const noexcept;
    IndexedCanvas(std::uint32_t width, std::uint32_t height, PixelDepth depth,
                  CanvasArena* arena, const CanvasRasterOps* rasterOps);
    std::uint32_t width_;
    std::uint32_t height_;
    PixelDepth depth_;
    ColorIndex ink_ = 7;
    ColorIndex paper_ = 0;
    OverMode overMode_ = OverMode::Set;
    CanvasArena* arena_ = nullptr;
    const CanvasRasterOps* rasterOps_ = nullptr;
    std::uint64_t arenaHandle_ = 0;
    // CanvasArena handles stay stable, but resolving one traverses its block
    // list. Cache the current allocation pointer on this hot pixel-access path.
    std::uint8_t* arenaPixels_ = nullptr;
    std::vector<ColorIndex> pixels_;
};

// Portable retained-canvas manager. Platform presenters consume composed
// frames after changes, restoring content that was previously covered.
class WindowManager {
public:
    WindowManager(std::uint32_t screenWidth, std::uint32_t screenHeight,
                  std::shared_ptr<const CanvasRasterOps> rasterOps = {});

    WindowId create(SessionId owner, Rect bounds,
                    PixelDepth depth = PixelDepth::Four);
    void setCanvasMemoryProbe(std::function<std::size_t()> probe) {
        arena_.setMemoryProbe(std::move(probe));
    }
    void setCanvasResizeObserver(CanvasResizeObserver observer) {
        arena_.setResizeObserver(std::move(observer));
    }
    void close(WindowId id);
    void closeSession(SessionId owner);
    WindowState windowState(WindowId id) const;
    std::optional<HitTarget> hitTest(Point screenPoint) const;
    void setTaskViewport(SessionId owner, TaskViewport viewport);
    TaskViewport taskViewport(SessionId owner) const;
    void releaseTaskViewportIfUnused(SessionId owner);

    // Bounds passed to create/setBounds are relative to the owning task origin.
    void setBounds(WindowId id, Rect bounds);
    void setPixelDepth(WindowId id, PixelDepth depth);
    void setScreenSize(std::uint32_t width, std::uint32_t height);
    // Drops all canvas data while retaining per-task viewport assignments.
    void clearWindows() noexcept;
    void setVisible(WindowId id, bool visible);
    void raise(WindowId id);
    bool raiseSession(SessionId owner);
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
    Rect screenBounds(const Entry& entry, const TaskViewport& viewport) const;
    Rect clipToViewport(const TaskViewport& viewport, Rect screenBounds) const;

    std::uint32_t screenWidth_;
    std::uint32_t screenHeight_;
    CanvasArena arena_;
    std::shared_ptr<const CanvasRasterOps> rasterOps_;
    WindowId nextId_ = 1;
    std::list<Entry> windows_; // bottom-to-top; each entry frees with its owner
    std::map<SessionId, TaskViewport> taskViewports_;
};

} // namespace ql32::graphics
