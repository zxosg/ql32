#pragma once

#include <cstdint>
#include <deque>
#include <vector>
#include <string>

namespace fabgl {

enum VirtualKey {
    VK_NONE, VK_RETURN, VK_KP_ENTER, VK_ESCAPE, VK_BACKSPACE, VK_TAB,
    VK_INSERT, VK_KP_INSERT, VK_DELETE, VK_KP_DELETE, VK_SPACE, VK_PAUSE,
    VK_BREAK, VK_HOME, VK_KP_HOME,
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

struct RGB888 {
    std::uint8_t R = 0;
    std::uint8_t G = 0;
    std::uint8_t B = 0;
    RGB888() = default;
    RGB888(std::uint8_t red, std::uint8_t green, std::uint8_t blue)
        : R(red), G(green), B(blue) {}
};

struct RGB222 {
    std::uint8_t R = 0;
    std::uint8_t G = 0;
    std::uint8_t B = 0;
    explicit RGB222(const RGB888& color)
        : R(static_cast<std::uint8_t>((color.R >> 6) & 0x03)),
          G(static_cast<std::uint8_t>((color.G >> 6) & 0x03)),
          B(static_cast<std::uint8_t>((color.B >> 6) & 0x03)) {}
};

enum class PixelFormat { RGBA8888 };

struct RGBA8888 {
    std::uint8_t R = 0;
    std::uint8_t G = 0;
    std::uint8_t B = 0;
    std::uint8_t A = 0;
    RGBA8888() = default;
    RGBA8888(std::uint8_t red, std::uint8_t green,
             std::uint8_t blue, std::uint8_t alpha)
        : R(red), G(green), B(blue), A(alpha) {}
};

class Bitmap {
public:
    Bitmap(int, int, void*, PixelFormat) {}
};

class VGAController {
public:
    VGAController(int width = 320, int height = 200)
        : width_(width), height_(height), buffers_{
            std::vector<std::vector<std::uint8_t>>(height,
                std::vector<std::uint8_t>(width)),
            std::vector<std::vector<std::uint8_t>>(height,
                std::vector<std::uint8_t>(width))} {}
    bool isDoubleBufferedEnabled() const { return doubleBuffered_; }
    void setDoubleBuffered(bool enabled) { doubleBuffered_ = enabled; }
    std::uint8_t createRawPixel(const RGB222& color) const {
        return static_cast<std::uint8_t>((color.R << 4) | (color.G << 2) | color.B);
    }
    std::uint8_t* getScanline(int y) {
        ++scanlineCallCount;
        if (scanlineCallCount == failOnScanlineCall) return nullptr;
        if (y < 0 || y >= height_) return nullptr;
        return buffers_[drawBuffer_][static_cast<std::size_t>(y)].data();
    }
    void swapBuffers() {
        std::swap(drawBuffer_, visibleBuffer_);
        ++swapCount;
    }
    const std::vector<std::vector<std::uint8_t>>& drawBuffer() const {
        return buffers_[drawBuffer_];
    }
    const std::vector<std::vector<std::uint8_t>>& visibleBuffer() const {
        return buffers_[visibleBuffer_];
    }
    int swapCount = 0;
    int scanlineCallCount = 0;
    int failOnScanlineCall = -1;

private:
    int width_;
    int height_;
    bool doubleBuffered_ = true;
    int drawBuffer_ = 0;
    int visibleBuffer_ = 1;
    std::vector<std::vector<std::uint8_t>> buffers_[2];
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
    int pixelCount = 0;
    int updateCount = 0;
    int completionCount = 0;
    int swapCount = 0;
    VGAController* display = nullptr;

    int getWidth() const { return width; }
    int getHeight() const { return height; }
    void beginUpdate() { ++updateCount; }
    void endUpdate() { ++updateCount; }
    void waitCompletion(bool) { ++completionCount; }
    void swapBuffers() {
        ++swapCount;
        if (display) display->swapBuffers();
    }
    void drawBitmap(int, int, const Bitmap*) {}
    void setPixel(int, int, RGB888 const&) { ++pixelCount; }
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
