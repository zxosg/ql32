#include "ql32/graphics/windowed_service.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <vector>

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
                                 std::shared_ptr<FramePresenter> presenter)
    : manager_(screenWidth, screenHeight),
      presenter_(std::move(presenter)) {}

void WindowedService::discardFrozenWindows() {
    if (frozenWindows_.empty()) return;
    Rect dirty{};
    std::vector<SessionId> owners;
    for (const auto id : frozenWindows_) {
        const auto state = manager_.windowState(id);
        dirty = unite(dirty, state.screenClip);
        owners.push_back(state.owner);
        manager_.close(id);
    }
    std::vector<WindowId>().swap(frozenWindows_);
    std::sort(owners.begin(), owners.end());
    owners.erase(std::unique(owners.begin(), owners.end()), owners.end());
    for (const auto owner : owners) manager_.releaseTaskViewportIfUnused(owner);
    presentRegion(dirty);
}

WindowId WindowedService::ensureWindow(SessionId session, ChannelId channel) {
    if (session == 0) throw std::invalid_argument("graphics session must be valid");
    const SurfaceKey key{session, channel};
    const auto found = sessionWindows_.find(key);
    if (found != sessionWindows_.end()) return found->second;
    // Completed tasks leave their last composed pixels visible on write-only
    // displays. Keep those canvases for capture until a later session draws.
    discardFrozenWindows();
    discardPresentationBuffer();
    const auto defaultBounds = defaultWindowBounds_.find(channel);
    const auto bounds = defaultBounds != defaultWindowBounds_.end() ? defaultBounds->second :
        Rect{0, 0, manager_.screenWidth(), manager_.screenHeight()};
    const auto id = manager_.create(session, bounds,
                                   PixelDepth::Four);
    sessionWindows_.emplace(key, id);
    return id;
}

WindowId WindowedService::windowForSession(SessionId session) {
    return ensureWindow(session);
}

std::optional<ChannelId> WindowedService::channelForWindow(SessionId session,
                                                            WindowId window) const {
    for (const auto& [key, id] : sessionWindows_)
        if (key.session == session && id == window) return key.channel;
    return std::nullopt;
}

void WindowedService::setDefaultWindowBounds(ChannelId channel, Rect bounds) {
    if (bounds.width == 0 || bounds.height == 0)
        throw std::invalid_argument("window size must be positive");
    defaultWindowBounds_[channel] = bounds;
}

