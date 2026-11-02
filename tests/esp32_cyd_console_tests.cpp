#include "ql32/platform/esp32_cyd/text_console.hpp"

#include <ostream>
#include <stdexcept>

namespace {

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

void testPrintOutputAndWrapping() {
    Adafruit_ILI9341 display;
    ql32::platform::esp32_cyd::TftTextConsole console(display);
    std::ostream output(&console);
    output << "HELLO\nWORLD";
    require(display.calls.front().name == "fill_rect" &&
                display.calls.front().values[1] == 176 &&
                display.calls.front().values[3] == 64,
            "console did not initialize the bottom display strip");
    std::string renderedCharacters;
    for (const auto& call : display.calls) {
        if (call.name == "character") renderedCharacters += call.text;
    }
    require(renderedCharacters == "HELLOWORLD",
            "stream output was not rendered as console text");
}

void testScrollingAndClear() {
    Adafruit_ILI9341 display;
    ql32::platform::esp32_cyd::TftTextConsole console(display);
    std::ostream output(&console);
    for (int line = 0; line < 10; ++line) output << "line" << line << '\n';
    int regionClears = 0;
    for (const auto& call : display.calls)
        if (call.name == "fill_rect") ++regionClears;
    require(regionClears == 1,
            "scrolling console repainted an opaque rectangle over graphics");
    console.clear();
    require(display.calls.back().name == "fill_rect" && regionClears + 1 == 2,
            "clearing console did not clear its display region");
}

void testReconfigureAfterRotation() {
    Adafruit_ILI9341 display;
    ql32::platform::esp32_cyd::TftTextConsole console(display);
    display.setRotation(0);
    console.reconfigure();
    std::ostream output(&console);
    output << "P";
    bool usedPortraitConsoleOrigin = false;
    for (const auto& call : display.calls) {
        if (call.name == "cursor" && call.values.size() == 2 &&
            call.values[0] == 0 && call.values[1] == 256)
            usedPortraitConsoleOrigin = true;
    }
    require(usedPortraitConsoleOrigin,
            "console retained landscape geometry after display rotation");
}

void testConsoleBoundsCanFollowChannelWindow() {
    Adafruit_ILI9341 display;
    ql32::platform::esp32_cyd::TftTextConsole console(display);
    console.setBounds({20, 100, 120, 24});
    std::ostream output(&console);
    output << "BOX";
    bool usedRequestedBounds = false;
    for (const auto& call : display.calls) {
        if (call.name == "cursor" && call.values.size() == 2 &&
            call.values[0] == 20 && call.values[1] == 100)
            usedRequestedBounds = true;
    }
    require(usedRequestedBounds, "console text ignored the channel #0 window bounds");
}

} // namespace

int main() {
    testPrintOutputAndWrapping();
    testScrollingAndClear();
    testReconfigureAfterRotation();
    testConsoleBoundsCanFollowChannelWindow();
    return 0;
}
