#pragma once

#include "ql32/graphics/frame_presenter.hpp"

#include <fabgl.h>

namespace ql32::platform::fabgl_vga {

class FabglFramePresenter final : public graphics::FramePresenter {
public:
    explicit FabglFramePresenter(fabgl::Canvas& canvas) : canvas_(canvas) {}
    void present(const graphics::Frame& frame) override;

private:
    fabgl::Canvas& canvas_;
};

} // namespace ql32::platform::fabgl_vga
