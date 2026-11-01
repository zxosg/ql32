#pragma once

#include "ql32/graphics/service.hpp"

#include <array>
#include <cstdint>
#include <functional>
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
    using RowProvider =
        std::function<void(std::uint32_t, std::vector<ColorIndex>&)>;

    virtual ~FramePresenter() = default;
    virtual void present(const Frame& frame) = 0;
    // Low-memory presenters can consume one composed row at a time instead
    // of requiring a full-screen indexed frame allocation.
    virtual bool presentRows(std::uint32_t, std::uint32_t,
                             const std::array<RgbColor, 256>&,
                             const RowProvider&) { return false; }
    virtual WindowInfo setRotation(std::uint16_t) {
        throw std::runtime_error("display rotation unsupported");
    }
};

} // namespace ql32::graphics