bool WindowedService::setDisplaySize(std::uint32_t width, std::uint32_t height) {
    if (width == 0 || height == 0 ||
        width > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()) ||
        height > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()))
        return false;
    const auto oldWidth = manager_.screenWidth();
    const auto oldHeight = manager_.screenHeight();
    if (oldWidth == width && oldHeight == height) return true;

    auto resizedDefaults = defaultWindowBounds_;
    for (auto& [channel, bounds] : resizedDefaults) {
        (void)channel;
        const auto old = bounds;
        if (old.x == 0 && old.width == oldWidth) bounds.width = width;
        if (old.y == 0 && old.height == oldHeight) bounds.height = height;
        else if (old.y == 0 && old.x == 0 && old.width == oldWidth &&
                 static_cast<std::uint64_t>(old.height) < oldHeight) {
            const auto bottomMargin = oldHeight - old.height;
            bounds.height = height > bottomMargin ? height - bottomMargin : height;
        }
        if (old.y != 0 &&
            static_cast<std::uint64_t>(old.y) + old.height == oldHeight) {
            bounds.y = static_cast<std::int32_t>(height > old.height ? height - old.height : 0);
            bounds.height = std::min(bounds.height, height);
        }
        if (bounds.height == 0) bounds.height = 1;
    }

    struct WindowResize { WindowId id; Rect oldBounds; Rect newBounds; };
    std::vector<WindowResize> windowsToResize;
    std::map<SessionId, TaskViewport> oldViewports;
    for (const auto& [key, id] : sessionWindows_) {
        oldViewports.emplace(key.session, manager_.taskViewport(key.session));
        const auto oldDefault = defaultWindowBounds_.find(key.channel);
        const auto nextDefault = resizedDefaults.find(key.channel);
        if (oldDefault == defaultWindowBounds_.end() || nextDefault == resizedDefaults.end())
            continue;
        const auto viewport = manager_.taskViewport(key.session);
        const auto state = manager_.windowState(id);
        const auto expectedX = static_cast<std::int64_t>(viewport.originX) +
                               viewport.offsetX + oldDefault->second.x;
        const auto expectedY = static_cast<std::int64_t>(viewport.originY) +
                               viewport.offsetY + oldDefault->second.y;
        if (state.bounds.x == expectedX && state.bounds.y == expectedY &&
            state.bounds.width == oldDefault->second.width &&
            state.bounds.height == oldDefault->second.height)
            windowsToResize.push_back({id, oldDefault->second, nextDefault->second});
    }

    manager_.setScreenSize(width, height);
    try {
        std::map<SessionId, TaskViewport> viewports;
        for (const auto& [session, originalViewport] : oldViewports) {
            auto viewport = originalViewport;
            if (viewport.originX == 0 && viewport.originY == 0 &&
                viewport.offsetX == 0 && viewport.offsetY == 0 &&
                viewport.canvasWidth == oldWidth && viewport.canvasHeight == oldHeight) {
                viewport.canvasWidth = width;
                viewport.canvasHeight = height;
                viewports[session] = viewport;
            }
        }
        for (const auto& [session, viewport] : viewports)
            manager_.setTaskViewport(session, viewport);
        for (const auto& window : windowsToResize)
            manager_.setBounds(window.id, window.newBounds);
    } catch (...) {
        for (auto window = windowsToResize.rbegin(); window != windowsToResize.rend(); ++window) {
            try { manager_.setBounds(window->id, window->oldBounds); } catch (...) {}
        }
        for (const auto& [session, viewport] : oldViewports) {
            try { manager_.setTaskViewport(session, viewport); } catch (...) {}
        }
        manager_.setScreenSize(oldWidth, oldHeight);
        throw;
    }

    defaultWindowBounds_ = std::move(resizedDefaults);
    if (consoleBoundsCallback_) {
        const auto console = defaultWindowBounds_.find(0);
        if (console != defaultWindowBounds_.end()) consoleBoundsCallback_(console->second);
    }
    discardPresentationBuffer();
    present();
    return true;
}

IndexedCanvas& WindowedService::canvas(SessionId session, ChannelId channel) {
    return manager_.canvas(ensureWindow(session, channel));
}

Rect WindowedService::screenRegion(SessionId session, ChannelId channel,
                                   std::int64_t x,
                                   std::int64_t y, std::int64_t width,
                                   std::int64_t height) {
    if (width <= 0 || height <= 0) return {};
    const auto id = ensureWindow(session, channel);
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
    return intersect(translated, state.screenClip);
}

void WindowedService::setInk(SessionId session, ColorIndex color) {
    setInk(session, 1, color);
}

void WindowedService::setInk(SessionId session, ChannelId channel, ColorIndex color) {
    auto& target = canvas(session, channel);
    target.setInk(color);
    if (channel == 0 && consoleColorsCallback_)
        consoleColorsCallback_(target.ink(), target.paper());
}

void WindowedService::setPaper(SessionId session, ColorIndex color) {
    setPaper(session, 1, color);
}

void WindowedService::setPaper(SessionId session, ChannelId channel, ColorIndex color) {
    auto& target = canvas(session, channel);
    target.setPaper(color);
    if (channel == 0 && consoleColorsCallback_)
        consoleColorsCallback_(target.ink(), target.paper());
}

void WindowedService::setOverMode(SessionId session, OverMode mode) {
    setOverMode(session, 1, mode);
}

void WindowedService::setOverMode(SessionId session, ChannelId channel, OverMode mode) {
    canvas(session, channel).setOverMode(mode);
}

void WindowedService::line(SessionId session, Point from, Point to) {
    line(session, 1, from, to);
}

void WindowedService::line(SessionId session, ChannelId channel, Point from, Point to) {
    canvas(session, channel).line(from, to);
    const auto left = std::min<std::int64_t>(from.x, to.x);
    const auto top = std::min<std::int64_t>(from.y, to.y);
    const auto width = std::abs(static_cast<std::int64_t>(from.x) - to.x) + 1;
    const auto height = std::abs(static_cast<std::int64_t>(from.y) - to.y) + 1;
    presentRegion(screenRegion(session, channel, left, top, width, height));
}

