#pragma once

#include <cstddef>
#include <cstdint>

#include <fabgl.h>

namespace ql32::platform::olimex_vga {

// Minimal scanout experiment for the Olimex ESP32-SBC-FabGL. The source test
// image lives in PSRAM; descriptors and the three reusable visible-line
// buffers are allocated from internal DMA-capable RAM.
class PsramDmaScanout {
public:
    PsramDmaScanout() = default;
    PsramDmaScanout(const PsramDmaScanout&) = delete;
    PsramDmaScanout& operator=(const PsramDmaScanout&) = delete;

    bool begin(const char* modeline, int redPin, int greenPin, int bluePin,
               int hsyncPin, int vsyncPin);
    std::size_t psramBytes() const noexcept { return psramBytes_; }
    std::size_t internalDmaBytes() const noexcept { return internalDmaBytes_; }

private:
    bool allocateAndBuild(const fabgl::VGATimings& timings);
    static std::uint8_t syncByte(const fabgl::VGATimings& timings,
                                 bool hsync, bool vsync,
                                 std::uint8_t rgb = 0) noexcept;

    fabgl::GPIOStream stream_;
    std::uint8_t* psramFrame_ = nullptr;
    std::uint8_t* lineRing_ = nullptr;
    std::uint8_t* horizontalBlank_ = nullptr;
    std::uint8_t* verticalBlank_ = nullptr;
    std::uint8_t* verticalSync_ = nullptr;
    lldesc_t* descriptors_ = nullptr;
    std::size_t descriptorCount_ = 0;
    std::size_t psramBytes_ = 0;
    std::size_t internalDmaBytes_ = 0;
};

} // namespace ql32::platform::olimex_vga
