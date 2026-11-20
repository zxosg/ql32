#pragma once

#include <cstddef>
#include <cstdint>

#include <fabgl.h>
#include <esp_attr.h>
#include <esp_intr_alloc.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <atomic>

#include "ql32/platform/olimex_vga/framebuffer.hpp"
#include "ql32/platform/olimex_vga/palette.hpp"
#include "ql32/platform/olimex_vga/scanout_geometry.hpp"

namespace ql32::platform::olimex_vga {

// Scanout experiment for the Olimex ESP32-SBC-FabGL. The logical source image
// lives in PSRAM; descriptors and the reusable line ring use internal DMA RAM.
class PsramDmaScanout {
public:
#if defined(QL32_OLIMEX_DMA_TEST_MODE_400X300)
    static constexpr std::uint32_t kOutputWidth = 400;
    static constexpr std::uint32_t kOutputHeight = 300;
#elif defined(QL32_OLIMEX_DMA_TEST_MODE_640X400)
    static constexpr std::uint32_t kOutputWidth = 640;
    static constexpr std::uint32_t kOutputHeight = 400;
#elif defined(QL32_OLIMEX_DMA_TEST_MODE_640X480)
    static constexpr std::uint32_t kOutputWidth = 640;
    static constexpr std::uint32_t kOutputHeight = 480;
#else
    static constexpr std::uint32_t kOutputWidth = 320;
    static constexpr std::uint32_t kOutputHeight = 200;
#endif

    PsramDmaScanout() = default;
    PsramDmaScanout(const PsramDmaScanout&) = delete;
    PsramDmaScanout& operator=(const PsramDmaScanout&) = delete;

    bool begin(const char* modeline, int redPin, int greenPin, int bluePin,
               int hsyncPin, int vsyncPin, bool diagnosticMode = true);
    // Geometry can only change after shutdown is complete.
    bool configureGeometry(std::uint32_t width, std::uint32_t height) noexcept;
    // Start and poll shutdown from the interrupt's allocating core. pollStop
    // never waits for the worker, so a scheduler can run other sessions.
    bool requestStop();
    bool pollStop();
    // Synchronous convenience for isolated startup diagnostics only.
    bool end();
    std::size_t psramBytes() const noexcept { return psramBytes_; }
    std::size_t internalDmaBytes() const noexcept { return internalDmaBytes_; }
    std::uint32_t width() const noexcept { return width_; }
    std::uint32_t height() const noexcept { return height_; }
    PixelFormat framebufferFormat() const noexcept {
#if defined(QL32_OLIMEX_DMA_TEST_PACKED4)
        return PixelFormat::Indexed4;
#else
        return PixelFormat::Rgb222;
#endif
    }
    void reportStatus() const;
    std::uint32_t frameCount() const noexcept { return frameCount_; }
    bool stagePaletteRotation(std::int32_t amount) noexcept;
    bool stagePalette(const Palette::Entries& entries) noexcept;
    FramebufferView* beginFrameUpdate() noexcept;
    FramebufferView* beginFrameUpdate(PixelRect damage) noexcept;
    bool publishFrameUpdate(FramebufferView* frame) noexcept;
    bool publishFrameUpdate(FramebufferView* frame, PixelRect damage) noexcept;
    void cancelFrameUpdate(FramebufferView* frame) noexcept;

private:
    static constexpr std::size_t kFramebufferCount = 3;
    // Keep ring sizes as divisors of each frame height so descriptor slots
    // restart at the same phase on every frame.
#if defined(QL32_OLIMEX_DMA_TEST_MODE_640X480) && defined(QL32_OLIMEX_DMA_RING160_GROUP20_TEST)
    // Diagnostic: 160-row ring refilled in 20-row batches so one stall costs
    // fewer rows and lead stays at 140 rows (160 * 640 B in internal DMA RAM).
    static constexpr std::size_t kLineRingCount = 160;
    static constexpr std::size_t kRefillGroupSize = 20;
#elif defined(QL32_OLIMEX_DMA_TEST_MODE_640X480) && defined(QL32_OLIMEX_DMA_RING160_TEST)
    // Four 40-row groups provide 120 rows of lead in the 480-line mode.
    static constexpr std::size_t kLineRingCount = 160;
    static constexpr std::size_t kRefillGroupSize = 40;
#elif defined(QL32_OLIMEX_DMA_TEST_MODE_640X480)
    // A 120-row ring divides the 480-line frame and provides 60 logical rows
    // of refill lead while remaining below the largest contiguous DMA block.
    static constexpr std::size_t kLineRingCount = 120;
    static constexpr std::size_t kRefillGroupSize = 60;
#elif defined(QL32_OLIMEX_DMA_TEST_MODE_640X400)
    // 100 rows divide the 400-line frame and fit the internal DMA heap.
    static constexpr std::size_t kLineRingCount = 100;
    static constexpr std::size_t kRefillGroupSize = 50;
#elif defined(QL32_OLIMEX_DMA_RING150_TEST)
    // A 150-row ring divides a 300-line frame and gives 75 rows of refill lead.
    static constexpr std::size_t kLineRingCount = 150;
    static constexpr std::size_t kRefillGroupSize = 75;
#elif defined(QL32_OLIMEX_DMA_RING200_TEST)
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
#elif defined(QL32_OLIMEX_DMA_REFILL_PRIORITY1_TEST)
    static constexpr unsigned kRefillTaskPriority = 1;
#else
    static constexpr unsigned kRefillTaskPriority = 4;
#endif
#if defined(QL32_OLIMEX_DMA_REFILL_CORE1_TEST)
    static constexpr BaseType_t kRefillTaskCore = 1;
#else
    static constexpr BaseType_t kRefillTaskCore = 0;
#endif
    bool allocateAndBuild(const fabgl::VGATimings& timings);
    void releaseAllocatedBuffers() noexcept;
    bool startRefillWorker();
    bool startPsramStressWorker();
    static void IRAM_ATTR dmaInterrupt(void* argument);
    void IRAM_ATTR handleDmaInterrupt();
    static void refillWorker(void* argument);
    static void psramStressWorker(void* argument);
    void refillCompletedRows();
    void refillRow(std::uint32_t sourceSequence);
    void encodeRow(std::uint32_t sourceRow, std::uint8_t* output) const;
    void encodePacked4Row(const std::uint8_t* source,
                          std::uint8_t* output) const;
    void rebuildPixelOutputLut();
    static std::uint8_t syncByte(const fabgl::VGATimings& timings,
                                 bool hsync, bool vsync,
                                 std::uint8_t rgb = 0) noexcept;