void WindowedService::pixel(SessionId session, Point position) {
    pixel(session, 1, position);
}

void WindowedService::pixel(SessionId session, ChannelId channel, Point position) {
    auto& target = canvas(session, channel);
    target.plotPixel(position.x, position.y);
    presentRegion(screenRegion(session, channel, position.x, position.y, 1, 1));
}

void WindowedService::rectangle(SessionId session, Point topLeft,
                                std::int32_t width, std::int32_t height) {
    rectangle(session, 1, topLeft, width, height);
}

void WindowedService::rectangle(SessionId session, ChannelId channel, Point topLeft,
                                std::int32_t width, std::int32_t height) {
    canvas(session, channel).rectangle(topLeft, width, height, false);
    presentRegion(screenRegion(session, channel, topLeft.x, topLeft.y, width, height));
}

void WindowedService::fillRectangle(SessionId session, Point topLeft,
                                    std::int32_t width, std::int32_t height) {
    fillRectangle(session, 1, topLeft, width, height);
}

void WindowedService::fillRectangle(SessionId session, ChannelId channel,
                                    Point topLeft, std::int32_t width,
                                    std::int32_t height) {
    canvas(session, channel).rectangle(topLeft, width, height, true);
    presentRegion(screenRegion(session, channel, topLeft.x, topLeft.y, width, height));
}

void WindowedService::circle(SessionId session, Point center, std::int32_t radius) {
    circle(session, 1, center, radius);
}

void WindowedService::circle(SessionId session, ChannelId channel, Point center,
                             std::int32_t radius) {
    canvas(session, channel).circle(center, radius, false);
    const auto extent = static_cast<std::int64_t>(radius);
    presentRegion(screenRegion(session, channel,
        static_cast<std::int64_t>(center.x) - extent,
        static_cast<std::int64_t>(center.y) - extent,
        extent * 2 + 1, extent * 2 + 1));
}

void WindowedService::fillCircle(SessionId session, Point center, std::int32_t radius) {
    fillCircle(session, 1, center, radius);
}

void WindowedService::fillCircle(SessionId session, ChannelId channel,
                                 Point center, std::int32_t radius) {
    canvas(session, channel).circle(center, radius, true);
    const auto extent = static_cast<std::int64_t>(radius);
    presentRegion(screenRegion(session, channel,
        static_cast<std::int64_t>(center.x) - extent,
        static_cast<std::int64_t>(center.y) - extent,
        extent * 2 + 1, extent * 2 + 1));
}

void WindowedService::text(SessionId session, Point topLeft, const std::string& value) {
    text(session, 1, topLeft, value);
}

void WindowedService::text(SessionId session, ChannelId channel, Point topLeft,
                           const std::string& value) {
    const auto& selection = graphicsFont(session, channel);
    const auto* selectedFont = selection.font;
    auto& target = canvas(session, channel);
    const Rect dirty = selectedFont ?
        target.text(topLeft, value, *selectedFont, selection.style) :
        target.text(topLeft, value);
    if (dirty.width != 0 && dirty.height != 0)
        presentRegion(screenRegion(session, channel, dirty.x, dirty.y,
                                   dirty.width, dirty.height));
}

void WindowedService::setTextFont(SessionId session, ChannelId channel,
                                  const BitmapFont* font, FontStyle style) {
    textFonts_[{session, channel}] = {font, style};
}

void WindowedService::setGraphicsFont(SessionId session, ChannelId channel,
                                      const BitmapFont* font, FontStyle style) {
    graphicsFonts_[{session, channel}] = {font, style};
}

void WindowedService::clearFontSelections(SessionId session, ChannelId channel) {
    textFonts_.erase({session, channel});
    graphicsFonts_.erase({session, channel});
}

const WindowedService::FontState& WindowedService::textFont(
        SessionId session, ChannelId channel) const {
    static const FontState defaultState{};
    const auto selected = textFonts_.find({session, channel});
    return selected == textFonts_.end() ? defaultState : selected->second;
}

