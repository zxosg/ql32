#pragma once

#include "ql32/graphics/service.hpp"

#include <Adafruit_ILI9341.h>

#include <deque>
#include <streambuf>
#include <string>

namespace ql32::platform::esp32_cyd {

// Scrollable PRINT output in the channel #0 area. Graphics retain the full
// display coordinate space and can draw behind this transparent text overlay.
class TftTextConsole final : public std::streambuf {
public:
    explicit TftTextConsole(Adafruit_ILI9341& display);
    void clear();
    void reconfigure();
    void setBounds(graphics::Rect bounds);
    void setColors(graphics::ColorIndex ink, graphics::ColorIndex paper);
    void restoreOverlay(graphics::Rect region);

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
    std::int16_t firstColumnX_;
    graphics::Rect bounds_{};
    std::uint16_t foregroundColor_ = 0xFFFF;
    std::uint16_t backgroundColor_ = 0x0000;

    void put(char character);
    void advanceLine();
    void redraw();
    void redraw(graphics::Rect region);
    void updateGeometry();
};

} // namespace ql32::platform::esp32_cyd
