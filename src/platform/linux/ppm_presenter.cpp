#include "ql32/platform/linux/ppm_presenter.hpp"

#include <fstream>
#include <stdexcept>
#include <utility>

namespace ql32::platform::linux_host {

PpmPresenter::PpmPresenter(std::filesystem::path outputFile)
    : outputFile_(std::move(outputFile)) {}

void PpmPresenter::present(const graphics::Frame& frame) {
    const auto required = static_cast<std::uint64_t>(frame.width) * frame.height;
    if (frame.width == 0 || frame.height == 0 || required != frame.pixels.size())
        throw std::invalid_argument("frame dimensions do not match pixel data");
    std::ofstream output(outputFile_, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("cannot open Linux display frame output");
    output << "P6\n" << frame.width << " " << frame.height << "\n255\n";
    for (const auto index : frame.pixels) {
        const auto& pixel = frame.palette[index];
        const char rgb[] = {static_cast<char>(pixel.red),
                            static_cast<char>(pixel.green),
                            static_cast<char>(pixel.blue)};
        output.write(rgb, sizeof(rgb));
    }
    if (!output) throw std::runtime_error("failed to write Linux display frame");
}

} // namespace ql32::platform::linux_host