const WindowedService::FontState& WindowedService::graphicsFont(
        SessionId session, ChannelId channel) const {
    static const FontState defaultState{};
    const auto selected = graphicsFonts_.find({session, channel});
    return selected == graphicsFonts_.end() ? defaultState : selected->second;
}

void WindowedService::writeText(SessionId session, const std::string& value) {
    writeText(session, 1, value);
}

bool WindowedService::setTextPosition(SessionId session, ChannelId channel,
                                      std::uint32_t row, std::uint32_t column) {
    if (channel == 0) {
        if (!consoleTextPositionCallback_) return false;
        consoleTextPositionCallback_(row, column);
        return true;
    }
    const auto info = windowInfo(session, channel);
    const auto& selection = textFont(session, channel);
    const auto* selectedFont = selection.font;
    const std::uint32_t cellWidth = selectedFont ? selectedFont->metrics().maxAdvance : 6;
    const std::uint32_t cellHeight = selectedFont ? selectedFont->metrics().lineHeight : 8;
    const auto columns = std::max<std::uint32_t>(1, info.width / cellWidth);
    const auto rows = std::max<std::uint32_t>(1, info.height / cellHeight);
    const auto boundedColumn = std::min(column, columns - 1);
    const auto boundedRow = std::min(row, rows - 1);
    textCursors_[{session, channel}] = {
        static_cast<std::int32_t>(boundedColumn * cellWidth),
        static_cast<std::int32_t>(boundedRow * cellHeight)};
    return true;
}

void WindowedService::writeText(SessionId session, ChannelId channel,
                                const std::string& value) {
    if (channel == 0) return;
    const SurfaceKey key{session, channel};
    auto& cursor = textCursors_[key];
    const auto info = windowInfo(session, channel);
    const auto& selection = textFont(session, channel);
    const auto* selectedFont = selection.font;
    const std::int32_t cellWidth = selectedFont ? selectedFont->metrics().maxAdvance : 6;
    const std::int32_t cellHeight = selectedFont ? selectedFont->metrics().lineHeight : 8;
    const auto columns = std::max<std::uint32_t>(1, info.width / cellWidth);
    const auto rows = std::max<std::uint32_t>(1, info.height / cellHeight);
    const auto maxX = static_cast<std::int32_t>(columns * cellWidth);
    const auto maxY = static_cast<std::int32_t>(rows * cellHeight);
    const auto id = ensureWindow(session, channel);
    const auto& bounds = manager_.windowState(id).bounds;
    auto& target = manager_.canvas(id);
    Rect dirty{};
    bool scrolled = false;
    cursor.x = std::clamp(cursor.x, 0, std::max(0, maxX - cellWidth));
    cursor.y = std::clamp(cursor.y, 0, std::max(0, maxY - cellHeight));
    for (std::size_t offset = 0; offset < value.size();) {
        const auto raw = static_cast<std::uint8_t>(value[offset]);
        if (raw == '\r') {
            ++offset;
            continue;
        }
        if (raw == '\n') {
            cursor.x = 0;
            cursor.y += cellHeight;
            ++offset;
        } else {
            const auto character = selectedFont ? nextAsciiCharacter(value, offset) :
                static_cast<std::uint8_t>(raw < 128 ? value[offset++] : '?');
            std::int32_t advance = cellWidth;
            if (selectedFont) {
                FontGlyph glyph{};
                selectedFont->readGlyph(character, glyph);
                advance = glyph.advance;
            }
            if (cursor.x + advance > maxX) {
                cursor.x = 0;
                cursor.y += cellHeight;
            }
            const Rect glyphDirty = selectedFont ?
                target.printText(cursor, std::string(1, static_cast<char>(character)),
                                 *selectedFont, selection.style) :
                target.printText(cursor, std::string(1, static_cast<char>(character)));
            dirty = unite(dirty, glyphDirty);
            cursor.x += advance;
        }
        if (cursor.y + cellHeight - 1 >= maxY) {
            target.scrollUp(cellHeight);
            cursor.y = std::max(0, cursor.y - cellHeight);
            scrolled = true;
        }
    }
    if (scrolled) dirty = {0, 0, target.width(), target.height()};
    if (dirty.width != 0 && dirty.height != 0) {
        const Rect translated{
            static_cast<std::int32_t>(static_cast<std::int64_t>(bounds.x) + dirty.x),
            static_cast<std::int32_t>(static_cast<std::int64_t>(bounds.y) + dirty.y),
            dirty.width, dirty.height};
        presentRegion(intersect(translated, manager_.windowState(id).screenClip));
    }
}

