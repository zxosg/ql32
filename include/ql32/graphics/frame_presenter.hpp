#pragma once

#include "ql32/graphics/service.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace ql32::graphics {

struct Frame {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::array<RgbColor, 256> palette{};
    std::vector<ColorIndex> pixels;
};

class FramePresenter {
public:
    virtual ~FramePresenter() = default;
    virtual void present(const Frame& frame) = 0;
    virtual WindowInfo setRotation(std::uint16_t) {
        throw std::runtime_error("display rotation unsupported");
    }
};

} // namespace ql32::graphics