    fabgl::GPIOStream stream_;
    // The boot profile sets the initial geometry. These instance fields allow
    // a later scanout restart to install a different geometry.
    std::uint32_t width_ = kOutputWidth;
    std::uint32_t height_ = kOutputHeight;
    std::uint32_t ringRows_ = kLineRingCount;
    std::uint32_t refillRows_ = kRefillGroupSize;
    PaletteExchange palette_;
    std::uint8_t* psramFrame_ = nullptr;
    FramebufferView framebuffer_{};
    FramebufferView framebufferSlots_[kFramebufferCount]{};
    std::int8_t activeFrameIndex_ = 0;
    std::int8_t pendingFrameIndex_ = -1;
    std::int8_t stagingFrameIndex_ = -1;
    PixelRect staleFrameDamage_[kFramebufferCount]{};
    PixelRect pendingFrameDamage_{};
    PixelRect stagingFrameDamage_{};
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
    SemaphoreHandle_t stoppedWorkers_ = nullptr;
    std::atomic<bool> stopRequested_{false};
    bool stopPending_ = false;
    unsigned stoppedWorkersRemaining_ = 0;
    BaseType_t interruptCore_ = -1;
    volatile std::uint32_t completedSequence_ = 0;
    volatile std::uint32_t frameCount_ = 0;
    volatile std::uint32_t eofEventCount_ = 0;
    volatile std::uint32_t refillCount_ = 0;
    volatile std::uint32_t lateRefillCount_ = 0;
    volatile std::uint32_t spuriousInterruptCount_ = 0;
    volatile std::uint32_t emptyInterruptCount_ = 0;
    volatile std::uint32_t unexpectedInterruptMask_ = 0;
    volatile std::uint32_t paletteApplyCount_ = 0;
    volatile std::uint32_t frameCloneCount_ = 0;
    volatile std::uint32_t frameCloneBytes_ = 0;
    volatile std::uint32_t pendingFrameReuseCount_ = 0;
#if defined(QL32_OLIMEX_DMA_FRAMEBUFFER_BENCHMARK) || \
    defined(QL32_OLIMEX_DMA_REFILL_PROFILE)
    volatile std::uint32_t refillBatchCount_ = 0;
    volatile std::uint32_t refillBatchMaxMicros_ = 0;
    volatile std::uint32_t refillBatchTotalMicros_ = 0;
    volatile std::uint32_t refillBacklogMaxRows_ = 0;
    volatile std::uint32_t refillBatchMaxRows_ = 0;
    volatile std::uint32_t refillObsoleteRowsSkipped_ = 0;
    volatile std::uint32_t refillActiveSlotOverwriteCount_ = 0;
    volatile std::uint32_t refillRowMaxMicros_ = 0;
#endif
    volatile std::uint32_t lineReadySequence_[200]{};
    volatile std::uint32_t stressBytesWritten_ = 0;
    volatile std::uint32_t stressChecksum_ = 0;
    std::uint8_t pixelOutputLut_[256]{};
    std::uint16_t packed4PairOutputLut_[256]{};
    std::uint32_t nextRefillSequence_ = 0;
    bool haveCompletedSequence_ = false;
    fabgl::VGATimings timings_{};
    std::size_t descriptorCount_ = 0;
    std::size_t psramBytes_ = 0;
    std::size_t internalDmaBytes_ = 0;
    bool diagnosticMode_ = true;
};

} // namespace ql32::platform::olimex_vga
