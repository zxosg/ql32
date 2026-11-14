#pragma once

#include <cstddef>
#include <cstdint>

#include <fabgl.h>
#include <esp_attr.h>
#include <esp_intr_alloc.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "ql32/platform/olimex_vga/framebuffer.hpp"
#include "ql32/platform/olimex_vga/palette.hpp"

namespace ql32::platform::olimex_vga {

// Scanout experiment for the Olimex ESP32-SBC-FabGL. The logical source image
// lives in PSRAM; descriptors and the reusable line ring use internal DMA RAM.
class PsramDmaScanout {
public:
    PsramDmaScanout() = default;
    PsramDmaScanout(const PsramDmaScanout&) = delete;
    PsramDmaScanout& operator=(const PsramDmaScanout&) = delete;

    bool begin(const char* modeline, int redPin, int greenPin, int bluePin,
               int hsyncPin, int vsyncPin, bool diagnosticMode = true);
    std::size_t psramBytes() const noexcept { return psramBytes_; }
    std::size_t internalDmaBytes() const noexcept { return internalDmaBytes_; }
    std::uint32_t width() const noexcept { return 320; }
    std::uint32_t height() const noexcept { return 200; }
    PixelFormat framebufferFormat() const noexcept {
#if defined(QL32_OLIMEX_DMA_TEST_PACKED4)
        return PixelFormat::Indexed4;
#else
        return PixelFormat::Rgb222;
#endif
    }
    void reportStatus() const;
    bool stagePaletteRotation(std::int32_t amount) noexcept;
    bool stagePalette(const Palette::Entries& entries) noexcept;
    FramebufferView* beginFrameUpdate() noexcept;
    bool publishFrameUpdate(FramebufferView* frame) noexcept;
    void cancelFrameUpdate(FramebufferView* frame) noexcept;

private:
    static constexpr std::size_t kFramebufferCount = 3;
    // The descriptor table restarts at row zero every 200 logical lines, so
    // the ring must divide the frame height to preserve its slot phase.
#if defined(QL32_OLIMEX_DMA_RING200_TEST)
    // Temporary timing experiment: one full frame of line storage provides
    // 150 rows of lead while preserving 50-row EOF/refill batches.
    static constexpr std::size_t kLineRingCount = 200;
    static constexpr std::size_t kRefillGroupSize = 50;
#else
    static constexpr std::size_t kLineRingCount = 100;
    static constexpr std::size_t kRefillGroupSize = kLineRingCount / 2;
#endif
#if defined(QL32_OLIMEX_DMA_REFILL_PRIORITY7_TEST)
    static constexpr unsigned kRefillTaskPriority = 7;
#else
    static constexpr unsigned kRefillTaskPriority = 4;
#endif
    bool allocateAndBuild(const fabgl::VGATimings& timings);
    bool startRefillWorker();
    bool startPsramStressWorker();
    static void IRAM_ATTR dmaInterrupt(void* argument);
    void IRAM_ATTR handleDmaInterrupt();
    static void refillWorker(void* argument);
    static void psramStressWorker(void* argument);
    void refillCompletedRows();
    void refillRow(std::uint32_t sourceSequence);
    void encodeRow(std::uint32_t sourceRow, std::uint8_t* output) const;
    void rebuildPixelOutputLut();
    static std::uint8_t syncByte(const fabgl::VGATimings& timings,
                                 bool hsync, bool vsync,
                                 std::uint8_t rgb = 0) noexcept;

    fabgl::GPIOStream stream_;
    PaletteExchange palette_;
    std::uint8_t* psramFrame_ = nullptr;
    FramebufferView framebuffer_{};
    FramebufferView framebufferSlots_[kFramebufferCount]{};
    std::int8_t activeFrameIndex_ = 0;
    std::int8_t pendingFrameIndex_ = -1;
    std::int8_t stagingFrameIndex_ = -1;
    portMUX_TYPE frameMux_ = portMUX_INITIALIZER_UNLOCKED;
    std::uint8_t* lineRing_ = nullptr;
    std::uint8_t* horizontalBlank_ = nullptr;
    std::uint8_t* verticalBlank_ = nullptr;
    std::uint8_t* verticalSync_ = nullptr;
    std::uint8_t* psramStress_ = nullptr;
    lldesc_t* descriptors_ = nullptr;
    TaskHandle_t refillTask_ = nullptr;
    TaskHandle_t stressTask_ = nullptr;
    intr_handle_t interruptHandle_ = nullptr;
    volatile std::uint32_t completedSequence_ = 0;
    volatile std::uint32_t frameCount_ = 0;
    volatile std::uint32_t eofEventCount_ = 0;
    volatile std::uint32_t refillCount_ = 0;
    volatile std::uint32_t lateRefillCount_ = 0;
    volatile std::uint32_t spuriousInterruptCount_ = 0;
    volatile std::uint32_t paletteApplyCount_ = 0;
    volatile std::uint32_t frameCloneCount_ = 0;
    volatile std::uint32_t frameCloneBytes_ = 0;
    volatile std::uint32_t pendingFrameReuseCount_ = 0;
#if defined(QL32_OLIMEX_DMA_FRAMEBUFFER_BENCHMARK)
    volatile std::uint32_t refillBatchCount_ = 0;
    volatile std::uint32_t refillBatchMaxMicros_ = 0;
    volatile std::uint32_t refillBacklogMaxRows_ = 0;
    volatile std::uint32_t refillBatchMaxRows_ = 0;
    volatile std::uint32_t refillObsoleteRowsSkipped_ = 0;
#endif
    volatile std::uint32_t lineReadySequence_[kLineRingCount]{};
    volatile std::uint32_t stressBytesWritten_ = 0;
    volatile std::uint32_t stressChecksum_ = 0;
    std::uint8_t pixelOutputLut_[256]{};
    std::uint32_t nextRefillSequence_ = 0;
    bool haveCompletedSequence_ = false;
    fabgl::VGATimings timings_{};
    std::size_t descriptorCount_ = 0;
    std::size_t psramBytes_ = 0;
    std::size_t internalDmaBytes_ = 0;
    bool diagnosticMode_ = true;
};

} // namespace ql32::platform::olimex_vga
