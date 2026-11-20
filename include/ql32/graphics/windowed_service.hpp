#pragma once

#include "ql32/graphics/frame_presenter.hpp"
#include "ql32/graphics/service.hpp"
#include "ql32/graphics/window_manager.hpp"

#include <memory>
#include <optional>
#include <cstdint>
#include <cstddef>
#include <array>
#include <functional>
#include <list>
#include <map>
#include <unordered_map>
#include <utility>

namespace ql32::graphics {

// BASIC graphics service backed by retained canvases and a portable compositor.
class WindowedService final : public Service {
public:
    WindowedService(std::uint32_t screenWidth, std::uint32_t screenHeight,
                    std::shared_ptr<FramePresenter> presenter,
                    std::shared_ptr<const CanvasRasterOps> rasterOps = {});

    void setInk(SessionId session, ColorIndex color) override;
    void setInk(SessionId session, ChannelId channel, ColorIndex color) override;
    void setPaper(SessionId session, ColorIndex color) override;
    void setPaper(SessionId session, ChannelId channel, ColorIndex color) override;
    void setOverMode(SessionId session, OverMode mode) override;
    void setOverMode(SessionId session, ChannelId channel, OverMode mode) override;
    void line(SessionId session, Point from, Point to) override;
    void line(SessionId session, ChannelId channel, Point from, Point to) override;
    void setGraphicsPosition(SessionId session, ChannelId channel,
                             Point position) override;
    Point graphicsPosition(SessionId session, ChannelId channel) const override;
    void pixel(SessionId session, Point position) override;
    void pixel(SessionId session, ChannelId channel, Point position) override;
    ColorIndex inkColor(SessionId session, ChannelId channel) override;
    void drawIndexedRow(SessionId session, ChannelId channel, Point topLeft,
                        const std::uint8_t* pixels, std::size_t count,
                        std::optional<ColorIndex> transparentIndex = {}) override;
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
    void setTextFont(SessionId session, ChannelId channel,
                     const BitmapFont* font, FontStyle style) override;
    void setGraphicsFont(SessionId session, ChannelId channel,
                         const BitmapFont* font, FontStyle style) override;
    void resetTextFont(SessionId session, ChannelId channel) override;
    void resetGraphicsFont(SessionId session, ChannelId channel) override;
    void clearFontSelections(SessionId session, ChannelId channel) override;
    void writeText(SessionId session, const std::string& value) override;
    void writeText(SessionId session, ChannelId channel,
                   const std::string& value) override;
    void beginTextOutputBatch(SessionId session, ChannelId channel) override;
    void endTextOutputBatch(SessionId session, ChannelId channel) override;
    void flushTextOutputBatches(SessionId session) override;
    void beginPresentationBatch(SessionId session) override;
    void endPresentationBatch(SessionId session) override;
    bool setTextPosition(SessionId session, ChannelId channel,
                         std::uint32_t row, std::uint32_t column) override;
    void clear(SessionId session) override;
    void clear(SessionId session, ChannelId channel) override;
    void closeWindow(SessionId session) override;
    void closeWindow(SessionId session, ChannelId channel) override;
    void closeSession(SessionId session) override;
    void discardSession(SessionId session) override;
    void setUpdateMode(SessionId session, UpdateMode mode) override;
    void setFrameSkip(SessionId session, std::uint32_t frames) override;
    void update(SessionId session) override;
    void resetUpdateState(SessionId session) override;
    WindowInfo windowInfo(SessionId session) override;
    WindowInfo windowInfo(SessionId session, ChannelId channel) override;
    RgbColor paletteColor(SessionId session, std::uint16_t index) override;
    RgbColor paletteColor(SessionId session, ChannelId channel,
                          std::uint16_t index) override;
    void setPaletteColors(SessionId session, std::uint16_t firstIndex,
                          const std::vector<RgbColor>& colors) override;
    void setWindowBounds(SessionId session, Rect bounds) override;
    void setWindowBounds(SessionId session, ChannelId channel, Rect bounds) override;
    void setTaskViewport(SessionId session, TaskViewport viewport) override;
    TaskViewport taskViewport(SessionId session) const override;
    std::pair<std::uint32_t, std::uint32_t> displaySize() const noexcept override {
        return {manager_.screenWidth(), manager_.screenHeight()};
    }
    bool setDisplaySize(std::uint32_t width, std::uint32_t height) override;
    bool lastDisplayResizeDiscardedWindows() const noexcept override {
        return lastDisplayResizeDiscardedWindows_;
    }
    std::uint16_t rotationDegrees() const noexcept { return rotationDegrees_; }
    void setRotation(SessionId session, std::uint16_t degrees) override;
    void setDebugOverlayEnabled(bool enabled) override;
    bool debugOverlayEnabled() const noexcept override { return debugOverlayEnabled_; }
    void setDebugOverlayText(const std::string& text) override;

