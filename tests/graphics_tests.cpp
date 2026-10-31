#include "ql32/interpreter.hpp"

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
    void closeSession(SessionId session) override {
        events.push_back({"CLOSE_SESSION", session});
    }

    std::vector<Event> events;
};

void testDrawingCallsAndSessionOwnership() {
    auto graphics = std::make_shared<RecordingGraphics>();
    std::ostringstream output;
    ql32::graphics::SessionId session = 0;
    {
        ql32::Interpreter interpreter({}, {}, {}, graphics);
        interpreter.execute(
            "INK(7)\n"
            "PAPER(1)\n"
            "LINE(1,2,30,40)\n"
            "RECT(5,6,20,10)\n"
            "CIRCLE(50,60,8)\n"
            "TEXT(2,3,\"hello\")\n"
            "CLS()\n", output);
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

void testDrawingArgumentValidation() {
    auto graphics = std::make_shared<RecordingGraphics>();
    ql32::Interpreter interpreter({}, {}, {}, graphics);
    std::ostringstream output;
    bool invalidColor = false;
    try {
        interpreter.execute("INK(8)\n", output);
    } catch (const std::runtime_error& error) {
        invalidColor = std::string(error.what()).find("color index") != std::string::npos;
    }
    require(invalidColor, "invalid palette index was accepted");
    bool invalidCoordinates = false;
    try {
        interpreter.execute("LINE(1,2,3.5,4)\n", output);
    } catch (const std::runtime_error& error) {
        invalidCoordinates = std::string(error.what()).find("pixel coordinate") !=
                             std::string::npos;
    }
    require(invalidCoordinates, "non-integer pixel coordinate was accepted");
    require(graphics->events.empty(), "invalid drawing call reached the service");
}

} // namespace

int main() {
    testDrawingCallsAndSessionOwnership();
    testDrawingArgumentValidation();
}