void WindowedService::clear(SessionId session) {
    clear(session, 1);
}

void WindowedService::clear(SessionId session, ChannelId channel) {
    const SurfaceKey key{session, channel};
    canvas(session, channel).clear();
    textCursors_[key] = {};
    if (channel == 0 && consoleClearCallback_) consoleClearCallback_();
    const auto id = sessionWindows_.at(key);
    presentRegion(manager_.windowState(id).screenClip);
}

void WindowedService::closeWindow(SessionId session) {
    closeWindow(session, 1);
}

void WindowedService::closeWindow(SessionId session, ChannelId channel) {
    const SurfaceKey key{session, channel};
    const auto found = sessionWindows_.find(key);
    if (found == sessionWindows_.end()) {
        textCursors_.erase(key);
        return;
    }
    const auto oldBounds = manager_.windowState(found->second).screenClip;
    manager_.close(found->second);
    sessionWindows_.erase(found);
    textCursors_.erase(key);
    if (channel == 0 && consoleBoundsCallback_) {
        const auto fallback = defaultWindowBounds_.find(channel);
        if (fallback != defaultWindowBounds_.end()) {
            const auto viewport = manager_.taskViewport(session);
            auto bounds = fallback->second;
            bounds.x = static_cast<std::int32_t>(
                static_cast<std::int64_t>(bounds.x) + viewport.originX +
                viewport.offsetX);
            bounds.y = static_cast<std::int32_t>(
                static_cast<std::int64_t>(bounds.y) + viewport.originY +
                viewport.offsetY);
            consoleBoundsCallback_(intersect(intersect(bounds,
                {viewport.originX, viewport.originY,
                 viewport.canvasWidth, viewport.canvasHeight}),
                {0, 0, manager_.screenWidth(), manager_.screenHeight()}));
        }
    }
    presentRegion(oldBounds);
}

void WindowedService::closeSession(SessionId session) {
    for (auto found = sessionWindows_.begin(); found != sessionWindows_.end();) {
        if (found->first.session != session) {
            ++found;
            continue;
        }
        frozenWindows_.push_back(found->second);
        textCursors_.erase(found->first);
        textFonts_.erase(found->first);
        graphicsFonts_.erase(found->first);
        found = sessionWindows_.erase(found);
    }
    for (auto found = textFonts_.begin(); found != textFonts_.end();) {
        if (found->first.session == session) found = textFonts_.erase(found);
        else ++found;
    }
    for (auto found = graphicsFonts_.begin(); found != graphicsFonts_.end();) {
        if (found->first.session == session) found = graphicsFonts_.erase(found);
        else ++found;
    }
    manager_.releaseTaskViewportIfUnused(session);
}

void WindowedService::discardSession(SessionId session) {
    Rect dirty{};
    for (const auto& [key, id] : sessionWindows_)
        if (key.session == session)
            dirty = unite(dirty, manager_.windowState(id).screenClip);
    for (const auto id : frozenWindows_)
        if (manager_.windowState(id).owner == session)
            dirty = unite(dirty, manager_.windowState(id).screenClip);

    for (auto found = sessionWindows_.begin(); found != sessionWindows_.end();) {
        if (found->first.session == session) {
            textCursors_.erase(found->first);
            textFonts_.erase(found->first);
            graphicsFonts_.erase(found->first);
            found = sessionWindows_.erase(found);
        } else {
            ++found;
        }
    }
    for (auto found = textFonts_.begin(); found != textFonts_.end();) {
        if (found->first.session == session) found = textFonts_.erase(found);
        else ++found;
    }
    for (auto found = graphicsFonts_.begin(); found != graphicsFonts_.end();) {
        if (found->first.session == session) found = graphicsFonts_.erase(found);
        else ++found;
    }
    frozenWindows_.erase(std::remove_if(frozenWindows_.begin(), frozenWindows_.end(),
        [this, session](WindowId id) {
            return manager_.windowState(id).owner == session;
        }), frozenWindows_.end());
    if (frozenWindows_.empty()) std::vector<WindowId>().swap(frozenWindows_);
    manager_.closeSession(session);
    // Recompose only after releasing the canvases, so a low-memory recovery
    // path has the best chance of refreshing the physical display as well.
    presentRegion(dirty);
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
    return windowInfo(session, 1);
}

