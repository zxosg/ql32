#pragma once

#include "ql32/input/service.hpp"

#include <fabgl.h>

namespace ql32::platform::fabgl_vga {

// Non-blocking adapter for FabGL's PS/2 virtual-key queue. Initialize the
// PS2Controller with a virtual-key queue before constructing this adapter.
class FabglKeyboard final : public input::KeyboardService {
public:
    explicit FabglKeyboard(fabgl::Keyboard& keyboard) : keyboard_(keyboard) {}
    std::optional<input::KeyEvent> poll() override;

private:
    fabgl::Keyboard& keyboard_;
};

} // namespace ql32::platform::fabgl_vga
