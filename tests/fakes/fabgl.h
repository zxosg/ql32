#pragma once

#include <cstdint>
#include <deque>
#include <string>

namespace fabgl {

enum VirtualKey {
    VK_NONE, VK_RETURN, VK_KP_ENTER, VK_ESCAPE, VK_BACKSPACE, VK_TAB,
    VK_INSERT, VK_KP_INSERT, VK_DELETE, VK_KP_DELETE, VK_HOME, VK_KP_HOME,
    VK_END, VK_KP_END, VK_PAGEUP, VK_KP_PAGEUP, VK_PAGEDOWN, VK_KP_PAGEDOWN,
    VK_LEFT, VK_KP_LEFT, VK_RIGHT, VK_KP_RIGHT, VK_UP, VK_KP_UP,
    VK_DOWN, VK_KP_DOWN, VK_F1, VK_F2, VK_F3, VK_F4, VK_F5, VK_F6,
    VK_F7, VK_F8, VK_F9, VK_F10, VK_F11, VK_F12, VK_a
};

struct VirtualKeyItem {
    VirtualKey vk = VK_NONE;
    bool down = false;
    std::uint8_t ASCII = 0;
    bool SHIFT = false;
    bool CTRL = false;
    bool LALT = false;
    bool RALT = false;
    bool CAPSLOCK = false;
};

class Keyboard {
public:
    void push(VirtualKey key, bool down, std::uint8_t ascii,
              bool shift, bool control, bool alt, bool capsLock) {
        queue.push_back({key, down, ascii, shift, control, alt, false, capsLock});
    }
    bool getNextVirtualKey(VirtualKeyItem* item, int) {
        if (queue.empty()) return false;
        *item = queue.front();
        queue.pop_front();
        return true;
    }

private:
    std::deque<VirtualKeyItem> queue;
};

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
