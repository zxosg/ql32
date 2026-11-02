#include "ql32/interpreter.hpp"
#include "ql32/graphics/windowed_service.hpp"

#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

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
    void setPaper(SessionId session, ColorIndex color) override {
        events.push_back({"PAPER", session, {}, {}, color});
    }
    void line(SessionId session, Point from, Point to) override {
        events.push_back({"LINE", session, from, to});
    }
    void rectangle(SessionId session, Point topLeft, std::int32_t width,
                   std::int32_t height) override {
        events.push_back({"RECT", session, topLeft, {}, width, height});
    }
    void circle(SessionId session, Point center, std::int32_t radius) override {
        events.push_back({"CIRCLE", session, center, {}, radius});
    }
    void text(SessionId session, Point topLeft, const std::string& value) override {
        events.push_back({"TEXT", session, topLeft, {}, 0, 0, value});
    }
    void clear(SessionId session) override {
        events.push_back({"CLS", session});
    }
    void closeWindow(SessionId session) override {
        events.push_back({"CLOSEWINDOW", session});
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
            "RECT 5,6,20,10\n"
            "CIRCLE 50,60,8\n"
            "TEXT 2,3,\"hello\"\n"
            "CLS\n", output);
        require(graphics->events.size() == 7, "drawing calls were not dispatched");
        session = graphics->events.front().session;
        require(session != 0, "graphics calls used an invalid session");
        require(graphics->events[0].name == "INK" && graphics->events[0].a == 7,
                "INK arguments were not preserved");
        require(graphics->events[1].name == "PAPER" && graphics->events[1].a == 1,
                "PAPER arguments were not preserved");
        require(graphics->events[2].first.x == 1 && graphics->events[2].first.y == 2 &&
                    graphics->events[2].second.x == 30 && graphics->events[2].second.y == 40,
                "LINE coordinates were not preserved");
        require(graphics->events[3].a == 20 && graphics->events[3].b == 10,
                "RECT dimensions were not preserved");
        require(graphics->events[4].a == 8, "CIRCLE radius was not preserved");
        require(graphics->events[5].text == "hello", "TEXT value was not preserved");
        require(graphics->events[6].name == "CLS", "CLS was not dispatched");
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
    WindowedService graphics(10, 10, presenter, 2);
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
    require(graphics->events.empty(), "invalid drawing call reached the service");
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

    auto presenter = std::make_shared<CapturingPresenter>();
    auto window = std::make_shared<WindowedService>(60, 24, presenter, 2);
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

    bool consoleGraphicsRejected = false;
    try {
        interpreter.execute("CIRCLE #0,1,2,3\n", console);
    } catch (const std::runtime_error& error) {
        consoleGraphicsRejected = std::string(error.what()).find("not a graphics window") !=
                                  std::string::npos;
    }
    require(consoleGraphicsRejected, "graphics output was silently sent to the console channel");

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
    WindowedService graphics(100, 80, presenter, 4, 0);
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
    WindowManager manager(240, 320, 1, 0);
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
    constexpr std::size_t oneFullCanvasBytes = 20 * 10 / 2;
    WindowedService graphics(20, 10, presenter, 2, oneFullCanvasBytes);
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

void testCloseWindowCommandDispatch() {
    auto graphics = std::make_shared<RecordingGraphics>();
    ql32::Interpreter interpreter({}, {}, {}, graphics);
    std::ostringstream output;
    interpreter.execute("CLOSEWINDOW\n", output);
    require(graphics->events.size() == 1 &&
                graphics->events.front().name == "CLOSEWINDOW",
            "CLOSEWINDOW command was not dispatched");
}

} // namespace

int main() {
    testDrawingCallsAndSessionOwnership();
    testGraphicsUpdateCommandDispatch();
    testWindowedGraphicsUpdateBatching();
    testDrawingArgumentValidation();
    testDisplayCommands();
    testPrintChannelRouting();
    testChannelDefaultRoutingAndGraphicsSelectors();
    testListChannelRouting();
    testWindowPositionCommand();
    testWindowMovementRestoresCoveredContent();
    testCanvasReshapeReusesFullScreenStorage();
    testCloseWindowReleasesCanvasAndAllowsReuse();
    testCloseWindowCommandDispatch();
}
