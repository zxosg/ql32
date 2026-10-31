#include "ql32/platform/linux/svg_graphics.hpp"

#include <array>
#include <fstream>
#include <system_error>
#include <utility>

namespace ql32::platform::linux_host {
namespace {

constexpr std::array<const char*, 8> colors{
    "#000000", "#0000FF", "#FF0000", "#FF00FF",
    "#00FF00", "#00FFFF", "#FFFF00", "#FFFFFF"};

std::string escaped(const std::string& value) {
    std::string result;
    result.reserve(value.size());
    for (const char ch : value) {
        switch (ch) {
        case '&': result += "&amp;"; break;
        case '<': result += "&lt;"; break;
        case '>': result += "&gt;"; break;
        case '"': result += "&quot;"; break;
        case '\'': result += "&apos;"; break;
        default: result += ch; break;
        }
    }
    return result;
}

} // namespace

SvgGraphics::SvgGraphics(std::filesystem::path outputDirectory,
                         std::uint32_t width, std::uint32_t height)
    : outputDirectory_(std::move(outputDirectory)), width_(width), height_(height) {
    if (width_ == 0) width_ = 640;
    if (height_ == 0) height_ = 480;
}

SvgGraphics::Window& SvgGraphics::window(graphics::SessionId session) {
    return windows_.try_emplace(session).first->second;
}

void SvgGraphics::setInk(graphics::SessionId session, graphics::ColorIndex color) {
    std::lock_guard lock(mutex_);
    window(session).ink = static_cast<graphics::ColorIndex>(color % colors.size());
}

void SvgGraphics::setPaper(graphics::SessionId session, graphics::ColorIndex color) {
    std::lock_guard lock(mutex_);
    window(session).paper = static_cast<graphics::ColorIndex>(color % colors.size());
}

void SvgGraphics::line(graphics::SessionId session, graphics::Point from,
                       graphics::Point to) {
    std::lock_guard lock(mutex_);
    auto& w = window(session);
    w.elements.push_back("<line x1=\"" + std::to_string(from.x) + "\" y1=\"" +
        std::to_string(from.y) + "\" x2=\"" + std::to_string(to.x) +
        "\" y2=\"" + std::to_string(to.y) + "\" stroke=\"" + colors[w.ink] +
        "\" stroke-width=\"1\"/>\n");
}

void SvgGraphics::rectangle(graphics::SessionId session, graphics::Point topLeft,
                            std::int32_t width, std::int32_t height) {
    std::lock_guard lock(mutex_);
    auto& w = window(session);
    w.elements.push_back("<rect x=\"" + std::to_string(topLeft.x) + "\" y=\"" +
        std::to_string(topLeft.y) + "\" width=\"" + std::to_string(width) +
        "\" height=\"" + std::to_string(height) + "\" fill=\"none\" stroke=\"" +
        colors[w.ink] + "\" stroke-width=\"1\"/>\n");
}

void SvgGraphics::circle(graphics::SessionId session, graphics::Point center,
                         std::int32_t radius) {
    std::lock_guard lock(mutex_);
    auto& w = window(session);
    w.elements.push_back("<circle cx=\"" + std::to_string(center.x) + "\" cy=\"" +
        std::to_string(center.y) + "\" r=\"" + std::to_string(radius) +
        "\" fill=\"none\" stroke=\"" + colors[w.ink] +
        "\" stroke-width=\"1\"/>\n");
}

void SvgGraphics::text(graphics::SessionId session, graphics::Point topLeft,
                       const std::string& value) {
    std::lock_guard lock(mutex_);
    auto& w = window(session);
    w.elements.push_back("<text x=\"" + std::to_string(topLeft.x) + "\" y=\"" +
        std::to_string(topLeft.y) + "\" dominant-baseline=\"hanging\" font-family=\"monospace\" font-size=\"8\" fill=\"" +
        colors[w.ink] + "\" xml:space=\"preserve\">" + escaped(value) + "</text>\n");
}

void SvgGraphics::clear(graphics::SessionId session) {
    std::lock_guard lock(mutex_);
    auto& w = window(session);
    w.elements.clear();
    w.background = w.paper;
}

void SvgGraphics::closeSession(graphics::SessionId session) {
    Window w;
    {
        std::lock_guard lock(mutex_);
        const auto found = windows_.find(session);
        if (found == windows_.end()) return;
        w = std::move(found->second);
        windows_.erase(found);
    }

    std::error_code error;
    std::filesystem::create_directories(outputDirectory_, error);
    if (error) return;
    const auto filename = "ql32-graphics-session-" + std::to_string(session) + ".svg";
    std::ofstream output(outputDirectory_ / filename, std::ios::binary);
    if (!output) return;

    output << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
           << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << width_
           << "\" height=\"" << height_ << "\" viewBox=\"0 0 " << width_
           << " " << height_ << "\">\n"
           << "<defs><clipPath id=\"window\"><rect width=\"" << width_
           << "\" height=\"" << height_ << "\"/></clipPath></defs>\n"
           << "<rect width=\"100%\" height=\"100%\" fill=\"" << colors[w.background]
           << "\"/>\n<g clip-path=\"url(#window)\">\n";
    for (const auto& element : w.elements) output << element;
    output << "</g>\n</svg>\n";
}

} // namespace ql32::platform::linux_host
