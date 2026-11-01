#pragma once

#include "ql32/graphics/frame_presenter.hpp"
#include "ql32/graphics/service.hpp"
#include "ql32/graphics/window_manager.hpp"

#include <memory>
#include <cstdint>
#include <unordered_map>

namespace ql32::graphics {

// BASIC graphics service backed by retained canvases and a portable compositor.
class WindowedService final : public Service {
public:
    WindowedService(std::uint32_t screenWidth, std::uint32_t screenHeight,
                    std::shared_ptr<FramePresenter> presenter,
                    std::size_t maximumWindows = 4,
                    std::size_t maximumCanvasBytes = 0);

    void setInk(SessionId session, ColorIndex color) override;
    void setPaper(SessionId session, ColorIndex color) override;
    void line(SessionId session, Point from, Point to) override;
    void rectangle(SessionId session, Point topLeft,
                   std::int32_t width, std::int32_t height) override;
    void circle(SessionId session, Point center, std::int32_t radius) override;
    void text(SessionId session, Point topLeft, const std::string& value) override;
    void writeText(SessionId session, const std::string& value) override;
    void clear(SessionId session) override;
    void closeWindow(SessionId session) override;
    void closeSession(SessionId session) override;
    WindowInfo windowInfo(SessionId session) override;
    RgbColor paletteColor(SessionId session, std::uint16_t index) override;
    void setWindowBounds(SessionId session, Rect bounds) override;
    void setRotation(SessionId session, std::uint16_t degrees) override;

    WindowId windowForSession(SessionId session);
    void setWindowPixelDepth(SessionId session, PixelDepth depth);
    void setWindowVisible(SessionId session, bool visible);
    void raiseWindow(SessionId session);
    void tileWindows();
    void present();
    WindowManager& windowManager() noexcept { return manager_; }

private:
    WindowId ensureWindow(SessionId session);
    IndexedCanvas& canvas(SessionId session);
    Rect screenRegion(SessionId session, std::int64_t x, std::int64_t y,
                      std::int64_t width, std::int64_t height);
    static RgbColor defaultPaletteColor(std::uint16_t index);
    void discardPresentationBuffer();
    void presentRegion(Rect region);
    void presentIfConfigured();

    WindowManager manager_;
    std::shared_ptr<FramePresenter> presenter_;
    std::unordered_map<SessionId, WindowId> sessionWindows_;
    std::unordered_map<SessionId, Point> textCursors_;
    Frame frame_;
    std::uint16_t rotationDegrees_ = 0;
};

} // namespace ql32::graphics
