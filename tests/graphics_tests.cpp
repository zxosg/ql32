#include "ql32/interpreter.hpp"
#include "ql32/graphics/windowed_service.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <new>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::atomic<long> failAllocationAfter{-1};

bool shouldFailAllocation() noexcept {
    auto remaining = failAllocationAfter.load(std::memory_order_relaxed);
    while (remaining >= 0) {
        if (remaining == 0) {
            if (failAllocationAfter.compare_exchange_weak(
                    remaining, -1, std::memory_order_relaxed))
                return true;
        } else if (failAllocationAfter.compare_exchange_weak(
                       remaining, remaining - 1, std::memory_order_relaxed)) {
            return false;
        }
    }
    return false;
}
} // namespace

void* operator new(std::size_t size) {
    if (shouldFailAllocation()) throw std::bad_alloc();
    if (void* memory = std::malloc(size == 0 ? 1 : size)) return memory;
    throw std::bad_alloc();
}

void* operator new[](std::size_t size) {
    return ::operator new(size);
}

void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

namespace {

using namespace ql32::graphics;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct Event {
    std::string name;
    ql32::graphics::SessionId session;
    ql32::graphics::Point first{};
    ql32::graphics::Point second{};
    int a = 0;
    int b = 0;
    std::string text;
    ql32::graphics::ChannelId channel = 1;
    bool filled = false;
};

class CapturingPresenter final : public ql32::graphics::FramePresenter {
public:
    void present(const ql32::graphics::Frame& frame) override {
        lastFrame = frame;
        ++presentCount;
    }
    ql32::graphics::Frame lastFrame;
    std::size_t presentCount = 0;
};

class RecordingGraphics final : public ql32::graphics::Service {
public:
    void setInk(SessionId session, ColorIndex color) override {
        events.push_back({"INK", session, {}, {}, color});
    }
    void setInk(SessionId session, ChannelId channel, ColorIndex color) override {
        events.push_back({"INK", session, {}, {}, color, 0, {}, channel});
    }
    void setPaper(SessionId session, ColorIndex color) override {
        events.push_back({"PAPER", session, {}, {}, color});
    }
    void setPaper(SessionId session, ChannelId channel, ColorIndex color) override {
        events.push_back({"PAPER", session, {}, {}, color, 0, {}, channel});
    }
    void line(SessionId session, Point from, Point to) override {
        events.push_back({"LINE", session, from, to});
    }
    void line(SessionId session, ChannelId channel, Point from, Point to) override {
        line(session, from, to);
        events.back().channel = channel;
    }
    void pixel(SessionId session, ChannelId channel, Point position) override {
        events.push_back({"PLOT", session, position, {}, 0, 0, {}, channel});
    }
    void rectangle(SessionId session, Point topLeft, std::int32_t width,
                   std::int32_t height) override {
        events.push_back({"RECT", session, topLeft, {}, width, height});
    }
    void circle(SessionId session, Point center, std::int32_t radius) override {
        events.push_back({"CIRCLE", session, center, {}, radius});
    }
    void circle(SessionId session, ChannelId channel, Point center,
                std::int32_t radius) override {
        circle(session, center, radius);
        events.back().channel = channel;
    }
    void fillRectangle(SessionId session, ChannelId channel, Point topLeft,
                       std::int32_t width, std::int32_t height) override {
        events.push_back({"RECT", session, topLeft, {}, width, height, {}, channel, true});
    }
    void fillCircle(SessionId session, ChannelId channel, Point center,
                    std::int32_t radius) override {
        events.push_back({"CIRCLE", session, center, {}, radius, 0, {}, channel, true});
    }
    void text(SessionId session, Point topLeft, const std::string& value) override {
        events.push_back({"TEXT", session, topLeft, {}, 0, 0, value});
    }
    void writeText(SessionId session, ChannelId channel,
                   const std::string& value) override {
        events.push_back({"PRINT_TEXT", session, {}, {}, 0, 0, value, channel});
    }
    bool setTextPosition(SessionId session, ChannelId channel,
                         std::uint32_t row, std::uint32_t column) override {
        events.push_back({"AT", session,
            {static_cast<std::int32_t>(row), static_cast<std::int32_t>(column)},
            {}, 0, 0, {}, channel});
        return true;
    }
    void clear(SessionId session) override {
        events.push_back({"CLS", session});
    }
    void clear(SessionId session, ChannelId channel) override {
        events.push_back({"CLS", session, {}, {}, 0, 0, {}, channel});
    }
    void closeWindow(SessionId session) override {
        events.push_back({"CLOSEWINDOW", session});
    }
    void closeWindow(SessionId session, ChannelId channel) override {
        events.push_back({"CLOSEWINDOW", session, {}, {}, 0, 0, {}, channel});
    }
    void closeSession(SessionId session) override {
        events.push_back({"CLOSE_SESSION", session});
    }
    void setUpdateMode(SessionId session, UpdateMode mode) override {
        const char* name = mode == UpdateMode::Off ? "GUPDATE_OFF" :
                           mode == UpdateMode::On ? "GUPDATE_ON" : "GUPDATE_AUTO";
        events.push_back({name, session});
    }
    void update(SessionId session) override { events.push_back({"GUPDATE", session}); }
    WindowInfo windowInfo(SessionId) override { return info; }
    RgbColor paletteColor(SessionId, std::uint16_t index) override {
        if (index == 2) return {255, 0, 0};
        if (index == 7) return {255, 255, 255};
        throw std::out_of_range("palette index");
    }
    void setRotation(SessionId, std::uint16_t degrees) override {
        info.rotationDegrees = degrees;
        events.push_back({"ROTATE", 1, {}, {}, static_cast<int>(degrees)});
    }
    void setWindowBounds(SessionId session, Rect bounds) override {
        info.originX = bounds.x;
        info.originY = bounds.y;
        events.push_back({"WINDOW", session, {bounds.x, bounds.y}, {},
                          static_cast<int>(bounds.width),
                          static_cast<int>(bounds.height)});
    }
    void setWindowBounds(SessionId session, ChannelId channel, Rect bounds) override {
        if (channel == 1) setWindowBounds(session, bounds);
        else events.push_back({"WINDOW", session, {bounds.x, bounds.y}, {},
                              static_cast<int>(bounds.width),
                              static_cast<int>(bounds.height)});
        events.back().channel = channel;
    }

