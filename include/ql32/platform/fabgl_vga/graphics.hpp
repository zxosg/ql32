#pragma once

#include "ql32/graphics/service.hpp"

#include <fabgl.h>

namespace ql32::platform::fabgl_vga {

// VGA32 has one physical pixel surface. This adapter gives it to one active
// BASIC session at a time; session scheduling/window composition is future work.
class FabglGraphics final : public graphics::Service {
public:
    explicit FabglGraphics(fabgl::Canvas& canvas) : canvas_(canvas) {}

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

private:
    void claim(graphics::SessionId session);
    void applyInk();
    void applyPaper();

    fabgl::Canvas& canvas_;
    graphics::SessionId activeSession_ = 0;
    bool hasActiveSession_ = false;
    graphics::ColorIndex ink_ = 7;
    graphics::ColorIndex paper_ = 0;
};

} // namespace ql32::platform::fabgl_vga