    // Optional presentation rate limit for slow presenters. Region presents
    // are merged into one pending dirty rectangle and published when
    // max(minIntervalUs, lastPresentUs * costRatio) has elapsed since the
    // previous present finished. pollPendingPresentation() must be called
    // regularly so a deferred tail is eventually shown; flushPendingPresentation()
    // publishes it immediately. minIntervalUs == 0 and costRatio == 0 disable it.
    struct CoalesceStats {
        std::uint64_t requests = 0;
        std::uint64_t deferred = 0;
        std::uint64_t presented = 0;
        std::uint64_t presentUs = 0;
    };
    void setPresentCoalescing(std::uint32_t minIntervalUs, std::uint32_t costRatio,
                              std::function<std::uint64_t()> clockUs);
    void pollPendingPresentation();
    void flushPendingPresentation();
    bool hasPendingPresentation() const noexcept { return pendingDirty_.width != 0 && pendingDirty_.height != 0; }
    const CoalesceStats& coalesceStats() const noexcept { return coalesceStats_; }
    void resetCoalesceStats() noexcept { coalesceStats_ = {}; }

    WindowId windowForSession(SessionId session);
    std::optional<ChannelId> channelForWindow(SessionId session,
                                               WindowId window) const;
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
    void setConsoleTextPositionCallback(
        std::function<void(std::uint32_t, std::uint32_t)> callback) {
        consoleTextPositionCallback_ = std::move(callback);
    }
    void setConsoleOutputBatchCallbacks(std::function<void()> begin,
                                        std::function<void()> end) {
        consoleOutputBatchBeginCallback_ = std::move(begin);
        consoleOutputBatchEndCallback_ = std::move(end);
    }
    void setWindowPixelDepth(SessionId session, PixelDepth depth);
    void setWindowVisible(SessionId session, bool visible);
    void raiseWindows(SessionId session) override;
    void raiseWindow(SessionId session);
    bool tileWindows() override;
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
        bool operator<(const SurfaceKey& other) const noexcept {
            return session < other.session ||
                (session == other.session && channel < other.channel);
        }
    };
    WindowId ensureWindow(SessionId session, ChannelId channel = 1);
    IndexedCanvas& canvas(SessionId session, ChannelId channel = 1);
    struct FontState {
        const BitmapFont* font = nullptr;
        FontStyle style = FontStyle::Normal;
    };
    const FontState& textFont(SessionId session, ChannelId channel) const;
    const FontState& graphicsFont(SessionId session, ChannelId channel) const;
    void discardFrozenWindows();
    Rect screenRegion(SessionId session, ChannelId channel, std::int64_t x,
                      std::int64_t y,
                      std::int64_t width, std::int64_t height);
    void discardPresentationBuffer();
    void presentRegion(Rect region);
    bool skipPresentationRequest();
    void composeDisplayRegionRow(std::uint32_t y, std::uint32_t x,
                                 std::uint32_t width,
                                 std::vector<ColorIndex>& row) const;
    void presentIfConfigured();

    WindowManager manager_;
    std::shared_ptr<FramePresenter> presenter_;
    std::map<SurfaceKey, WindowId> sessionWindows_;
    std::vector<WindowId> frozenWindows_;
    std::map<SurfaceKey, Point> textCursors_;
    std::map<SurfaceKey, Point> graphicsCursors_;
    struct TextOutputBatch { std::size_t depth = 0; Rect dirty{}; };
    std::map<SurfaceKey, TextOutputBatch> textOutputBatches_;
    std::uint32_t coalesceMinIntervalUs_ = 0;
    std::uint32_t coalesceCostRatio_ = 0;
    std::function<std::uint64_t()> coalesceClockUs_;
    Rect pendingDirty_{};
    std::uint64_t nextPresentAllowedUs_ = 0;
    CoalesceStats coalesceStats_{};
    bool coalescingEnabled() const noexcept {
        return coalesceClockUs_ && (coalesceMinIntervalUs_ != 0 || coalesceCostRatio_ != 0);
    }
    void presentRegionNow(Rect region);
    bool takeCoalescedRegion(Rect& region);
    std::size_t presentationBatchDepth_ = 0;
    bool presentationBatchNeedsFullFrame_ = false;
    Rect presentationBatchDirty_{};
    std::map<SurfaceKey, FontState> textFonts_;
    std::map<SurfaceKey, FontState> graphicsFonts_;
    std::unordered_map<ChannelId, Rect> defaultWindowBounds_;
    std::function<void(Rect)> consoleBoundsCallback_;
    std::function<void(ColorIndex, ColorIndex)> consoleColorsCallback_;
    std::function<void()> consoleClearCallback_;
    std::function<void(std::uint32_t, std::uint32_t)> consoleTextPositionCallback_;
    std::function<void()> consoleOutputBatchBeginCallback_;
    std::function<void()> consoleOutputBatchEndCallback_;
    Frame frame_;
    std::array<RgbColor, 256> palette_{};
    bool lastDisplayResizeDiscardedWindows_ = false;
    std::uint16_t rotationDegrees_ = 0;
    UpdateMode updateMode_ = UpdateMode::Auto;
    std::uint32_t frameSkip_ = 0;
    std::uint32_t frameSkipRemaining_ = 0;
    RowLayer consoleLayer_;
    RowLayer uiLayer_;
    bool debugOverlayEnabled_ = false;
    std::string debugOverlayText_;
};

} // namespace ql32::graphics