    std::vector<Event> events;
    WindowInfo info{320, 240, 16, 8, 90};
};

class ListingStorage final : public ql32::runtime::StorageService {
public:
    std::string readText(const std::string&) override { return {}; }
    void writeText(const std::string&, const std::string&) override {}
    std::vector<ql32::runtime::FileEntry> list(const std::string&) override {
        return {{"demo.bas", 0, false}};
    }
    bool exists(const std::string&) override { return false; }
    std::uintmax_t size(const std::string&) override { return 0; }
    void rename(const std::string&, const std::string&) override {}
    void remove(const std::string&) override {}
};

void testDrawingCallsAndSessionOwnership() {
    auto graphics = std::make_shared<RecordingGraphics>();
    std::ostringstream output;
    ql32::graphics::SessionId session = 0;
    {
        ql32::Interpreter interpreter({}, {}, {}, graphics);
        interpreter.execute(
            "INK 7\n"
            "PAPER 1\n"
            "LINE 1,2,30,40\n"
            "PLOT #1,3,4\n"
            "RECT 5,6,20,10\n"
            "RECT #1,7,8,6,5,1\n"
            "CIRCLE 50,60,8\n"
            "CIRCLE 60,70,9,1\n"
            "TEXT 2,3,\"hello\"\n"
            "CLS\n", output);
        require(graphics->events.size() == 10, "drawing calls were not dispatched");
        session = graphics->events.front().session;
        require(session != 0, "graphics calls used an invalid session");
        require(graphics->events[0].name == "INK" && graphics->events[0].a == 7,
                "INK arguments were not preserved");
        require(graphics->events[1].name == "PAPER" && graphics->events[1].a == 1,
                "PAPER arguments were not preserved");
        require(graphics->events[2].first.x == 1 && graphics->events[2].first.y == 2 &&
                    graphics->events[2].second.x == 30 && graphics->events[2].second.y == 40,
                "LINE coordinates were not preserved");
        require(graphics->events[3].name == "PLOT" && graphics->events[3].channel == 1 &&
                    graphics->events[3].first.x == 3 && graphics->events[3].first.y == 4,
                "PLOT coordinates were not preserved");
        require(graphics->events[4].a == 20 && graphics->events[4].b == 10 &&
                    !graphics->events[4].filled,
                "RECT dimensions were not preserved");
        require(graphics->events[5].name == "RECT" && graphics->events[5].channel == 1 &&
                    graphics->events[5].filled && graphics->events[5].a == 6 &&
                    graphics->events[5].b == 5,
                "channel-qualified filled RECT arguments were not preserved");
        require(graphics->events[6].a == 8 && !graphics->events[6].filled,
                "CIRCLE outline radius was not preserved");
        require(graphics->events[7].a == 9 && graphics->events[7].filled,
                "filled CIRCLE was not dispatched");
        require(graphics->events[8].text == "hello", "TEXT value was not preserved");
        require(graphics->events[9].name == "CLS", "CLS was not dispatched");
    }
    require(graphics->events.back().name == "CLOSE_SESSION" &&
                graphics->events.back().session == session,
            "interpreter destruction did not release its graphics session");
}

void testGraphicsUpdateCommandDispatch() {
    auto graphics = std::make_shared<RecordingGraphics>();
    std::ostringstream output;
    ql32::Interpreter interpreter({}, {}, {}, graphics);
    interpreter.execute(
        "CONST KGupdateOff = 0\n"
        "CONST KGupdateOn = 1\n"
        "CONST KGupdateAuto = 1\n"
        "GUPDATE KGupdateOff\n"
        "CIRCLE 2,3,1\n"
        "GUPDATE\n"
        "GUPDATE KGupdateOn\n"
        "GUPDATE KGupdateAuto\n", output);
    require(graphics->events.size() == 5,
            "GUPDATE or drawing command dispatch count was incorrect");
    require(graphics->events[0].name == "GUPDATE_OFF" &&
                graphics->events[1].name == "CIRCLE" &&
                graphics->events[2].name == "GUPDATE" &&
                graphics->events[3].name == "GUPDATE_AUTO" &&
                graphics->events[4].name == "GUPDATE_AUTO",
            "GUPDATE modes or explicit flush were dispatched in the wrong order");
}

void testWindowedGraphicsUpdateBatching() {
    auto presenter = std::make_shared<CapturingPresenter>();
    WindowedService graphics(10, 10, presenter);
    graphics.setUpdateMode(1, UpdateMode::Off);
    graphics.setInk(1, 2);
    graphics.circle(1, {4, 5}, 0);
    require(presenter->presentCount == 0,
            "drawing was presented while GUPDATE was OFF");

    graphics.update(1);
    require(presenter->presentCount == 1,
            "GUPDATE did not force one composed-screen presentation");
    require(presenter->lastFrame.pixels[5 * 10 + 4] == 2,
            "forced GUPDATE did not present retained drawing changes");

    graphics.circle(1, {6, 5}, 0);
    require(presenter->presentCount == 1,
            "manual GUPDATE unexpectedly enabled automatic updates");
    graphics.setUpdateMode(1, UpdateMode::On);
    require(presenter->presentCount == 2 && presenter->lastFrame.pixels[5 * 10 + 6] == 2,
            "GUPDATE ON did not restore updates and present pending drawing");

    graphics.setUpdateMode(1, UpdateMode::Off);
    graphics.circle(1, {8, 5}, 0);
    graphics.setUpdateMode(1, UpdateMode::Auto);
    require(presenter->presentCount == 3 && presenter->lastFrame.pixels[5 * 10 + 8] == 2,
            "GUPDATE AUTO did not restore default automatic presentation");
}

void testDrawingArgumentValidation() {
    auto graphics = std::make_shared<RecordingGraphics>();
    ql32::Interpreter interpreter({}, {}, {}, graphics);
    std::ostringstream output;
    bool invalidColor = false;
    try {
        interpreter.execute("INK 8\n", output);
    } catch (const std::runtime_error& error) {
        invalidColor = std::string(error.what()).find("color index") != std::string::npos;
    }
    require(invalidColor, "invalid palette index was accepted");
    bool invalidCoordinates = false;
    try {
        interpreter.execute("LINE 1,2,3.5,4\n", output);
    } catch (const std::runtime_error& error) {
        invalidCoordinates = std::string(error.what()).find("pixel coordinate") !=
                             std::string::npos;
    }
    require(invalidCoordinates, "non-integer pixel coordinate was accepted");
    bool invalidFill = false;
    try {
        interpreter.execute("RECT 1,2,3,4,2\n", output);
    } catch (const std::runtime_error& error) {
        invalidFill = std::string(error.what()).find("fill flag") != std::string::npos;
    }
    require(invalidFill, "RECT accepted a fill flag other than 0 or 1");
    bool missingArguments = false;
    try {
        interpreter.execute("CIRCLE 1,2,3,1,0\n", output);
    } catch (const std::runtime_error& error) {
        missingArguments = std::string(error.what()).find("CIRCLE expects") != std::string::npos;
    }
    require(missingArguments, "CIRCLE accepted too many arguments");
    require(graphics->events.empty(), "invalid drawing call reached the service");
}

void testClsChannelAndColorSyntax() {
    auto graphics = std::make_shared<RecordingGraphics>();
    ql32::Interpreter interpreter({}, {}, {}, graphics);
    std::ostringstream output;
    interpreter.execute(
        "CLS\n"
        "CLS #0\n"
        "CLS #1,2,3\n"
        "CLS 4,5\n"
        "CHANNEL #0\n"
        "CLS 6,7\n"
        "CLOSEWINDOW #0\n", output);
    require(graphics->events.size() == 12 &&
                graphics->events[0].name == "CLS" && graphics->events[0].channel == 1 &&
                graphics->events[1].name == "CLS" && graphics->events[1].channel == 0 &&
                graphics->events[2].name == "INK" && graphics->events[2].channel == 1 &&
                graphics->events[2].a == 2 &&
                graphics->events[3].name == "PAPER" && graphics->events[3].channel == 1 &&
                graphics->events[3].a == 3 &&
                graphics->events[4].name == "CLS" && graphics->events[4].channel == 1 &&
                graphics->events[5].name == "INK" && graphics->events[5].channel == 1 &&
                graphics->events[5].a == 4 &&
                graphics->events[6].name == "PAPER" && graphics->events[6].channel == 1 &&
                graphics->events[6].a == 5 &&
                graphics->events[7].name == "CLS" && graphics->events[7].channel == 1 &&
                graphics->events[8].name == "INK" && graphics->events[8].channel == 0 &&
                graphics->events[8].a == 6 &&
                graphics->events[9].name == "PAPER" && graphics->events[9].channel == 0 &&
                graphics->events[9].a == 7 &&
                graphics->events[10].name == "CLS" && graphics->events[10].channel == 0 &&
                graphics->events[11].name == "CLOSEWINDOW" &&
                graphics->events[11].channel == 0,
            "CLS channel selection or optional ink/paper colors were incorrect");
}

void testClsColorArgumentsClearWithNewPaper() {
    auto presenter = std::make_shared<CapturingPresenter>();
    auto graphics = std::make_shared<WindowedService>(12, 10, presenter);
    ql32::Interpreter interpreter({}, {}, {}, graphics);
    std::ostringstream output;
    interpreter.execute("INK 1\nPLOT 3,4\nCLS 2,3\n", output);
    const auto states = graphics->windowManager().compositionOrder();
    require(states.size() == 1, "CLS did not create the default graphics window");
    const auto& canvas = graphics->windowManager().canvas(states.front().id);
    require(canvas.ink() == 2 && canvas.paper() == 3 &&
                canvas.pixel(3, 4) == 3 && canvas.pixel(0, 0) == 3,
            "CLS ink,paper did not set drawing colors and clear to the paper color");
}

void testPixelAndFilledShapesRenderOnRetainedCanvas() {
    auto presenter = std::make_shared<CapturingPresenter>();
    auto graphics = std::make_shared<WindowedService>(16, 16, presenter);
    ql32::Interpreter interpreter({}, {}, {}, graphics);
    std::ostringstream output;
    interpreter.execute(
        "INK 2\n"
        "PLOT 1,1\n"
        "RECT 2,2,5,5,1\n"
        "RECT 2,8,5,5,0\n"
        "CIRCLE 10,5,2,1\n"
        "CIRCLE 10,12,2\n", output);
    const auto& frame = presenter->lastFrame;
    const auto pixel = [&](std::uint32_t x, std::uint32_t y) {
        return frame.pixels[static_cast<std::size_t>(y) * frame.width + x];
    };
    require(pixel(1, 1) == 2, "PLOT did not set one pixel to the current ink");
    require(pixel(4, 4) == 2, "filled RECT did not draw its interior");
    require(pixel(4, 8) == 2 && pixel(4, 9) == 0,
            "RECT outline and fill=0 behavior were incorrect");
    require(pixel(10, 5) == 2 && pixel(10, 12) == 0,
            "filled and outline CIRCLE behavior was incorrect");
}

void testDisplayCommands() {
    auto graphics = std::make_shared<RecordingGraphics>();
    ql32::Interpreter interpreter({}, {}, {}, graphics);
    std::ostringstream output;
    interpreter.execute(
        "PRINT #0,GWIDTH()\nPRINT #0,GHEIGHT()\nPRINT #0,GDEPTH()\n"
        "PRINT #0,GROTATION()\nPRINT #0,GPALETTESIZE()\nPRINT #0,GPALETTE$(2)\n"
        "ROTATE 270\nPRINT #0,GROTATION()\n", output);
    require(output.str() == "320\n240\n16\n90\n8\n#FF0000\n270\n",
            "display queries or rotation command returned incorrect values");
    bool invalidPalette = false;
    try {
        interpreter.execute("PRINT #0,GPALETTE$(8)\n", output);
    } catch (const std::runtime_error& error) {
        invalidPalette = std::string(error.what()).find("palette index") != std::string::npos;
    }
    require(invalidPalette, "out-of-range palette index was accepted");

    bool parenthesizedCommandRejected = false;
    try {
        interpreter.execute("ROTATE(90)\n", output);
    } catch (const std::runtime_error&) {
        parenthesizedCommandRejected = true;
    }
    require(parenthesizedCommandRejected,
            "built-in command accepted function-call parentheses");
    bool functionParenthesesRequired = false;
    try {
        interpreter.execute("PRINT GWIDTH\n", output);
    } catch (const std::runtime_error& error) {
        functionParenthesesRequired = std::string(error.what()).find("requires parentheses") !=
                                      std::string::npos;
    }
    require(functionParenthesesRequired,
            "built-in function was accepted without parentheses");
}

void testWindowPositionCommand() {
    auto graphics = std::make_shared<RecordingGraphics>();
    ql32::Interpreter interpreter({}, {}, {}, graphics);
    std::ostringstream output;
    interpreter.execute("WINDOW 21,34,123,95\n", output);
    require(graphics->events.size() == 1 && graphics->events[0].name == "WINDOW",
            "WINDOW command was not dispatched");
    const auto& event = graphics->events[0];
    require(event.first.x == 21 && event.first.y == 34 &&
                event.a == 123 && event.b == 95,
            "WINDOW bounds were not preserved");
    interpreter.execute("WINDOW #0,7,8,80,60\n", output);
    require(graphics->events.back().name == "WINDOW" &&
                graphics->events.back().channel == 0 &&
                graphics->events.back().first.x == 7 &&
                graphics->events.back().first.y == 8 &&
                graphics->events.back().a == 80 && graphics->events.back().b == 60,
            "channel-qualified WINDOW did not target channel #0");
    interpreter.execute("PRINT #0,GORIGINX()\nPRINT #0,GORIGINY()\n", output);
    require(output.str() == "21\n34\n",
            "window screen-origin queries returned incorrect coordinates");

    bool invalidSize = false;
    try {
        interpreter.execute("WINDOW 0,0,0,100\n", output);
    } catch (const std::runtime_error& error) {
        invalidSize = std::string(error.what()).find("window size") != std::string::npos;
    }
    require(invalidSize, "zero-width window was accepted");

    bool parenthesizedCommandRejected = false;
    try {
        interpreter.execute("WINDOW(1,2,30,40)\n", output);
    } catch (const std::runtime_error&) {
        parenthesizedCommandRejected = true;
    }
    require(parenthesizedCommandRejected,
            "WINDOW accepted function-call parentheses");
}

void testPrintChannelRouting() {
    auto graphics = std::make_shared<RecordingGraphics>();
    ql32::Interpreter interpreter({}, {}, {}, graphics);
    std::ostringstream console;
    interpreter.execute("PRINT #1,\"hello\"\nPRINT #0,\"console\"\n", console);
    require(console.str() == "console\n", "PRINT #0 did not target the console");
    const auto windowText = std::find_if(graphics->events.begin(), graphics->events.end(),
        [](const Event& event) { return event.name == "TEXT" && event.text == "hello"; });
    require(windowText != graphics->events.end(), "PRINT #1 did not target the user window");
    interpreter.execute("PRINT #0;12\n", console);
    require(console.str() == "console\n12\n",
            "semicolon after a PRINT channel selector was not accepted");

    graphics->events.clear();
    interpreter.execute("PRINT \"default\"\n", console);
    require(std::find_if(graphics->events.begin(), graphics->events.end(),
        [](const Event& event) { return event.name == "TEXT" && event.text == "default"; }) !=
        graphics->events.end(), "unqualified PRINT did not target channel #1");

    graphics->events.clear();
    interpreter.execute("PRINT #1,INK 3,PAPER 6,\"styled\"\n", console);
    require(graphics->events.size() >= 3 &&
            graphics->events[0].name == "INK" && graphics->events[0].a == 3 &&
            graphics->events[1].name == "PAPER" && graphics->events[1].a == 6 &&
            std::any_of(graphics->events.begin(), graphics->events.end(),
                [](const Event& event) {
                    return event.name == "TEXT" && event.text == "styled";
                }),
            "PRINT leading INK/PAPER options were not applied before its text");

    auto presenter = std::make_shared<CapturingPresenter>();
    auto window = std::make_shared<WindowedService>(60, 24, presenter);
    ql32::Interpreter windowInterpreter({}, {}, {}, window);
    std::ostringstream windowConsole;
    windowInterpreter.execute("PRINT #1,\"A\"\nPRINT #1,\"B\"\n", windowConsole);
    const auto hasInk = [&](std::uint32_t top, std::uint32_t bottom) {
        for (std::uint32_t y = top; y < bottom; ++y)
            for (std::uint32_t x = 0; x < presenter->lastFrame.width; ++x)
                if (presenter->lastFrame.pixels[static_cast<std::size_t>(y) *
                        presenter->lastFrame.width + x] != 0) return true;
        return false;
    };
    require(windowConsole.str().empty() && hasInk(0, 8) && hasInk(8, 16),
            "window PRINT did not draw consecutive lines to the retained canvas");
}

void testChannelDefaultRoutingAndGraphicsSelectors() {
    auto graphics = std::make_shared<RecordingGraphics>();
    ql32::Interpreter interpreter({}, {}, {}, graphics);
    std::ostringstream console;
    interpreter.execute(
        "CHANNEL #0\n"
        "PRINT 7\n"
        "PRINT #1,8\n"
        "CHANNEL #1\n"
        "CIRCLE #1,10,20,5\n", console);
    require(console.str() == "7\n", "CHANNEL did not change the default output to #0");
    require(std::find_if(graphics->events.begin(), graphics->events.end(),
        [](const Event& event) { return event.name == "TEXT" && event.text == "8"; }) !=
        graphics->events.end(), "explicit PRINT #1 did not override the default channel");
    require(std::find_if(graphics->events.begin(), graphics->events.end(),
        [](const Event& event) {
            return event.name == "CIRCLE" && event.first.x == 10 && event.first.y == 20;
        }) != graphics->events.end(), "CIRCLE #1 was not dispatched to the graphics window");

    interpreter.execute("CIRCLE #0,1,2,3\n", console);
    require(std::find_if(graphics->events.begin(), graphics->events.end(),
        [](const Event& event) {
            return event.name == "CIRCLE" && event.channel == 0 &&
                   event.first.x == 1 && event.first.y == 2;
        }) != graphics->events.end(), "CIRCLE #0 was not dispatched to the console window");

    auto presenter = std::make_shared<CapturingPresenter>();
    auto windowed = std::make_shared<WindowedService>(100, 80, presenter);
    ql32::Interpreter windowInterpreter({}, {}, {}, windowed);
    std::ostringstream windowConsole;
    windowInterpreter.execute(
        "WINDOW #0,5,6,30,20\nRECT #0,0,0,12,12\n"
        "WINDOW #1,50,6,30,20\nRECT #1,0,0,12,12\n",
        windowConsole);
    const auto windowStates = windowed->windowManager().compositionOrder();
    const auto owner = windowStates.empty() ? 0 : windowStates.front().owner;
    const auto zeroWindow = windowed->windowInfo(owner, 0);
    const auto oneWindow = windowed->windowInfo(owner, 1);
    require(windowed->windowManager().size() == 2 &&
                zeroWindow.originX == 5 && zeroWindow.originY == 6 &&
                oneWindow.originX == 50 && oneWindow.originY == 6,
            "channel windows did not retain independent bounds");

    ql32::Interpreter routed({}, {}, {}, graphics);
    std::ostringstream routedConsole;
    routed.execute("CHANNEL #1 TO #0\nPRINT \"default route\"\n"
                   "PRINT #1,\"explicit route\"\n", routedConsole);
    require(routedConsole.str() == "default route\nexplicit route\n",
            "CHANNEL route did not redirect default and explicit output");
    routed.execute("CHANNEL #0 TO #1\n"
                   "PRINT #0,GETCHANNEL(2,#1)\n"
                   "PRINT #1,\"same endpoint\"\n", routedConsole);
    require(routedConsole.str() ==
                "default route\nexplicit route\nCONSOLE\nsame endpoint\n",
            "direct endpoint binding or active endpoint inspection failed");
}

void testListChannelRouting() {
    auto graphics = std::make_shared<RecordingGraphics>();
    ql32::Interpreter interpreter({}, {}, {}, graphics);
    std::ostringstream console;
    interpreter.executeInteractive("10 PRINT \"stored\"\n", console);
    interpreter.executeInteractive("LIST #1\n", console);
    require(console.str().empty(), "LIST #1 wrote the listing to the console stream");
    const auto windowListing = std::find_if(graphics->events.begin(), graphics->events.end(),
        [](const Event& event) {
            return event.name == "TEXT" && event.text == "10 PRINT \"stored\"\n";
        });
    require(windowListing != graphics->events.end(), "LIST #1 did not write to the user window");

    interpreter.executeInteractive("LIST #0\n", console);
    require(console.str() == "10 PRINT \"stored\"\n",
            "LIST #0 did not write to the console stream");

    graphics->events.clear();
    console.str({});
    console.clear();
    interpreter.executeInteractive("LIST\n", console);
    require(console.str().empty() && std::find_if(graphics->events.begin(), graphics->events.end(),
        [](const Event& event) {
            return event.name == "TEXT" && event.text == "10 PRINT \"stored\"\n";
        }) != graphics->events.end(), "bare LIST did not use default output channel #1");

    auto storage = std::make_shared<ListingStorage>();
    auto directoryInterpreter = std::make_unique<ql32::Interpreter>(storage, nullptr, nullptr,
                                                                    graphics);
    graphics->events.clear();
    directoryInterpreter->executeInteractive("DIR\n", console);
    require(console.str().empty() && std::find_if(graphics->events.begin(), graphics->events.end(),
        [](const Event& event) { return event.name == "TEXT" && event.text == "demo.bas\n"; }) !=
        graphics->events.end(), "DIR did not use default output channel #1");
}

void testWindowMovementRestoresCoveredContent() {
    using namespace ql32::graphics;
    auto presenter = std::make_shared<CapturingPresenter>();
    WindowedService graphics(100, 80, presenter);
    constexpr SessionId background = 101;
    constexpr SessionId foreground = 202;

    graphics.setPaper(background, 1);
    graphics.clear(background);
    graphics.setPaper(foreground, 2);
    graphics.clear(foreground);
    graphics.setWindowBounds(foreground, {20, 15, 30, 25});

    auto pixel = [&](std::uint32_t x, std::uint32_t y) {
        return presenter->lastFrame.pixels[static_cast<std::size_t>(y) * 100 + x];
    };
    require(pixel(25, 20) == 2, "foreground window did not cover background pixels");
    require(pixel(10, 10) == 1, "background window was lost outside overlap");

    graphics.setWindowBounds(foreground, {60, 35, 30, 25});
    require(pixel(25, 20) == 1,
            "moving the covering window did not restore retained background pixels");
    require(pixel(65, 40) == 2,
            "moved window did not retain its own canvas contents");
    const auto info = graphics.windowInfo(foreground);
    require(info.originX == 60 && info.originY == 35,
            "window info did not expose absolute screen coordinates");
}

void testCanvasReshapeReusesFullScreenStorage() {
    using namespace ql32::graphics;
    WindowManager manager(240, 320);
    const auto id = manager.create(1, {0, 0, 240, 320}, PixelDepth::Four);
    auto& canvas = manager.canvas(id);
    canvas.setPaper(5);
    canvas.clear();
    canvas.setPixel(0, 0, 1);
    canvas.setPixel(239, 239, 2);
    canvas.setPixel(0, 319, 3);

    manager.setBounds(id, {0, 0, 320, 240});
    require(canvas.width() == 320 && canvas.height() == 240,
            "canvas did not reshape to landscape dimensions");
    require(canvas.pixel(0, 0) == 1 && canvas.pixel(239, 239) == 2,
            "landscape reshape lost pixels in the retained upper-left area");
    require(canvas.pixel(319, 10) == 5,
            "landscape reshape did not fill newly exposed pixels with paper");

    manager.setBounds(id, {0, 0, 240, 320});
    require(canvas.width() == 240 && canvas.height() == 320,
            "canvas did not reshape back to portrait dimensions");
    require(canvas.pixel(0, 0) == 1 && canvas.pixel(239, 239) == 2,
            "portrait reshape lost retained pixels");
    require(canvas.pixel(0, 319) == 5,
            "portrait reshape did not fill newly exposed pixels with paper");
}

void testCloseWindowReleasesCanvasAndAllowsReuse() {
    using namespace ql32::graphics;
    auto presenter = std::make_shared<CapturingPresenter>();
    WindowedService graphics(20, 10, presenter);
    graphics.setPaper(11, 2);
    graphics.clear(11);
    require(graphics.windowManager().size() == 1,
            "graphics command did not create a retained window");

    graphics.closeWindow(11);
    require(graphics.windowManager().size() == 0,
            "CLOSEWINDOW did not release the window");
    require(!presenter->lastFrame.pixels.empty() && presenter->lastFrame.pixels[0] == 0,
            "closing the final window did not clear its displayed area");

    graphics.setPaper(22, 1);
    graphics.clear(22);
    require(graphics.windowManager().size() == 1,
            "released canvas budget could not be reused");
}

void testCompletedSessionRemainsAvailableForCapture() {
    using namespace ql32::graphics;
    WindowedService graphics(8, 8, nullptr);
    graphics.setInk(31, 2);
    graphics.pixel(31, {2, 2});
    graphics.setInk(31, 4);
    graphics.pixel(31, {3, 3});

    graphics.closeSession(31);
    std::vector<ColorIndex> row(8);
    graphics.composeDisplayRow(2, row);
    require(row[2] == 2,
            "completed session graphics disappeared before screen capture");
    graphics.composeDisplayRow(3, row);
    require(row[3] == 4,
            "completed session did not retain all drawn colors for capture");

    graphics.setInk(32, 6);
    graphics.pixel(32, {4, 4});
    graphics.composeDisplayRow(2, row);
    require(row[2] == 0,
            "starting a new drawing session did not discard the frozen image");
    graphics.composeDisplayRow(4, row);
    require(row[4] == 6,
            "new graphics session did not draw after frozen image cleanup");
}

void testNewWindowCommandReclaimsFrozenCanvasStorage() {
    using namespace ql32::graphics;
    WindowedService graphics(16, 8, nullptr);

    graphics.setInk(41, 3);
    graphics.pixel(41, {2, 2});
    graphics.closeSession(41);
    require(graphics.windowManager().size() == 1,
            "completed session canvas was not retained for capture");

    // BASIC WINDOW creates a new session's first canvas directly, rather than
    // going through ensureWindow(). It must release the frozen canvas first.
    graphics.setWindowBounds(42, {0, 0, 16, 8});
    require(graphics.windowManager().size() == 1,
            "WINDOW did not reclaim the previous session's frozen canvas");
    const auto info = graphics.windowInfo(42);
    require(info.width == 16 && info.height == 8,
            "new WINDOW command did not receive the reclaimed canvas budget");
}

void testDiscardSessionReleasesActiveAndFrozenWindowsByOwner() {
    using namespace ql32::graphics;
    auto presenter = std::make_shared<CapturingPresenter>();
    WindowedService graphics(24, 8, presenter);
    graphics.setInk(51, 3);
    graphics.pixel(51, {1, 1});
    graphics.setInk(52, 6);
    graphics.pixel(52, {2, 2});
    graphics.closeSession(51); // Frozen completed output is still discardable.
    require(graphics.windowManager().size() == 2,
            "setup did not retain the completed session canvas");

    graphics.discardSession(51);
    require(graphics.windowManager().size() == 1,
            "discard did not release the selected frozen canvas");
    std::vector<ColorIndex> row(24);
    graphics.composeDisplayRow(1, row);
    require(row[1] == 0,
            "discarded frozen canvas remained available to screen capture");
    graphics.composeDisplayRow(2, row);
    require(row[2] == 6,
            "discarding one session removed another session's canvas");
    require(presenter->lastFrame.pixels[2 * 24 + 2] == 6,
            "discard did not refresh the display with the remaining window");

    graphics.discardSession(52);
    require(graphics.windowManager().size() == 0,
            "discard did not release an active session canvas");
    require(presenter->lastFrame.pixels[2 * 24 + 2] == 0,
            "discard did not clear the released window from the display");
}

void testRepeatedWindowSessionBurstsReleaseAndRecreate() {
    WindowedService graphics(64, 32, nullptr);
    SessionId nextSession = 100;
    for (int cycle = 0; cycle < 4; ++cycle) {
        std::array<SessionId, 12> sessions{};
        for (auto& session : sessions) {
            session = nextSession++;
            graphics.setTaskViewport(session, {0, 0, 64, 32});
            graphics.setInk(session, static_cast<ColorIndex>(session % 16));
            graphics.pixel(session, {cycle, cycle});
        }
        require(graphics.windowManager().size() == sessions.size(),
                "window burst did not create every session window");
        for (const auto session : sessions) graphics.discardSession(session);
        require(graphics.windowManager().size() == 0,
                "discard left a window after a session burst");

        const auto survivor = nextSession++;
        graphics.setTaskViewport(survivor, {0, 0, 64, 32});
        graphics.setInk(survivor, 7);
        graphics.pixel(survivor, {1, 1});
        const auto id = graphics.windowForSession(survivor);
        require(graphics.windowManager().size() == 1 &&
                    graphics.windowManager().canvas(id).pixel(1, 1) == 7,
                "window manager failed to create a fresh window after cleanup");
        graphics.discardSession(survivor);
    }
}

void testDebugOverlayComposesAboveGraphics() {
    using namespace ql32::graphics;
    auto presenter = std::make_shared<CapturingPresenter>();
    WindowedService graphics(24, 16, presenter);
    graphics.setInk(51, 7);
    graphics.fillRectangle(51, {0, 0}, 24, 16);
    graphics.setDebugOverlayText("A");
    graphics.setDebugOverlayEnabled(true);

    require(presenter->lastFrame.pixels.size() == 24 * 16,
            "debug overlay did not present the composed screen");
    require(presenter->lastFrame.pixels[8] == 0,
            "debug overlay did not cover application graphics across the status row");
    require(presenter->lastFrame.pixels[8 * 24] == 7,
            "debug overlay covered application graphics below its single text row");
    bool glyphVisible = false;
    for (std::size_t y = 0; y < 7; ++y)
        for (std::size_t x = 0; x < 5; ++x)
            glyphVisible = glyphVisible || presenter->lastFrame.pixels[y * 24 + x] == 15;
    require(glyphVisible, "debug overlay text glyph was not composed");
}

void testCanvasStorageReleasesAndReusesAllocations() {
    using namespace ql32::graphics;
    WindowManager manager(16, 8);
    const auto first = manager.create(1, {0, 0, 4, 4}, PixelDepth::Four);
    const auto middle = manager.create(2, {4, 0, 4, 4}, PixelDepth::Four);
    const auto last = manager.create(3, {8, 0, 4, 4}, PixelDepth::Four);
    manager.canvas(first).setPixel(0, 0, 3);
    manager.canvas(middle).setPixel(1, 1, 6);
    manager.canvas(last).setPixel(2, 2, 9);

    manager.close(middle);
    require(manager.canvas(first).pixel(0, 0) == 3 &&
                manager.canvas(last).pixel(2, 2) == 9,
            "canvas arena compaction changed a live canvas");

    const auto reused = manager.create(4, {12, 0, 4, 4}, PixelDepth::Four);
    manager.canvas(reused).setPixel(3, 3, 12);
    require(manager.canvas(reused).pixel(3, 3) == 12,
            "canvas arena did not reuse released canvas space");
    std::vector<WindowId> additional;
    for (SessionId owner = 5; owner < 15; ++owner)
        additional.push_back(manager.create(owner, {0, 4, 1, 1}, PixelDepth::Four));
    require(manager.size() == 13,
            "window manager retained a fixed window-count or canvas-byte cap");
    for (const auto window : additional) manager.close(window);

    manager.close(reused);
    manager.setPixelDepth(last, PixelDepth::Eight);
    require(manager.canvas(last).pixel(2, 2) == 9,
            "canvas depth change lost pixels in the arena");
}

void testTaskViewportTranslatesAndClipsOwnedWindows() {
    using namespace ql32::graphics;
    WindowManager manager(8, 6);
    const auto background = manager.create(9, {0, 0, 8, 6});
    manager.canvas(background).setPaper(3);
    manager.canvas(background).clear();

    const auto first = manager.create(41, {1, 1, 6, 4});
    auto& firstCanvas = manager.canvas(first);
    firstCanvas.setPaper(2);
    firstCanvas.clear();
    firstCanvas.setPixel(1, 0, 8);
    firstCanvas.setPixel(5, 3, 12);

    const auto second = manager.create(41, {6, 0, 3, 3});
    manager.canvas(second).setPaper(5);
    manager.canvas(second).clear();
    manager.canvas(second).setPixel(0, 0, 14);

    require(manager.taskViewport(41).canvasWidth == 8 &&
                manager.taskViewport(41).canvasHeight == 6,
            "new task viewport did not default to the physical display");
    manager.setTaskViewport(41, {2, 1, 3, 2});
    const auto firstState = manager.windowState(first);
    require(firstState.bounds.x == 3 && firstState.bounds.y == 2 &&
                firstState.screenClip.x == 3 && firstState.screenClip.y == 2 &&
                firstState.screenClip.width == 2 && firstState.screenClip.height == 1,
            "task origin did not translate and clip its window bounds");
    require(manager.windowState(second).screenClip.width == 0,
            "window extending outside the task viewport was not clipped");

    manager.setTaskViewport(41, {2, 1, 3, 2, 1, 0});
    const auto offsetState = manager.windowState(first);
    require(offsetState.bounds.x == 4 && offsetState.bounds.y == 2 &&
                offsetState.screenClip.x == 4 && offsetState.screenClip.y == 2 &&
                offsetState.screenClip.width == 1 &&
                offsetState.screenClip.height == 1,
            "task display offsets did not move content inside the fixed viewport");

    auto frame = manager.compose();
    require(frame[2 * 8 + 4] == 2 &&
                frame[1 * 8 + 2] == 3,
            "task clipping changed window-local pixels or covered unrelated pixels");

    manager.setTaskViewport(41, {-1, 0, 8, 6});
    frame = manager.compose();
    require(frame[1 * 8 + 1] == 8 && frame[4 * 8 + 5] == 12 &&
                frame[5] == 14 && frame[5 * 8 + 7] == 3,
            "expanding/moving the task viewport did not reveal retained pixels or preserve other tasks");
}

void testWindowedServiceTaskViewportRedrawAndAbsoluteOrigin() {
    using namespace ql32::graphics;
    auto presenter = std::make_shared<CapturingPresenter>();
    WindowedService graphics(8, 6, presenter);
    const auto background = graphics.windowManager().create(99, {0, 0, 8, 6});
    graphics.windowManager().canvas(background).setPaper(3);
    graphics.windowManager().canvas(background).clear();

    graphics.setWindowBounds(77, 1, {1, 1, 6, 4});
    const auto window = graphics.windowForSession(77);
    auto& canvas = graphics.windowManager().canvas(window);
    canvas.setPaper(2);
    canvas.clear();
    canvas.setPixel(1, 0, 8);
    graphics.present();

    graphics.setTaskViewport(77, {2, 1, 3, 2});
    const auto info = graphics.windowInfo(77);
    require(info.originX == 3 && info.originY == 2,
            "GORIGINX/Y did not return the translated absolute window position");
    require(presenter->lastFrame.pixels[1 * 8 + 1] == 3 &&
                presenter->lastFrame.pixels[2 * 8 + 4] == 8 &&
                presenter->lastFrame.pixels[2 * 8 + 5] == 3,
            "moving the task viewport did not redraw old and clipped new regions");

    graphics.closeSession(77);
    require(graphics.taskViewport(77).canvasWidth == 3,
            "completed task lost its viewport while its frozen canvas remained visible");
    graphics.clear(88);
    require(graphics.taskViewport(77).canvasWidth == 8 &&
                graphics.taskViewport(77).canvasHeight == 6,
            "reclaiming a frozen canvas did not release its task viewport");
}

void testCanvasResizePreservesPixelsAcrossOddWidthGrowAndShrink() {
    using namespace ql32::graphics;
    WindowManager manager(16, 10);
    const auto first = manager.create(1, {0, 0, 3, 3}, PixelDepth::Four);
    const auto target = manager.create(2, {3, 0, 3, 3}, PixelDepth::Four);
    const auto third = manager.create(3, {6, 0, 3, 3}, PixelDepth::Four);

    manager.canvas(first).setPixel(1, 1, 2);
    manager.canvas(third).setPixel(2, 2, 12);
    auto& canvas = manager.canvas(target);
    canvas.setPaper(6);
    canvas.clear();
    canvas.setPixel(0, 0, 1);
    canvas.setPixel(2, 1, 11);

    std::array<CanvasResizePhase, 3> phases{};
    std::size_t observed = 0;
    bool byteCountsMatch = true;
    manager.setCanvasResizeObserver(
        [&](CanvasResizePhase phase, std::size_t oldBytes, std::size_t newBytes) {
            if (observed < phases.size()) phases[observed] = phase;
            ++observed;
            byteCountsMatch = byteCountsMatch && oldBytes == 5 && newBytes == 13;
        });
    manager.setBounds(target, {3, 0, 5, 5});
    manager.setCanvasResizeObserver({});
    require(observed == 3 && byteCountsMatch &&
                phases[0] == CanvasResizePhase::BeforeAllocation &&
                phases[1] == CanvasResizePhase::BothBuffersLive &&
                phases[2] == CanvasResizePhase::AfterSwap,
            "resize telemetry did not report the three allocation phases");
    require(canvas.width() == 5 && canvas.height() == 5,
            "odd-width grow did not update canvas dimensions");
    for (std::uint32_t y = 0; y < 5; ++y) {
        for (std::uint32_t x = 0; x < 5; ++x) {
            ColorIndex expected = 6;
            if (x == 0 && y == 0) expected = 1;
            if (x == 2 && y == 1) expected = 11;
            require(canvas.pixel(x, y) == expected,
                    "odd-width grow lost pixels or paper-filled new area");
        }
    }

    manager.setBounds(target, {3, 0, 3, 3});
    require(canvas.width() == 3 && canvas.height() == 3,
            "odd-width shrink did not update canvas dimensions");
    require(canvas.pixel(0, 0) == 1 && canvas.pixel(2, 1) == 11 &&
                canvas.pixel(2, 2) == 6,
            "odd-width shrink did not preserve overlapping pixels");
    require(manager.canvas(first).pixel(1, 1) == 2 &&
                manager.canvas(third).pixel(2, 2) == 12,
            "resizing one window changed another window's pixels");
}

void testFailedCanvasResizePreservesAllWindows() {
    using namespace ql32::graphics;
    WindowManager manager(16, 8);
    const auto first = manager.create(1, {0, 0, 4, 4}, PixelDepth::Four);
    const auto target = manager.create(2, {4, 0, 4, 4}, PixelDepth::Four);
    const auto last = manager.create(3, {8, 0, 4, 4}, PixelDepth::Four);
    manager.canvas(first).setPixel(0, 0, 2);
    manager.canvas(target).setPixel(1, 1, 5);
    manager.canvas(last).setPixel(2, 2, 9);

    // Fail the replacement canvas allocation while the target is in the
    // middle of the window list. No live block or window metadata may change.
    failAllocationAfter.store(0, std::memory_order_relaxed);
    bool failed = false;
    try {
        manager.setBounds(target, {4, 0, 8, 8});
    } catch (const std::bad_alloc&) {
        failed = true;
    }
    failAllocationAfter.store(-1, std::memory_order_relaxed);

    require(failed, "injected replacement canvas allocation did not fail");
    require(manager.size() == 3, "failed resize changed the window count");
    const auto firstState = manager.windowState(first);
    const auto targetState = manager.windowState(target);
    const auto lastState = manager.windowState(last);
    require(firstState.id == first && firstState.bounds.x == 0 &&
                firstState.bounds.y == 0 && firstState.bounds.width == 4 &&
                firstState.bounds.height == 4 && targetState.id == target &&
                targetState.bounds.x == 4 && targetState.bounds.y == 0 &&
                targetState.bounds.width == 4 && targetState.bounds.height == 4 &&
                lastState.id == last && lastState.bounds.x == 8 &&
                lastState.bounds.y == 0 && lastState.bounds.width == 4 &&
                lastState.bounds.height == 4,
            "failed resize changed a window's bounds or handle");
    require(manager.canvas(first).width() == 4 && manager.canvas(target).width() == 4 &&
                manager.canvas(last).width() == 4,
            "failed resize changed a canvas dimension");
    require(manager.canvas(first).pixel(0, 0) == 2 &&
                manager.canvas(target).pixel(1, 1) == 5 &&
                manager.canvas(last).pixel(2, 2) == 9,
            "failed resize changed pixels or invalidated a window handle");
}

void testEveryPrintableAsciiCharacterHasAGlyph() {
    const auto fallback = classicGlyphColumns('\x01');
    for (int character = 32; character <= 126; ++character) {
        const auto pattern = classicGlyphColumns(static_cast<char>(character));
        require(pattern != fallback,
                "printable ASCII character fell through to the unknown-glyph box");
    }
}

void testTextCellsFillCharacterGapsWithPaper() {
    IndexedCanvas canvas(12, 8, PixelDepth::Four);
    canvas.setInk(3);
    canvas.setPaper(6);
    canvas.clear();
    canvas.text({0, 0}, "A");
    bool sawInk = false;
    for (std::uint32_t y = 0; y < 8; ++y) {
        require(canvas.pixel(5, y) == 6,
                "text cell's inter-character column was not filled with paper");
        for (std::uint32_t x = 0; x < 5; ++x)
            sawInk = sawInk || canvas.pixel(x, y) == 3;
    }
    for (std::uint32_t x = 0; x < 6; ++x)
        require(canvas.pixel(x, 7) == 6,
                "text cell's bottom row was not filled with paper");
    require(sawInk, "text glyph did not use the active ink color");
}

void testCloseWindowCommandDispatch() {
    auto graphics = std::make_shared<RecordingGraphics>();
    ql32::Interpreter interpreter({}, {}, {}, graphics);
    std::ostringstream output;
    interpreter.execute("CLOSEWINDOW\n", output);
    require(graphics->events.size() == 1 &&
                graphics->events.front().name == "CLOSEWINDOW",
            "CLOSEWINDOW command was not dispatched");
}

void testAtPositionsDefaultAndOpenedScreenChannels() {
    auto graphics = std::make_shared<RecordingGraphics>();
    auto channels = std::make_shared<ql32::runtime::ChannelRegistry>(nullptr);
    ql32::Interpreter interpreter({}, channels, {}, graphics);
    std::ostringstream output;
    interpreter.execute(
        "AT 5,10\n"
        "PRINT \"default\"\n"
        "wchan=OPEN(4)\n"
        "AT #wchan,2,7\n"
        "PRINT #wchan,\"window\"\n"
        "AT #0,3,4\n"
        "PRINT #0,\"console\"\n", output);
    const auto atEvents = std::count_if(graphics->events.begin(), graphics->events.end(),
        [](const Event& event) { return event.name == "AT"; });
    const auto windowPrint = std::find_if(graphics->events.begin(), graphics->events.end(),
        [](const Event& event) {
            return event.name == "PRINT_TEXT" && event.channel == 2 &&
                   event.text == "window";
        });
    const auto defaultAt = std::find_if(graphics->events.begin(), graphics->events.end(),
        [](const Event& event) {
            return event.name == "AT" && event.channel == 1 &&
                   event.first.x == 5 && event.first.y == 10;
        });
    const auto windowAt = std::find_if(graphics->events.begin(), graphics->events.end(),
        [](const Event& event) {
            return event.name == "AT" && event.channel == 2 &&
                   event.first.x == 2 && event.first.y == 7;
        });
    const auto consoleAt = std::find_if(graphics->events.begin(), graphics->events.end(),
        [](const Event& event) {
            return event.name == "AT" && event.channel == 0 &&
                   event.first.x == 3 && event.first.y == 4;
        });
    require(atEvents == 3 && defaultAt != graphics->events.end() &&
                windowAt != graphics->events.end() && consoleAt != graphics->events.end() &&
                windowPrint != graphics->events.end(),
            "AT did not preserve row/column order and route each screen channel");
}

void testScreenPrintPositionsArePerChannelAndSeparateFromText() {
    auto presenter = std::make_shared<CapturingPresenter>();
    auto graphics = std::make_shared<WindowedService>(80, 64, presenter);
    graphics->setDefaultWindowBounds(2, {0, 0, 32, 40});
    graphics->setDefaultWindowBounds(3, {40, 0, 32, 40});
    require(graphics->setTextPosition(9, 2, 2, 1),
            "screen channel rejected AT position");
    require(graphics->setTextPosition(9, 3, 0, 4),
            "second screen channel rejected AT position");
    graphics->text(9, 2, {24, 24}, "B");
    graphics->writeText(9, 2, "A");
    graphics->writeText(9, 3, "C");
    const auto hasInk = [&](std::uint32_t x, std::uint32_t y) {
        for (std::uint32_t row = y; row < y + 7; ++row)
            for (std::uint32_t column = x; column < x + 5; ++column)
                if (presenter->lastFrame.pixels[row * presenter->lastFrame.width + column] == 7)
                    return true;
        return false;
    };
    require(hasInk(6, 16), "AT did not position PRINT at the requested cell");
    require(hasInk(24, 24), "TEXT pixel positioning did not draw independently");
    require(hasInk(64, 0), "second screen channel did not retain its own PRINT position");
    require(!hasInk(24, 0), "TEXT changed the independent PRINT position");
}

} // namespace

