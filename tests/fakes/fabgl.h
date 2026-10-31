#pragma once

#include <cstdint>
#include <string>

namespace fabgl {

class Canvas {
public:
    int width = 320;
    int height = 200;
    std::uint8_t penRed = 0;
    std::uint8_t penGreen = 0;
    std::uint8_t penBlue = 0;
    std::uint8_t brushRed = 0;
    std::uint8_t brushGreen = 0;
    std::uint8_t brushBlue = 0;
    int x1 = 0;
    int y1 = 0;
    int x2 = 0;
    int y2 = 0;
    int widthArg = 0;
    int heightArg = 0;
    int clearCount = 0;
    int lineCount = 0;
    int rectCount = 0;
    int ellipseCount = 0;
    std::string text;

    int getWidth() const { return width; }
    int getHeight() const { return height; }
    void setPenColor(std::uint8_t red, std::uint8_t green, std::uint8_t blue) {
        penRed = red; penGreen = green; penBlue = blue;
    }
    void setBrushColor(std::uint8_t red, std::uint8_t green, std::uint8_t blue) {
        brushRed = red; brushGreen = green; brushBlue = blue;
    }
    void clear() { ++clearCount; }
    void drawLine(int fromX, int fromY, int toX, int toY) {
        ++lineCount; x1 = fromX; y1 = fromY; x2 = toX; y2 = toY;
    }
    void drawRectangle(int left, int top, int right, int bottom) {
        ++rectCount; x1 = left; y1 = top; x2 = right; y2 = bottom;
    }
    void drawEllipse(int centerX, int centerY, int width, int height) {
        ++ellipseCount; x1 = centerX; y1 = centerY; widthArg = width; heightArg = height;
    }
    void drawText(int left, int top, const char* value) {
        x1 = left; y1 = top; text = value;
    }
};

} // namespace fabgl
