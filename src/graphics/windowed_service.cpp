#include "ql32/graphics/windowed_service.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace ql32::graphics {
namespace {

constexpr std::array<RgbColor, 16> basePalette{{
    {0, 0, 0}, {0, 0, 255}, {255, 0, 0}, {255, 0, 255},
    {0, 255, 0}, {0, 255, 255}, {255, 255, 0}, {255, 255, 255},
    {85, 85, 85}, {85, 85, 255}, {255, 85, 85}, {255, 85, 255},
    {85, 255, 85}, {85, 255, 255}, {255, 255, 85}, {192, 192, 192}
}};

std::uint8_t scale(std::uint8_t value, std::uint8_t maximum) {
    return static_cast<std::uint8_t>(static_cast<unsigned>(value) * 255u / maximum);
}

} // namespace

WindowedService::WindowedService(std::uint32_t screenWidth, std::uint32_t screenHeight,
                                 std::shared_ptr<FramePresenter> presenter,
                                 std::size_t maximumWindows,
                                 std::size_t maximumCanvasBytes)
    : manager_(screenWidth, screenHeight, maximumWindows, maximumCanvasBytes),
      presenter_(std::move(presenter)) {}

WindowId WindowedService::ensureWindow(SessionId session) {
    if (session == 0) throw std::invalid_argument("graphics session must be valid");
    const auto found = sessionWindows_.find(session);
    if (found != sessionWindows_.end()) return found->second;
    const auto id = manager_.create(session, {0, 0, manager_.screenWidth(),
                                              manager_.screenHeight()},
                                   PixelDepth::Four);
    sessionWindows_.emplace(session, id);
    return id;
}

WindowId WindowedService::windowForSession(SessionId session) {
    return ensureWindow(session);
}

IndexedCanvas& WindowedService::canvas(SessionId session) {
    return manager_.canvas(ensureWindow(session));
}

void WindowedService::setInk(SessionId session, ColorIndex color) {
    canvas(session).setInk(color);
}

void WindowedService::setPaper(SessionId session, ColorIndex color) {
    canvas(session).setPaper(color);
}

void WindowedService::line(SessionId session, Point from, Point to) {
    canvas(session).line(from, to);
    presentIfConfigured();
}

void WindowedService::rectangle(SessionId session, Point topLeft,
                                std::int32_t width, std::int32_t height) {
    canvas(session).rectangle(topLeft, width, height);
    presentIfConfigured();
}

void WindowedService::circle(SessionId session, Point center, std::int32_t radius) {
    canvas(session).circle(center, radius);
    presentIfConfigured();
}

void WindowedService::text(SessionId session, Point topLeft, const std::string& value) {
    canvas(session).text(topLeft, value);
    presentIfConfigured();
}

void WindowedService::writeText(SessionId session, const std::string& value) {
    auto& cursor = textCursors_[session];
    const auto info = windowInfo(session);
    const auto columns = std::max<std::uint32_t>(1, info.width / 6);
    const auto rows = std::max<std::uint32_t>(1, info.height / 8);
    const auto maxX = static_cast<std::int32_t>(columns * 6);
    const auto maxY = static_cast<std::int32_t>(rows * 8);
    auto& target = canvas(session);
    cursor.x = std::clamp(cursor.x, 0, std::max(0, maxX - 6));
    cursor.y = std::clamp(cursor.y, 0, std::max(0, maxY - 8));
    for (const unsigned char ch : value) {
        if (ch == '\r') continue;
        if (ch == '\n') {
            cursor.x = 0;
            cursor.y += 8;
        } else {
            if (cursor.x + 5 > maxX) {
                cursor.x = 0;
                cursor.y += 8;
            }
            target.text(cursor, std::string(1, ch < 128 ? static_cast<char>(ch) : '?'));
            cursor.x += 6;
        }
        if (cursor.y + 7 >= maxY) {
            target.scrollUp(8);
            cursor.y = std::max(0, cursor.y - 8);
        }
    }
    presentIfConfigured();
}

void WindowedService::clear(SessionId session) {
    canvas(session).clear();
    textCursors_[session] = {};
    presentIfConfigured();
}

