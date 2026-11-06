#pragma once

#include <cstdint>
#include <string>
#include <vector>

class SPIClass {};

class Adafruit_ST7789 {
public:
    struct Call {
        std::string name;
        std::vector<std::int32_t> values;
    };

    Adafruit_ST7789(SPIClass*, std::int8_t, std::int8_t, std::int8_t) {}
    void init(std::uint16_t width, std::uint16_t height) {
        nativeWidth_ = width;
        nativeHeight_ = height;
        setRotation(0);
    }
    void setRotation(std::uint8_t rotation) {
        rotation_ = rotation % 4;
        width_ = rotation_ % 2 ? nativeHeight_ : nativeWidth_;
        height_ = rotation_ % 2 ? nativeWidth_ : nativeHeight_;
        calls.push_back({"rotation", {rotation_}});
    }
    std::int16_t width() const { return width_; }
    std::int16_t height() const { return height_; }
    void fillScreen(std::uint16_t color) { calls.push_back({"fill", {color}}); }
    void fillRect(std::int16_t x, std::int16_t y, std::int16_t width,
                  std::int16_t height, std::uint16_t color) {
        calls.push_back({"fill_rect", {x, y, width, height, color}});
    }
    void drawLine(std::int16_t x0, std::int16_t y0, std::int16_t x1,
                  std::int16_t y1, std::uint16_t color) {
        calls.push_back({"line", {x0, y0, x1, y1, color}});
    }
    void drawRect(std::int16_t x, std::int16_t y, std::int16_t width,
                  std::int16_t height, std::uint16_t color) {
        calls.push_back({"rect", {x, y, width, height, color}});
    }
    void drawCircle(std::int16_t x, std::int16_t y, std::int16_t radius,
                    std::uint16_t color) {
        calls.push_back({"circle", {x, y, radius, color}});
    }
    void setCursor(std::int16_t x, std::int16_t y) {
        calls.push_back({"cursor", {x, y}});
    }
    void setTextColor(std::uint16_t foreground, std::uint16_t background) {
        calls.push_back({"text_color", {foreground, background}});
    }
    void setTextSize(std::uint8_t size) { calls.push_back({"text_size", {size}}); }
    std::size_t print(const char* text) {
        const std::string value = text ? text : "";
        calls.push_back({"text", {static_cast<std::int32_t>(value.size())}});
        return value.size();
    }
    std::size_t print(char character) {
        calls.push_back({"character", {static_cast<unsigned char>(character)}});
        return 1;
    }
    void drawRGBBitmap(std::int16_t x, std::int16_t y, const std::uint16_t*,
                       std::int16_t width, std::int16_t height) {
        calls.push_back({"bitmap", {x, y, width, height}});
    }
    void startWrite() { calls.push_back({"start", {}}); }
    void endWrite() { calls.push_back({"end", {}}); }
    void setAddrWindow(std::uint16_t x, std::uint16_t y, std::uint16_t width,
                       std::uint16_t height) {
        calls.push_back({"window", {x, y, width, height}});
    }
    void writePixels(std::uint16_t*, std::uint32_t count) {
        calls.push_back({"pixels", {static_cast<std::int32_t>(count)}});
    }

    std::vector<Call> calls;

private:
    std::uint8_t rotation_ = 0;
    std::int16_t nativeWidth_ = 135;
    std::int16_t nativeHeight_ = 240;
    std::int16_t width_ = 135;
    std::int16_t height_ = 240;
};

constexpr std::uint16_t ST77XX_BLACK = 0x0000;
