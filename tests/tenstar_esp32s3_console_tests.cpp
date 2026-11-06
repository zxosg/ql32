#include "ql32/platform/tenstar_esp32s3/text_console.hpp"
#include "ql32/platform/tenstar_esp32s3/graphics.hpp"

#include <cassert>
#include <ostream>

int main() {
    SPIClass spi;
    ql32::platform::tenstar_esp32s3::TenstarSt7789Display display(&spi, 39, 7, 40);
    display.beginTenstar();
    ql32::platform::tenstar_esp32s3::TftTextConsole console(display);
    std::ostream output(&console);
    output << "TENSTAR\nQL32";

    bool startedAtBottomStrip = false;
    bool renderedAllText = false;
    std::size_t characters = 0;
    for (const auto& call : display.calls) {
        if (call.name == "fill_rect" && call.values.size() == 5 &&
            call.values[1] == 95 && call.values[3] == 40)
            startedAtBottomStrip = true;
        if (call.name == "character") ++characters;
    }
    renderedAllText = characters == 11;
    assert(startedAtBottomStrip);
    assert(renderedAllText);
}