int main() {
    testDrawingCallsAndSessionOwnership();
    testGraphicsUpdateCommandDispatch();
    testWindowedGraphicsUpdateBatching();
    testDrawingArgumentValidation();
    testClsChannelAndColorSyntax();
    testClsColorArgumentsClearWithNewPaper();
    testPixelAndFilledShapesRenderOnRetainedCanvas();
    testDisplayCommands();
    testPrintChannelRouting();
    testChannelDefaultRoutingAndGraphicsSelectors();
    testListChannelRouting();
    testWindowPositionCommand();
    testWindowMovementRestoresCoveredContent();
    testCanvasReshapeReusesFullScreenStorage();
    testCloseWindowReleasesCanvasAndAllowsReuse();
    testCompletedSessionRemainsAvailableForCapture();
    testNewWindowCommandReclaimsFrozenCanvasStorage();
    testDiscardSessionReleasesActiveAndFrozenWindowsByOwner();
    testRepeatedWindowSessionBurstsReleaseAndRecreate();
    testDebugOverlayComposesAboveGraphics();
    testCanvasStorageReleasesAndReusesAllocations();
    testTaskViewportTranslatesAndClipsOwnedWindows();
    testWindowedServiceTaskViewportRedrawAndAbsoluteOrigin();
    testCanvasResizePreservesPixelsAcrossOddWidthGrowAndShrink();
    testFailedCanvasResizePreservesAllWindows();
    testEveryPrintableAsciiCharacterHasAGlyph();
    testTextCellsFillCharacterGapsWithPaper();
    testCloseWindowCommandDispatch();
    testAtPositionsDefaultAndOpenedScreenChannels();
    testScreenPrintPositionsArePerChannelAndSeparateFromText();
}
