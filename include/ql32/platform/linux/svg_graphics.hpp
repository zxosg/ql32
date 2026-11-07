#pragma once

#include "ql32/graphics/service.hpp"

#include <filesystem>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace ql32::platform::linux_host {

// Debug renderer that writes one self-contained SVG snapshot per session when
// the interpreter releases its graphics window.
class SvgGraphics final : public graphics::Service {
public:
    explicit SvgGraphics(std::filesystem::path outputDirectory = ".",
                         std::uint32_t width = 640, std::uint32_t height = 480);

    void setInk(graphics::SessionId session, graphics::ColorIndex color) override;
    void setPaper(graphics::SessionId session, graphics::ColorIndex color) override;
    void line(graphics::SessionId session, graphics::Point from,
              graphics::Point to) override;
    void rectangle(graphics::SessionId session, graphics::Point topLeft,
                   std::int32_t width, std::int32_t height) override;
    void circle(graphics::SessionId session, graphics::Point center,
                std::int32_t radius) override;
    void text(graphics::SessionId session, graphics::Point topLeft,
              const std::string& value) override;
    void clear(graphics::SessionId session) override;
    void closeSession(graphics::SessionId session) override;
    void discardSession(graphics::SessionId session) override;
    graphics::WindowInfo windowInfo(graphics::SessionId session) override;
    graphics::RgbColor paletteColor(graphics::SessionId session, std::uint16_t index) override;

private:
    struct Window {
        graphics::ColorIndex ink = 7;
        graphics::ColorIndex paper = 0;
        graphics::ColorIndex background = 0;
        std::vector<std::string> elements;
    };

    Window& window(graphics::SessionId session);
    std::filesystem::path outputDirectory_;
    std::uint32_t width_;
    std::uint32_t height_;
    std::mutex mutex_;
    std::unordered_map<graphics::SessionId, Window> windows_;
};

} // namespace ql32::platform::linux_host
