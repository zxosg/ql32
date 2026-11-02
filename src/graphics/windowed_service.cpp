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

Rect intersect(Rect first, Rect second) {
    const auto left = std::max<std::int64_t>(first.x, second.x);
    const auto top = std::max<std::int64_t>(first.y, second.y);
    const auto right = std::min<std::int64_t>(
        static_cast<std::int64_t>(first.x) + first.width,
        static_cast<std::int64_t>(second.x) + second.width);
    const auto bottom = std::min<std::int64_t>(
        static_cast<std::int64_t>(first.y) + first.height,
        static_cast<std::int64_t>(second.y) + second.height);
    if (left >= right || top >= bottom) return {};
    return {static_cast<std::int32_t>(left), static_cast<std::int32_t>(top),
            static_cast<std::uint32_t>(right - left),
            static_cast<std::uint32_t>(bottom - top)};
}

Rect unite(Rect first, Rect second) {
    if (first.width == 0 || first.height == 0) return second;
    if (second.width == 0 || second.height == 0) return first;
    const auto left = std::min<std::int64_t>(first.x, second.x);
    const auto top = std::min<std::int64_t>(first.y, second.y);
    const auto right = std::max<std::int64_t>(
        static_cast<std::int64_t>(first.x) + first.width,
        static_cast<std::int64_t>(second.x) + second.width);
    const auto bottom = std::max<std::int64_t>(
        static_cast<std::int64_t>(first.y) + first.height,
        static_cast<std::int64_t>(second.y) + second.height);
    return {static_cast<std::int32_t>(left), static_cast<std::int32_t>(top),
            static_cast<std::uint32_t>(right - left),
            static_cast<std::uint32_t>(bottom - top)};
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
    discardPresentationBuffer();
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

Rect WindowedService::screenRegion(SessionId session, std::int64_t x,
                                   std::int64_t y, std::int64_t width,
                                   std::int64_t height) {
    if (width <= 0 || height <= 0) return {};
    const auto id = ensureWindow(session);
    const auto& target = manager_.canvas(id);
    const auto state = manager_.windowState(id);
    const auto localLeft = std::max<std::int64_t>(0, x);
    const auto localTop = std::max<std::int64_t>(0, y);
    const auto localRight = std::min<std::int64_t>(target.width(), x + width);
    const auto localBottom = std::min<std::int64_t>(target.height(), y + height);
    if (localLeft >= localRight || localTop >= localBottom) return {};
    const Rect translated{
        static_cast<std::int32_t>(static_cast<std::int64_t>(state.bounds.x) + localLeft),
        static_cast<std::int32_t>(static_cast<std::int64_t>(state.bounds.y) + localTop),
        static_cast<std::uint32_t>(localRight - localLeft),
        static_cast<std::uint32_t>(localBottom - localTop)};
    return intersect(translated,
                     {0, 0, manager_.screenWidth(), manager_.screenHeight()});
}

void WindowedService::setInk(SessionId session, ColorIndex color) {
    canvas(session).setInk(color);
}

void WindowedService::setPaper(SessionId session, ColorIndex color) {
    canvas(session).setPaper(color);
}

void WindowedService::line(SessionId session, Point from, Point to) {
    canvas(session).line(from, to);
    const auto left = std::min<std::int64_t>(from.x, to.x);
    const auto top = std::min<std::int64_t>(from.y, to.y);
    const auto width = std::abs(static_cast<std::int64_t>(from.x) - to.x) + 1;
    const auto height = std::abs(static_cast<std::int64_t>(from.y) - to.y) + 1;
    presentRegion(screenRegion(session, left, top, width, height));
}

void WindowedService::rectangle(SessionId session, Point topLeft,
                                std::int32_t width, std::int32_t height) {
    canvas(session).rectangle(topLeft, width, height);
    presentRegion(screenRegion(session, topLeft.x, topLeft.y, width, height));
}

void WindowedService::circle(SessionId session, Point center, std::int32_t radius) {
    canvas(session).circle(center, radius);
    const auto extent = static_cast<std::int64_t>(radius);
    presentRegion(screenRegion(session,
        static_cast<std::int64_t>(center.x) - extent,
        static_cast<std::int64_t>(center.y) - extent,
        extent * 2 + 1, extent * 2 + 1));
}

void WindowedService::text(SessionId session, Point topLeft, const std::string& value) {
    canvas(session).text(topLeft, value);
    presentRegion(screenRegion(session, topLeft.x, topLeft.y,
        static_cast<std::int64_t>(value.size()) * 6, 8));
}

void WindowedService::writeText(SessionId session, const std::string& value) {
    auto& cursor = textCursors_[session];
    const auto info = windowInfo(session);
    const auto columns = std::max<std::uint32_t>(1, info.width / 6);
    const auto rows = std::max<std::uint32_t>(1, info.height / 8);
    const auto maxX = static_cast<std::int32_t>(columns * 6);
    const auto maxY = static_cast<std::int32_t>(rows * 8);
    const auto id = ensureWindow(session);
    const auto& bounds = manager_.windowState(id).bounds;
    auto& target = manager_.canvas(id);
    Rect dirty{};
    bool scrolled = false;
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
            dirty = unite(dirty, {cursor.x, cursor.y, 6, 8});
            cursor.x += 6;
        }
        if (cursor.y + 7 >= maxY) {
            target.scrollUp(8);
            cursor.y = std::max(0, cursor.y - 8);
            scrolled = true;
        }
    }
    if (scrolled) dirty = {0, 0, target.width(), target.height()};
    if (dirty.width != 0 && dirty.height != 0) {
        const Rect translated{
            static_cast<std::int32_t>(static_cast<std::int64_t>(bounds.x) + dirty.x),
            static_cast<std::int32_t>(static_cast<std::int64_t>(bounds.y) + dirty.y),
            dirty.width, dirty.height};
        presentRegion(intersect(translated,
            {0, 0, manager_.screenWidth(), manager_.screenHeight()}));
    }
}

