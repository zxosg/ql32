#pragma once

#include <cstdint>
#include <string>
#include <vector>

class Adafruit_ILI9341 {
public:
    struct Call {
        std::string name;
        std::vector<std::int32_t> values;
        std::string text;
    };

    std::int16_t width() const { return _width; }
    std::int16_t height() const { return _height; }
    void setRotation(std::uint8_t rotation) {
        rotation_ = rotation % 4;
        _width = rotation_ % 2 ? 320 : 240;
        _height = rotation_ % 2 ? 240 : 320;
        calls.push_back({"rotation", {rotation_}, {}});
    }
    void sendCommand(std::uint8_t command, const std::uint8_t* data,
                     std::uint8_t length) {
        calls.push_back({"command", {command, length, length ? data[0] : 0}, {}});
    }
    void fillScreen(std::uint16_t color) { calls.push_back({"fill", {color}, {}}); }
    void fillRect(std::int16_t x, std::int16_t y, std::int16_t width,
                  std::int16_t height, std::uint16_t color) {
        calls.push_back({"fill_rect", {x, y, width, height, color}, {}});
    }
    void drawRGBBitmap(std::int16_t x, std::int16_t y, const std::uint16_t*,
                       std::int16_t width, std::int16_t height) {
        calls.push_back({"rgb_bitmap", {x, y, width, height}, {}});
    }
    void drawLine(std::int16_t x0, std::int16_t y0, std::int16_t x1,
                  std::int16_t y1, std::uint16_t color) {
        calls.push_back({"line", {x0, y0, x1, y1, color}, {}});
    }
    void drawRect(std::int16_t x, std::int16_t y, std::int16_t width,
                  std::int16_t height, std::uint16_t color) {
        calls.push_back({"rect", {x, y, width, height, color}, {}});
    }
    void drawCircle(std::int16_t x, std::int16_t y, std::int16_t radius,
                    std::uint16_t color) {
        calls.push_back({"circle", {x, y, radius, color}, {}});
    }
    void setCursor(std::int16_t x, std::int16_t y) {
        calls.push_back({"cursor", {x, y}, {}});
    }
    void setTextColor(std::uint16_t foreground, std::uint16_t background) {
        calls.push_back({"text_color", {foreground, background}, {}});
    }
    void setTextSize(std::uint8_t size) {
        calls.push_back({"text_size", {size}, {}});
    }
    std::size_t print(const char* text) {
        calls.push_back({"text", {}, text});
        return text ? std::char_traits<char>::length(text) : 0;
    }
    std::size_t print(char character) {
        calls.push_back({"character", {}, std::string(1, character)});
        return 1;
    }

    std::vector<Call> calls;
protected:
    std::uint8_t rotation_ = 1;
    std::int16_t _width = 320;
    std::int16_t _height = 240;
};
