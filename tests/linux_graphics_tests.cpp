#include "ql32/platform/linux/svg_graphics.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() {
    const auto directory = std::filesystem::temp_directory_path() /
        "ql32-linux-graphics-tests";
    std::error_code error;
    std::filesystem::remove_all(directory, error);

    ql32::platform::linux_host::SvgGraphics graphics(directory, 100, 80);
    constexpr ql32::graphics::SessionId session = 17;
    graphics.setPaper(session, 4);
    graphics.clear(session);
    graphics.setPaper(session, 2);
    graphics.setInk(session, 7);
    graphics.line(session, {1, 2}, {30, 40});
    graphics.rectangle(session, {5, 6}, 20, 10);
    graphics.circle(session, {50, 40}, 8);
    graphics.text(session, {8, 12}, "A<&\"B");
    graphics.closeSession(session);

    const auto image = directory / "ql32-graphics-session-17.svg";
    std::ifstream input(image, std::ios::binary);
    require(static_cast<bool>(input), "SVG snapshot was not created");
    const std::string svg{std::istreambuf_iterator<char>(input), {}};
    require(svg.find("width=\"100\" height=\"80\"") != std::string::npos,
            "SVG dimensions were not preserved");
    require(svg.find("fill=\"#00FF00\"") != std::string::npos,
            "paper color did not use QL palette ordering");
    require(svg.find("x1=\"1\" y1=\"2\" x2=\"30\" y2=\"40\"") != std::string::npos,
            "line coordinates were not preserved");
    require(svg.find("width=\"20\" height=\"10\"") != std::string::npos,
            "rectangle dimensions were not preserved");
    require(svg.find("cx=\"50\" cy=\"40\" r=\"8\"") != std::string::npos,
            "circle geometry was not preserved");
    require(svg.find("A&lt;&amp;&quot;B") != std::string::npos,
            "text was not escaped for XML");

    constexpr ql32::graphics::SessionId discardedSession = 18;
    graphics.setInk(discardedSession, 2);
    graphics.line(discardedSession, {0, 0}, {1, 1});
    graphics.discardSession(discardedSession);
    require(!std::filesystem::exists(
                directory / "ql32-graphics-session-18.svg"),
            "discarding a session unexpectedly wrote an SVG snapshot");

    std::filesystem::remove_all(directory, error);
}