void WindowedService::clear(SessionId session) {
    canvas(session).clear();
    textCursors_[session] = {};
    const auto id = sessionWindows_.at(session);
    presentRegion(manager_.windowState(id).screenClip);
}

void WindowedService::closeWindow(SessionId session) {
    const auto found = sessionWindows_.find(session);
    if (found == sessionWindows_.end()) return;
    const auto oldBounds = manager_.windowState(found->second).screenClip;
    manager_.close(found->second);
    sessionWindows_.erase(found);
    textCursors_.erase(session);
    presentRegion(oldBounds);
}

void WindowedService::closeSession(SessionId session) {
    const auto found = sessionWindows_.find(session);
    if (found == sessionWindows_.end()) return;
    const auto oldBounds = manager_.windowState(found->second).screenClip;
    manager_.close(found->second);
    sessionWindows_.erase(found);
    textCursors_.erase(session);
    // Keep the last Linux frame available for inspection when the final
    // command-line session exits. Remaining sessions still trigger redraw.
    if (manager_.size() != 0) presentRegion(oldBounds);
}

void WindowedService::setUpdateMode(SessionId session, UpdateMode mode) {
    if (session == 0) throw std::invalid_argument("graphics session must be valid");
    updateMode_ = mode;
    if (mode != UpdateMode::Off) present();
}

void WindowedService::update(SessionId session) {
    if (session == 0) throw std::invalid_argument("graphics session must be valid");
    present();
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
    const auto found = sessionWindows_.find(session);
    if (found == sessionWindows_.end()) {
        if (session == 0) throw std::invalid_argument("graphics session must be valid");
        discardPresentationBuffer();
        const auto id = manager_.create(session, bounds, PixelDepth::Four);
        sessionWindows_.emplace(session, id);
        presentRegion(intersect(bounds,
            {0, 0, manager_.screenWidth(), manager_.screenHeight()}));
        return;
    }
    const auto id = found->second;
    const auto oldBounds = manager_.windowState(id).screenClip;
    const auto current = manager_.windowState(id).bounds;
    if (bounds.width != current.width || bounds.height != current.height)
        discardPresentationBuffer();
    manager_.setBounds(id, bounds);
    const auto newBounds = intersect(bounds,
        {0, 0, manager_.screenWidth(), manager_.screenHeight()});
    presentRegion(unite(oldBounds, newBounds));
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
    discardPresentationBuffer();
    const auto id = ensureWindow(session);
    const auto dirty = manager_.windowState(id).screenClip;
    manager_.setPixelDepth(id, depth);
    presentRegion(dirty);
}

void WindowedService::setWindowVisible(SessionId session, bool visible) {
    const auto id = ensureWindow(session);
    const auto dirty = manager_.windowState(id).screenClip;
    manager_.setVisible(id, visible);
    presentRegion(dirty);
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
    for (std::size_t index = 0; index < frame_.palette.size(); ++index)
        frame_.palette[index] = defaultPaletteColor(static_cast<std::uint16_t>(index));
    const auto width = manager_.screenWidth();
    const auto height = manager_.screenHeight();
    if (presenter_->presentRows(
            width, height, frame_.palette,
            [this](std::uint32_t y, std::vector<ColorIndex>& row) {
                manager_.composeRowInto(y, row);
            })) {
        return;
    }
    frame_.width = manager_.screenWidth();
    frame_.height = manager_.screenHeight();
    manager_.composeInto(frame_.pixels);
    presenter_->present(frame_);
}

void WindowedService::presentRegion(Rect region) {
    if (!presenter_) return;
    if (updateMode_ == UpdateMode::Off) return;
    region = intersect(region,
        {0, 0, manager_.screenWidth(), manager_.screenHeight()});
    if (region.width == 0 || region.height == 0) return;
    for (std::size_t index = 0; index < frame_.palette.size(); ++index)
        frame_.palette[index] = defaultPaletteColor(static_cast<std::uint16_t>(index));
    const auto width = manager_.screenWidth();
    const auto height = manager_.screenHeight();
    if (presenter_->presentRowsRegion(width, height, frame_.palette, region,
            [this, region](std::uint32_t y, std::vector<ColorIndex>& row) {
                manager_.composeRegionRowInto(y,
                    static_cast<std::uint32_t>(region.x), region.width, row);
            }))
        return;
    present();
}

void WindowedService::discardPresentationBuffer() {
    std::vector<ColorIndex>().swap(frame_.pixels);
}

void WindowedService::presentIfConfigured() {
    if (!presenter_) return;
    if (updateMode_ == UpdateMode::Off) return;
    present();
}

} // namespace ql32::graphics
