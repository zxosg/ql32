#pragma once

#include "ql32/graphics/frame_presenter.hpp"

#include <filesystem>

namespace ql32::platform::linux_host {

// Writes the latest composed display as a binary PPM image.
class PpmPresenter final : public graphics::FramePresenter {
public:
    explicit PpmPresenter(std::filesystem::path outputFile);
    void present(const graphics::Frame& frame) override;

private:
    std::filesystem::path outputFile_;
};

} // namespace ql32::platform::linux_host
