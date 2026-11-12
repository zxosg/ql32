#include "ql32/platform/olimex_vga/psram_dma_scanout.hpp"

#include <driver/gpio.h>
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_attr.h>
#include <rom/lldesc.h>
#include <soc/gpio_sig_map.h>
#include <soc/soc_memory_types.h>

#include <cstdio>
#include <limits>

namespace ql32::platform::olimex_vga {
namespace {

constexpr std::uint32_t kWidth = 320;
constexpr std::uint32_t kHeight = 200;
constexpr std::size_t kLineRingCount = 3;

std::uint8_t* dmaAlloc(std::size_t bytes) {
    return static_cast<std::uint8_t*>(heap_caps_malloc(
        bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
}

void connectOutputPin(int pin, int bit) {
    fabgl::configureGPIO(static_cast<gpio_num_t>(pin), GPIO_MODE_OUTPUT);
    gpio_matrix_out(static_cast<gpio_num_t>(pin), I2S1O_DATA_OUT0_IDX + bit,
                    false, false);
}

void fillDescriptor(lldesc_t& desc, std::uint8_t* bytes, std::size_t length,
                    lldesc_t* next) {
    desc.eof = 0;
    desc.sosf = 0;
    desc.owner = 1;
    desc.offset = 0;
    desc.size = static_cast<std::uint32_t>((length + 3u) & ~std::size_t(3u));
    desc.length = static_cast<std::uint32_t>(length);
    desc.buf = bytes;
    desc.qe.stqe_next = next;
}

} // namespace

std::uint8_t PsramDmaScanout::syncByte(const fabgl::VGATimings& timings,
                                      bool hsync, bool vsync,
                                      std::uint8_t rgb) noexcept {
    const auto h = timings.HSyncLogic == '+' ? hsync : !hsync;
    const auto v = timings.VSyncLogic == '+' ? vsync : !vsync;
    return static_cast<std::uint8_t>(rgb | (h ? 1u << 6 : 0u) |
                                      (v ? 1u << 7 : 0u));
}

bool PsramDmaScanout::begin(const char* modeline, int redPin, int greenPin,
                            int bluePin, int hsyncPin, int vsyncPin) {
    fabgl::VGATimings timings{};
    if (!modeline || !fabgl::VGABaseController::convertModelineToTimings(
                         modeline, &timings)) {
        Serial.println("PSRAM VGA scanout: invalid FabGL modeline");
        return false;
    }
    if (timings.HVisibleArea != static_cast<int>(kWidth) ||
        timings.VVisibleArea != static_cast<int>(kHeight) ||
        timings.HStartingBlock != fabgl::VGAScanStart::VisibleArea) {
        Serial.println("PSRAM VGA scanout: test path currently requires 320x200 visible-area-first timing");
        return false;
    }

    connectOutputPin(redPin, 0);
    connectOutputPin(greenPin, 2);
    connectOutputPin(bluePin, 4);
    connectOutputPin(hsyncPin, 6);
    connectOutputPin(vsyncPin, 7);
    stream_.begin();

    if (!allocateAndBuild(timings)) return false;
    stream_.play(timings.frequency, descriptors_);
    const auto hTotal = timings.HFrontPorch + timings.HSyncPulse +
        timings.HBackPorch + timings.HVisibleArea;
    const auto vTotal = timings.VVisibleArea + timings.VFrontPorch +
        timings.VSyncPulse + timings.VBackPorch;
    const float refresh = static_cast<float>(timings.frequency) /
        (hTotal * vTotal * timings.scanCount);
    Serial.printf("PSRAM VGA scanout started: %ux%u, pixel clock=%u Hz, nominal refresh=%.2f Hz; source PSRAM=%u bytes; internal DMA=%u bytes (%u descriptors, %u x %u-byte line ring)\n",
                  static_cast<unsigned>(kWidth), static_cast<unsigned>(kHeight),
                  static_cast<unsigned>(timings.frequency), refresh,
                  static_cast<unsigned>(psramBytes_),
                  static_cast<unsigned>(internalDmaBytes_),
                  static_cast<unsigned>(descriptorCount_),
                  static_cast<unsigned>(kLineRingCount),
                  static_cast<unsigned>(kWidth));
    return true;
}

bool PsramDmaScanout::allocateAndBuild(const fabgl::VGATimings& timings) {
    const std::size_t sourceBytes = static_cast<std::size_t>(kWidth) * kHeight;
    const std::size_t horizontalBytes = static_cast<std::size_t>(
        timings.HFrontPorch + timings.HSyncPulse + timings.HBackPorch);
    const std::size_t rawLineBytes = horizontalBytes + kWidth;
    const std::size_t physicalActiveLines = static_cast<std::size_t>(
        timings.VVisibleArea) * timings.scanCount;
    const std::size_t porchLines = static_cast<std::size_t>(
        timings.VFrontPorch + timings.VSyncPulse + timings.VBackPorch) *
        timings.scanCount;
    const std::size_t count = physicalActiveLines * 2 + porchLines;
    if (count > std::numeric_limits<std::uint32_t>::max() / sizeof(lldesc_t))
        return false;

    psramFrame_ = static_cast<std::uint8_t*>(heap_caps_malloc(
        sourceBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    lineRing_ = dmaAlloc(kLineRingCount * kWidth);
    horizontalBlank_ = dmaAlloc(horizontalBytes);
    verticalBlank_ = dmaAlloc(rawLineBytes);
    verticalSync_ = dmaAlloc(rawLineBytes);
    descriptors_ = static_cast<lldesc_t*>(heap_caps_malloc(
        count * sizeof(lldesc_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (!psramFrame_ || !lineRing_ || !horizontalBlank_ || !verticalBlank_ ||
        !verticalSync_ || !descriptors_) {
        Serial.println("PSRAM VGA scanout: allocation failed");
        return false;
    }
    if (!esp_ptr_external_ram(psramFrame_) || !esp_ptr_internal(lineRing_) ||
        !esp_ptr_dma_capable(lineRing_) || !esp_ptr_dma_capable(descriptors_) ||
        !esp_ptr_dma_capable(horizontalBlank_) || !esp_ptr_dma_capable(verticalBlank_) ||
        !esp_ptr_dma_capable(verticalSync_)) {
        Serial.println("PSRAM VGA scanout: allocator returned memory with unexpected capabilities");
        return false;
    }

    // Test frame: eight vertical RGB bars. Every source row is deliberately
    // identical so the three line buffers can be reused by all active-row
    // descriptors without the producer racing DMA during this first slice.
    for (std::uint32_t y = 0; y < kHeight; ++y) {
        auto* row = psramFrame_ + static_cast<std::size_t>(y) * kWidth;
        for (std::uint32_t x = 0; x < kWidth; ++x)
            row[x] = static_cast<std::uint8_t>((x * 8u) / kWidth);
    }

    // RGB222 mapping follows FabGL's single-bit-per-channel pin assignment.
    for (std::uint32_t y = 0; y < kHeight; ++y) {
        auto* out = lineRing_ + (y % kLineRingCount) * kWidth;
        const auto* in = psramFrame_ + static_cast<std::size_t>(y) * kWidth;
        for (std::uint32_t x = 0; x < kWidth; ++x) {
            const auto index = in[x];
            const auto rgb = static_cast<std::uint8_t>(
                ((index & 1u) << 0) | ((index & 2u) << 1) | ((index & 4u) << 2));
            out[x] = syncByte(timings, false, false, rgb);
        }
    }

    for (std::size_t x = 0; x < horizontalBytes; ++x) {
        const auto inHSync = x >= static_cast<std::size_t>(timings.HFrontPorch) &&
            x < static_cast<std::size_t>(timings.HFrontPorch + timings.HSyncPulse);
        horizontalBlank_[x] = syncByte(timings, inHSync, false);
    }
    for (std::size_t x = 0; x < rawLineBytes; ++x) {
        const auto visible = x < kWidth;
        const auto hpos = visible ? horizontalBytes : x - kWidth;
        const auto inHSync = !visible &&
            hpos >= static_cast<std::size_t>(timings.HFrontPorch) &&
            hpos < static_cast<std::size_t>(timings.HFrontPorch + timings.HSyncPulse);
        verticalBlank_[x] = syncByte(timings, inHSync, false);
        verticalSync_[x] = syncByte(timings, inHSync, true);
    }

    // Visible-area-first modeline: each active raster row is a visible buffer
    // followed by horizontal porch/sync bytes. Vertical porch rows are blank.
    std::size_t index = 0;
    for (std::size_t y = 0; y < physicalActiveLines; ++y) {
        auto* row = lineRing_ + ((y / timings.scanCount) % kLineRingCount) * kWidth;
        fillDescriptor(descriptors_[index], row, kWidth, &descriptors_[index + 1]);
        ++index;
        fillDescriptor(descriptors_[index], horizontalBlank_, horizontalBytes,
                       &descriptors_[index + 1]);
        ++index;
    }
    const std::size_t frontLines = static_cast<std::size_t>(timings.VFrontPorch) * timings.scanCount;
    const std::size_t syncLines = static_cast<std::size_t>(timings.VSyncPulse) * timings.scanCount;
    for (std::size_t y = 0; y < porchLines; ++y) {
        auto* porchLine = (y >= frontLines && y < frontLines + syncLines)
            ? verticalSync_ : verticalBlank_;
        fillDescriptor(descriptors_[index], porchLine, rawLineBytes,
                       &descriptors_[index + 1]);
        ++index;
    }
    descriptors_[count - 1].qe.stqe_next = &descriptors_[0];

    descriptorCount_ = count;
    psramBytes_ = sourceBytes;
    internalDmaBytes_ = kLineRingCount * kWidth + horizontalBytes +
        2 * rawLineBytes + count * sizeof(lldesc_t);
    return true;
}

} // namespace ql32::platform::olimex_vga
