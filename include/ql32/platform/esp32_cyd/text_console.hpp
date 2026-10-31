#pragma once

#include <Adafruit_ILI9341.h>

#include <deque>
#include <streambuf>
#include <string>

namespace ql32::platform::esp32_cyd {

// Scrollable PRINT output at the bottom of the TFT. Graphics retain the full
// display coordinate space and can draw behind this text overlay.
class TftTextConsole final : public std::streambuf {
public:
    explicit TftTextConsole(Adafruit_ILI9341& display);
    void clear();
    void reconfigure();

protected:
    int_type overflow(int_type character) override;
    std::streamsize xsputn(const char* data, std::streamsize size) override;
    int sync() override { return 0; }

private:
    Adafruit_ILI9341& display_;
    std::deque<std::string> lines_;
    std::size_t visibleRows_;
    std::size_t columns_;
    std::int16_t firstRowY_;

    void put(char character);
    void advanceLine();
    void redraw();
    void updateGeometry();
};

} // namespace ql32::platform::esp32_cyd