void WindowedService::closeWindow(SessionId session) {
    const auto found = sessionWindows_.find(session);
    if (found == sessionWindows_.end()) return;
    manager_.close(found->second);
    sessionWindows_.erase(found);
    textCursors_.erase(session);
    presentIfConfigured();
}

void WindowedService::closeSession(SessionId session) {
    const auto found = sessionWindows_.find(session);
    if (found == sessionWindows_.end()) return;
    manager_.close(found->second);
    sessionWindows_.erase(found);
    textCursors_.erase(session);
    // Keep the last Linux frame available for inspection when the final
    // command-line session exits. Remaining sessions still trigger redraw.
    if (manager_.size() != 0) presentIfConfigured();
}

WindowInfo WindowedService::windowInfo(SessionId session) {
    const auto id = ensureWindow(session);
    const auto& window = manager_.canvas(id);
    const auto state = manager_.windowState(id);
    return {window.width(), window.height(), static_cast<std::uint8_t>(window.depth()),
            window.paletteSize(), rotationDegrees_, state.bounds.x, state.bounds.y};
}

RgbColor WindowedService::defaultPaletteColor(std::uint16_t index) {
    if (index < basePalette.size()) return basePalette[index];
    const auto red = static_cast<std::uint8_t>((index >> 5) & 0x07);
    const auto green = static_cast<std::uint8_t>((index >> 2) & 0x07);
    const auto blue = static_cast<std::uint8_t>(index & 0x03);
    return {scale(red, 7), scale(green, 7), scale(blue, 3)};
}

RgbColor WindowedService::paletteColor(SessionId session, std::uint16_t index) {
    const auto& window = canvas(session);
    if (index >= window.paletteSize()) throw std::out_of_range("palette index out of range");
    return defaultPaletteColor(index);
}

void WindowedService::setWindowBounds(SessionId session, Rect bounds) {
    manager_.setBounds(ensureWindow(session), bounds);
    presentIfConfigured();
}

void WindowedService::setRotation(SessionId session, std::uint16_t degrees) {
    if (degrees != 0 && degrees != 90 && degrees != 180 && degrees != 270)
        throw std::invalid_argument("rotation must be 0, 90, 180, or 270 degrees");
    const auto firstWindow = ensureWindow(session);
    const auto oldWidth = manager_.screenWidth();
    const auto oldHeight = manager_.screenHeight();
    if (!presenter_)
        throw std::runtime_error("display rotation unsupported");
    const auto display = presenter_->setRotation(degrees);
    if (display.width == 0 || display.height == 0)
        throw std::runtime_error("display presenter returned invalid dimensions");
    manager_.setScreenSize(display.width, display.height);
    for (const auto& [owner, id] : sessionWindows_) {
        (void)owner;
        const auto state = manager_.windowState(id);
        if (state.bounds.x == 0 && state.bounds.y == 0 &&
            state.bounds.width == oldWidth && state.bounds.height == oldHeight)
            manager_.setBounds(id, {0, 0, display.width, display.height});
    }
    rotationDegrees_ = degrees;
    (void)firstWindow;
    presentIfConfigured();
}

void WindowedService::setWindowPixelDepth(SessionId session, PixelDepth depth) {
    manager_.setPixelDepth(ensureWindow(session), depth);
    presentIfConfigured();
}

void WindowedService::setWindowVisible(SessionId session, bool visible) {
    manager_.setVisible(ensureWindow(session), visible);
    presentIfConfigured();
}

void WindowedService::raiseWindow(SessionId session) {
    manager_.raise(ensureWindow(session));
    presentIfConfigured();
}

void WindowedService::tileWindows() {
    manager_.tile();
    presentIfConfigured();
}

void WindowedService::present() {
    if (!presenter_) return;
    frame_.width = manager_.screenWidth();
    frame_.height = manager_.screenHeight();
    manager_.composeInto(frame_.pixels);
    for (std::size_t index = 0; index < frame_.palette.size(); ++index)
        frame_.palette[index] = defaultPaletteColor(static_cast<std::uint16_t>(index));
    presenter_->present(frame_);
}

void WindowedService::presentIfConfigured() {
    if (presenter_) present();
}

} // namespace ql32::graphics