WindowInfo WindowedService::windowInfo(SessionId session, ChannelId channel) {
    const auto id = ensureWindow(session, channel);
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
    return paletteColor(session, 1, index);
}

RgbColor WindowedService::paletteColor(SessionId session, ChannelId channel,
                                       std::uint16_t index) {
    const auto& window = canvas(session, channel);
    if (index >= window.paletteSize()) throw std::out_of_range("palette index out of range");
    return defaultPaletteColor(index);
}

void WindowedService::setWindowBounds(SessionId session, Rect bounds) {
    setWindowBounds(session, 1, bounds);
}

void WindowedService::setWindowBounds(SessionId session, ChannelId channel, Rect bounds) {
    const SurfaceKey key{session, channel};
    const auto found = sessionWindows_.find(key);
    if (found == sessionWindows_.end()) {
        if (session == 0) throw std::invalid_argument("graphics session must be valid");
        // A completed session's last image is retained until the next session
        // draws. WINDOW creates the first canvas through this path, so reclaim
        // frozen canvases here just as ensureWindow() does for drawing commands.
        discardFrozenWindows();
        discardPresentationBuffer();
        const auto id = manager_.create(session, bounds, PixelDepth::Four);
        sessionWindows_.emplace(key, id);
        const auto state = manager_.windowState(id);
        if (channel == 0 && consoleBoundsCallback_) consoleBoundsCallback_(state.screenClip);
        presentRegion(state.screenClip);
        return;
    }
    const auto id = found->second;
    const auto oldBounds = manager_.windowState(id).screenClip;
    const auto current = manager_.windowState(id).bounds;
    if (bounds.width != current.width || bounds.height != current.height)
        discardPresentationBuffer();
    manager_.setBounds(id, bounds);
    const auto state = manager_.windowState(id);
    if (channel == 0 && consoleBoundsCallback_) consoleBoundsCallback_(state.screenClip);
    const auto newBounds = state.screenClip;
    presentRegion(unite(oldBounds, newBounds));
}

void WindowedService::setTaskViewport(SessionId session, TaskViewport viewport) {
    const auto oldViewport = manager_.taskViewport(session);
    const Rect oldScreen{oldViewport.originX, oldViewport.originY,
                         oldViewport.canvasWidth, oldViewport.canvasHeight};
    manager_.setTaskViewport(session, viewport);
    const Rect newScreen{viewport.originX, viewport.originY,
                         viewport.canvasWidth, viewport.canvasHeight};
    presentRegion(unite(intersect(oldScreen,
                                  {0, 0, manager_.screenWidth(), manager_.screenHeight()}),
                        intersect(newScreen,
                                  {0, 0, manager_.screenWidth(), manager_.screenHeight()})));
    for (const auto& [key, id] : sessionWindows_) {
        if (key.session == session && key.channel == 0 && consoleBoundsCallback_)
            consoleBoundsCallback_(manager_.windowState(id).screenClip);
    }
}

TaskViewport WindowedService::taskViewport(SessionId session) const {
    return manager_.taskViewport(session);
}

void WindowedService::setRotation(SessionId session, std::uint16_t degrees) {
    if (degrees != 0 && degrees != 90 && degrees != 180 && degrees != 270)
        throw std::invalid_argument("rotation must be 0, 90, 180, or 270 degrees");
    (void)ensureWindow(session, 1);
    const auto oldWidth = manager_.screenWidth();
    const auto oldHeight = manager_.screenHeight();
    if (!presenter_)
        throw std::runtime_error("display rotation unsupported");
    const auto display = presenter_->setRotation(degrees);
    if (display.width == 0 || display.height == 0)
        throw std::runtime_error("display presenter returned invalid dimensions");
    manager_.setScreenSize(display.width, display.height);
    for (const auto& [key, id] : sessionWindows_) {
        (void)key;
        const auto state = manager_.windowState(id);
        if (state.bounds.x == 0 && state.bounds.y == 0 &&
            state.bounds.width == oldWidth && state.bounds.height == oldHeight)
            manager_.setBounds(id, {0, 0, display.width, display.height});
    }
    rotationDegrees_ = degrees;
    presenter_->rotationApplied();
    presentIfConfigured();
}

