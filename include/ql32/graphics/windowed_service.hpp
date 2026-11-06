#pragma once

#include "ql32/graphics/frame_presenter.hpp"
#include "ql32/graphics/service.hpp"
#include "ql32/graphics/window_manager.hpp"

#include <memory>
#include <cstdint>
#include <cstddef>
#include <functional>
#include <unordered_map>
#include <utility>

namespace ql32::graphics {

// BASIC graphics service backed by retained canvases and a portable compositor.
class WindowedService final : public Service {
public:
    WindowedService(std::uint32_t screenWidth, std::uint32_t screenHeight,
                    std::shared_ptr<FramePresenter> presenter,
                    std::size_t maximumWindows = 4,
                    std::size_t maximumCanvasBytes = 0);

    void setInk(SessionId session, ColorIndex color) override;
    void setInk(SessionId session, ChannelId channel, ColorIndex color) override;
    void setPaper(SessionId session, ColorIndex color) override;
    void setPaper(SessionId session, ChannelId channel, ColorIndex color) override;
    void line(SessionId session, Point from, Point to) override;
    void line(SessionId session, ChannelId channel, Point from, Point to) override;
    void pixel(SessionId session, Point position) override;
    void pixel(SessionId session, ChannelId channel, Point position) override;
    void rectangle(SessionId session, Point topLeft,
                   std::int32_t width, std::int32_t height) override;
    void rectangle(SessionId session, ChannelId channel, Point topLeft,
                   std::int32_t width, std::int32_t height) override;
    void fillRectangle(SessionId session, Point topLeft,
                       std::int32_t width, std::int32_t height) override;
    void fillRectangle(SessionId session, ChannelId channel, Point topLeft,
                       std::int32_t width, std::int32_t height) override;
    void circle(SessionId session, Point center, std::int32_t radius) override;
    void circle(SessionId session, ChannelId channel, Point center,
                std::int32_t radius) override;
    void fillCircle(SessionId session, Point center, std::int32_t radius) override;
    void fillCircle(SessionId session, ChannelId channel, Point center,
                    std::int32_t radius) override;
    void text(SessionId session, Point topLeft, const std::string& value) override;
    void text(SessionId session, ChannelId channel, Point topLeft,
              const std::string& value) override;
    void writeText(SessionId session, const std::string& value) override;
    void clear(SessionId session) override;
    void clear(SessionId session, ChannelId channel) override;
    void closeWindow(SessionId session) override;
    void closeWindow(SessionId session, ChannelId channel) override;
    void closeSession(SessionId session) override;
    void setUpdateMode(SessionId session, UpdateMode mode) override;
    void update(SessionId session) override;
    WindowInfo windowInfo(SessionId session) override;
    WindowInfo windowInfo(SessionId session, ChannelId channel) override;
    RgbColor paletteColor(SessionId session, std::uint16_t index) override;
    RgbColor paletteColor(SessionId session, ChannelId channel,
                          std::uint16_t index) override;
    void setWindowBounds(SessionId session, Rect bounds) override;
    void setWindowBounds(SessionId session, ChannelId channel, Rect bounds) override;
    void setTaskViewport(SessionId session, TaskViewport viewport) override;
    TaskViewport taskViewport(SessionId session) const override;
    void setRotation(SessionId session, std::uint16_t degrees) override;
    void setDebugOverlayEnabled(bool enabled) override;
    bool debugOverlayEnabled() const noexcept override { return debugOverlayEnabled_; }
    void setDebugOverlayText(const std::string& text) override;

    WindowId windowForSession(SessionId session);
    void setDefaultWindowBounds(ChannelId channel, Rect bounds);
    void setConsoleBoundsCallback(std::function<void(Rect)> callback) {
        consoleBoundsCallback_ = std::move(callback);
    }
    void setConsoleColorsCallback(
        std::function<void(ColorIndex, ColorIndex)> callback) {
        consoleColorsCallback_ = std::move(callback);
    }
    void setConsoleClearCallback(std::function<void()> callback) {
        consoleClearCallback_ = std::move(callback);
    }
    void setWindowPixelDepth(SessionId session, PixelDepth depth);
    void setWindowVisible(SessionId session, bool visible);
    void raiseWindow(SessionId session);
    void tileWindows();
    void present();
    using RowLayer = std::function<void(std::uint32_t, std::uint32_t,
                                        std::vector<ColorIndex>&)>;
    void setConsoleLayer(RowLayer layer) { consoleLayer_ = std::move(layer); }
    void setUiLayer(RowLayer layer) { uiLayer_ = std::move(layer); }
    void invalidateSystemRegion(Rect region);
    void composeDisplayRow(std::uint32_t y, std::vector<ColorIndex>& row) const;
    WindowManager& windowManager() noexcept { return manager_; }
    void setCanvasResizeObserver(CanvasResizeObserver observer) {
        manager_.setCanvasResizeObserver(std::move(observer));
    }
    static RgbColor defaultPaletteColor(std::uint16_t index);

private:
    struct SurfaceKey {
        SessionId session = 0;
        ChannelId channel = 1;
        bool operator==(const SurfaceKey& other) const noexcept {
            return session == other.session && channel == other.channel;
        }
    };
    struct SurfaceKeyHash {
        std::size_t operator()(const SurfaceKey& key) const noexcept;
    };

    WindowId ensureWindow(SessionId session, ChannelId channel = 1);
    IndexedCanvas& canvas(SessionId session, ChannelId channel = 1);
    void discardFrozenWindows();
    Rect screenRegion(SessionId session, ChannelId channel, std::int64_t x,
                      std::int64_t y,
                      std::int64_t width, std::int64_t height);
    void discardPresentationBuffer();
    void presentRegion(Rect region);
    void composeDisplayRegionRow(std::uint32_t y, std::uint32_t x,
                                 std::uint32_t width,
                                 std::vector<ColorIndex>& row) const;
    void presentIfConfigured();

    WindowManager manager_;
    std::shared_ptr<FramePresenter> presenter_;
    std::unordered_map<SurfaceKey, WindowId, SurfaceKeyHash> sessionWindows_;
    std::vector<WindowId> frozenWindows_;
    std::unordered_map<SurfaceKey, Point, SurfaceKeyHash> textCursors_;
    std::unordered_map<ChannelId, Rect> defaultWindowBounds_;
    std::function<void(Rect)> consoleBoundsCallback_;
    std::function<void(ColorIndex, ColorIndex)> consoleColorsCallback_;
    std::function<void()> consoleClearCallback_;
    Frame frame_;
    std::uint16_t rotationDegrees_ = 0;
    UpdateMode updateMode_ = UpdateMode::Auto;
    RowLayer consoleLayer_;
    RowLayer uiLayer_;
    bool debugOverlayEnabled_ = false;
    std::string debugOverlayText_;
};

} // namespace ql32::graphics
