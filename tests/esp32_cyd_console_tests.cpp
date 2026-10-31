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
    require(regionClears > 1, "console did not scroll its output region");
    console.clear();
    require(display.calls.back().name == "fill_rect",
            "clearing console did not clear its display region");
}

} // namespace

int main() {
    testPrintOutputAndWrapping();
    testScrollingAndClear();
    return 0;
}