void WindowedService::setDebugOverlayEnabled(bool enabled) {
    if (debugOverlayEnabled_ == enabled) return;
    debugOverlayEnabled_ = enabled;
    invalidateSystemRegion({0, 0, manager_.screenWidth(), 8});
}

void WindowedService::setDebugOverlayText(const std::string& text) {
    if (debugOverlayText_ == text) return;
    debugOverlayText_ = text;
    if (debugOverlayEnabled_)
        invalidateSystemRegion({0, 0, manager_.screenWidth(), 8});
}

void WindowedService::setWindowPixelDepth(SessionId session, PixelDepth depth) {
    discardPresentationBuffer();
    const auto id = ensureWindow(session, 1);
    const auto dirty = manager_.windowState(id).screenClip;
    manager_.setPixelDepth(id, depth);
    presentRegion(dirty);
}

void WindowedService::setWindowVisible(SessionId session, bool visible) {
    const auto id = ensureWindow(session, 1);
    const auto dirty = manager_.windowState(id).screenClip;
    manager_.setVisible(id, visible);
    presentRegion(dirty);
}

void WindowedService::raiseWindow(SessionId session) {
    manager_.raise(ensureWindow(session, 1));
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
                composeDisplayRow(y, row);
            })) {
        return;
    }
    frame_.width = manager_.screenWidth();
    frame_.height = manager_.screenHeight();
    manager_.composeInto(frame_.pixels);
    std::vector<ColorIndex> row;
    for (std::uint32_t y = 0; y < frame_.height; ++y) {
        composeDisplayRow(y, row);
        std::copy(row.begin(), row.end(), frame_.pixels.begin() +
                  static_cast<std::size_t>(y) * frame_.width);
    }
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
                composeDisplayRegionRow(y,
                    static_cast<std::uint32_t>(region.x), region.width, row);
            }))
        return;
    present();
}

void WindowedService::composeDisplayRegionRow(std::uint32_t y, std::uint32_t x,
                                              std::uint32_t width,
                                              std::vector<ColorIndex>& row) const {
    manager_.composeRegionRowInto(y, x, width, row);
    if (consoleLayer_) consoleLayer_(y, x, row);
    if (uiLayer_) uiLayer_(y, x, row);
    if (!debugOverlayEnabled_ || y >= 8) return;
    std::fill(row.begin(), row.end(), static_cast<ColorIndex>(0));
    const auto glyphRow = y;
    const auto firstColumn = x / 6;
    const auto lastColumn = (x + width + 5) / 6;
    for (std::uint32_t column = firstColumn;
         column < lastColumn && column < debugOverlayText_.size(); ++column) {
        const auto glyph = classicGlyphColumns(debugOverlayText_[column]);
        const auto columnStart = column * 6;
        for (std::uint32_t glyphColumn = 0; glyphColumn < 5; ++glyphColumn) {
            if ((glyph[glyphColumn] & (1u << glyphRow)) == 0) continue;
            const auto screenX = columnStart + glyphColumn;
            if (screenX >= x && screenX < x + width)
                row[screenX - x] = 15;
        }
    }
}

void WindowedService::composeDisplayRow(std::uint32_t y,
                                        std::vector<ColorIndex>& row) const {
    composeDisplayRegionRow(y, 0, manager_.screenWidth(), row);
}

void WindowedService::invalidateSystemRegion(Rect region) {
    if (!presenter_) return;
    region = intersect(region,
        {0, 0, manager_.screenWidth(), manager_.screenHeight()});
    if (region.width == 0 || region.height == 0) return;
    const auto saved = updateMode_;
    updateMode_ = UpdateMode::Auto;
    try { presentRegion(region); }
    catch (...) { updateMode_ = saved; throw; }
    updateMode_ = saved;
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
