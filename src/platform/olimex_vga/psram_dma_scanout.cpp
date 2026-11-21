#include "ql32/platform/olimex_vga/psram_dma_scanout.hpp"
#include "ql32/platform/olimex_vga/refill_window.hpp"
#include "ql32/platform/olimex_vga/dma_pixel_order.hpp"
#if defined(QL32_OLIMEX_DMA_TEST_PIXEL_EDGES)
#include "ql32/graphics/window_manager.hpp"
#endif

#include <driver/gpio.h>
#include <Arduino.h>
#include <esp_intr_alloc.h>
#include <esp_heap_caps.h>
#include <esp_attr.h>
#include <rom/lldesc.h>
#include <soc/gpio_sig_map.h>
#include <soc/i2s_reg.h>
#include <soc/soc_memory_types.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstdio>
#include <cstring>
#include <limits>

// Experimental: keep the row refill code in IRAM so core-1 refills do not take
// instruction-cache misses on the flash/PSRAM bus shared with the other core.
#if defined(QL32_OLIMEX_DMA_REFILL_IRAM_TEST)
#define QL32_REFILL_HOT IRAM_ATTR
#else
#define QL32_REFILL_HOT
#endif

namespace ql32::platform::olimex_vga {
namespace {

#if defined(QL32_OLIMEX_DMA_TEST_PACKED4)
constexpr PixelFormat kFramebufferFormat = PixelFormat::Indexed4;
#else
constexpr PixelFormat kFramebufferFormat = PixelFormat::Rgb222;
#endif
constexpr std::size_t kPsramStressBytes = 64 * 1024;
constexpr int kRefillTaskStackBytes = 4096;
constexpr int kStressTaskStackBytes = 4096;
#if defined(QL32_OLIMEX_DMA_DISPLAY_DIAGNOSTIC) || \
    defined(QL32_OLIMEX_DMA_FRAMEBUFFER_BENCHMARK) || \
    defined(QL32_OLIMEX_DMA_TEST_PIXEL_EDGES) || \
    defined(QL32_OLIMEX_DMA_TEST_TRIANGLE) || \
    defined(QL32_OLIMEX_DMA_NO_STRESS_TEST)
// Keep the low-transition scanout marker while isolating runtime scheduling
// from the optional competing PSRAM writer. The framebuffer benchmark itself
// supplies the PSRAM write load and must not be combined with this extra loop.
constexpr bool kEnablePsramStress = false;
#else
constexpr bool kEnablePsramStress = true;
#endif
#if defined(QL32_OLIMEX_DMA_TEST_STATIC_SCANOUT)
// Isolate modeline/descriptor timing from refill scheduling. The initialized
// line ring is left untouched after DMA starts, and no EOF worker is created.
constexpr bool kEnableLiveRefill = false;
constexpr bool kEnableDmaEofInterrupt = false;
#else
constexpr bool kEnableLiveRefill = true;
constexpr bool kEnableDmaEofInterrupt = true;
#endif

std::uint8_t* dmaAlloc(std::size_t bytes) {
    return static_cast<std::uint8_t*>(heap_caps_malloc(
        bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
}

std::uint8_t outputRgb(std::uint8_t color) {
    // One output bit per RGB channel, selected from each RGB222 channel's MSB.
    return static_cast<std::uint8_t>(
        ((color >> 5) & 1u) | (((color >> 3) & 1u) << 2) |
        (((color >> 1) & 1u) << 4));
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

bool PsramDmaScanout::stagePaletteRotation(std::int32_t amount) noexcept {
    auto* staging = palette_.beginEdit();
    if (!staging) return false;
    staging->rotate(amount);
    return palette_.publish();
}

bool PsramDmaScanout::stagePalette(const Palette::Entries& entries) noexcept {
    auto* staging = palette_.beginEdit();
    if (!staging) return false;
    staging->load(entries);
    return palette_.publish();
}

FramebufferView* PsramDmaScanout::beginFrameUpdate() noexcept {
    return beginFrameUpdate({0, 0, width_, height_});
}

FramebufferView* PsramDmaScanout::beginFrameUpdate(PixelRect damage) noexcept {
    if (stopRequested_.load(std::memory_order_acquire)) return nullptr;
    if (damage.empty() || damage.x > width_ || damage.y > height_ ||
        damage.width > width_ - damage.x || damage.height > height_ - damage.y)
        return nullptr;
#if defined(QL32_OLIMEX_SINGLE_FRAMEBUFFER)
    // The framebuffer is the live scanout surface; any number of producers
    // may write it. Neighbouring packed-4 nibbles are protected by
    // storePackedNibble, and the refill worker never takes a lock.
    activeWriters_.fetch_add(1, std::memory_order_acq_rel);
    if (stopRequested_.load(std::memory_order_acquire) ||
        !framebufferSlots_[0].valid()) {
        leaveWriter();
        return nullptr;
    }
    return &framebufferSlots_[0];
}

void PsramDmaScanout::leaveWriter() noexcept {
    auto count = activeWriters_.load(std::memory_order_acquire);
    while (count != 0 && !activeWriters_.compare_exchange_weak(
               count, count - 1, std::memory_order_acq_rel)) {}
}

bool PsramDmaScanout::publishFrameUpdate(FramebufferView* frame) noexcept {
    if (!frame || frame->pixels != framebufferSlots_[0].pixels) return false;
    leaveWriter();
    return true;
}

bool PsramDmaScanout::publishFrameUpdate(FramebufferView* frame,
                                         PixelRect) noexcept {
    return publishFrameUpdate(frame);
}

void PsramDmaScanout::cancelFrameUpdate(FramebufferView* frame) noexcept {
    if (frame) leaveWriter();
}
#else
    portENTER_CRITICAL(&frameMux_);
    if (stagingFrameIndex_ >= 0) {
        portEXIT_CRITICAL(&frameMux_);
        return nullptr;
    }
    if (pendingFrameIndex_ >= 0) {
        // A pending frame is not visible to DMA yet. Temporarily detach it
        // from the frame-boundary swap so consecutive updates can accumulate
        // safely without cloning another full PSRAM frame.
        stagingFrameIndex_ = pendingFrameIndex_;
        pendingFrameIndex_ = -1;
        stagingFrameDamage_ = pendingFrameDamage_;
        auto* destination = &framebufferSlots_[stagingFrameIndex_];
        portEXIT_CRITICAL(&frameMux_);
        ++pendingFrameReuseCount_;
        return destination;
    }
    std::int8_t freeIndex = -1;
    for (std::int8_t index = 0; index < static_cast<std::int8_t>(kFramebufferCount); ++index) {
        if (index != activeFrameIndex_) {
            freeIndex = index;
            break;
        }
    }
    if (freeIndex < 0) {
        portEXIT_CRITICAL(&frameMux_);
        return nullptr;
    }
    stagingFrameIndex_ = freeIndex;
    auto* destination = &framebufferSlots_[freeIndex];
    const auto source = framebufferSlots_[activeFrameIndex_];
    stagingFrameDamage_ = staleFrameDamage_[freeIndex];
    staleFrameDamage_[freeIndex] = {};
    portEXIT_CRITICAL(&frameMux_);

    if (!stagingFrameDamage_.empty()) {
        const auto syncDamage = stagingFrameDamage_;
        if (!copyRegion(source, *destination, syncDamage.x, syncDamage.y,
                        syncDamage.width, syncDamage.height)) {
            portENTER_CRITICAL(&frameMux_);
            staleFrameDamage_[freeIndex] = unite(staleFrameDamage_[freeIndex],
                                                  syncDamage);
            stagingFrameIndex_ = -1;
            stagingFrameDamage_ = {};
            portEXIT_CRITICAL(&frameMux_);
            return nullptr;
        }
        stagingFrameDamage_ = {};
        const auto bytesPerRow = kFramebufferFormat == PixelFormat::Rgb222
            ? syncDamage.width
            : (syncDamage.width + (syncDamage.x & 1u) + 1u) / 2u;
        frameCloneBytes_ += bytesPerRow * syncDamage.height;
    }
    ++frameCloneCount_;
    return destination;
}

bool PsramDmaScanout::publishFrameUpdate(FramebufferView* frame) noexcept {
    return publishFrameUpdate(frame, {0, 0, width_, height_});
}

bool PsramDmaScanout::publishFrameUpdate(FramebufferView* frame,
                                         PixelRect damage) noexcept {
    if (!frame) return false;
    if (damage.empty() || damage.x > width_ || damage.y > height_ ||
        damage.width > width_ - damage.x || damage.height > height_ - damage.y)
        return false;
    portENTER_CRITICAL(&frameMux_);
    if (stagingFrameIndex_ < 0 ||
        frame->pixels != framebufferSlots_[stagingFrameIndex_].pixels) {
        portEXIT_CRITICAL(&frameMux_);
        return false;
    }
    stagingFrameDamage_ = unite(stagingFrameDamage_, damage);
    pendingFrameDamage_ = stagingFrameDamage_;
    pendingFrameIndex_ = stagingFrameIndex_;
    stagingFrameIndex_ = -1;
    portEXIT_CRITICAL(&frameMux_);
    return true;
}

void PsramDmaScanout::cancelFrameUpdate(FramebufferView* frame) noexcept {
    if (!frame) return;
    portENTER_CRITICAL(&frameMux_);
    if (stagingFrameIndex_ >= 0 &&
        frame->pixels == framebufferSlots_[stagingFrameIndex_].pixels) {
        staleFrameDamage_[stagingFrameIndex_] = {0, 0, width_, height_};
        stagingFrameIndex_ = -1;
        stagingFrameDamage_ = {};
        pendingFrameDamage_ = {};
    }
    portEXIT_CRITICAL(&frameMux_);
}
#endif

bool PsramDmaScanout::configureGeometry(std::uint32_t width,
                                        std::uint32_t height) noexcept {
    if (descriptors_ || refillTask_ || interruptHandle_ || stopPending_) return false;
    const auto geometry = scanoutGeometry(width, height);
    if (!geometry.valid()) return false;
    width_ = geometry.width;
    height_ = geometry.height;
    ringRows_ = geometry.ringRows;
    refillRows_ = geometry.refillRows;
    return true;
}

bool PsramDmaScanout::begin(const char* modeline, int redPin, int greenPin,
                            int bluePin, int hsyncPin, int vsyncPin,
                            bool diagnosticMode) {
    if (descriptors_ || refillTask_ || interruptHandle_ || stopPending_) return false;
    fabgl::VGATimings timings{};
    if (!modeline || !fabgl::VGABaseController::convertModelineToTimings(
                         modeline, &timings)) {
        Serial.println("PSRAM VGA scanout: invalid FabGL modeline");
        return false;
    }
    if (timings.HVisibleArea != static_cast<int>(width_) ||
        timings.VVisibleArea != static_cast<int>(height_) ||
        timings.HStartingBlock != fabgl::VGAScanStart::VisibleArea) {
        Serial.printf("PSRAM VGA scanout: test path requires %ux%u visible-area-first timing\n",
                      static_cast<unsigned>(width_), static_cast<unsigned>(height_));
        return false;
    }
    if (!ScanoutGeometry{width_, height_, ringRows_, refillRows_}.valid()) {
        Serial.println("PSRAM VGA scanout: invalid line-ring geometry");
        return false;
    }
    diagnosticMode_ = diagnosticMode;
    stopRequested_.store(false, std::memory_order_release);
    completedSequence_ = frameCount_ = eofEventCount_ = refillCount_ = 0;
    lateRefillCount_ = spuriousInterruptCount_ = emptyInterruptCount_ = 0;
#if defined(QL32_OLIMEX_DMA_FRAMEBUFFER_BENCHMARK) || \
    defined(QL32_OLIMEX_DMA_REFILL_PROFILE)
    refillActiveSlotOverwriteCount_ = refillRowMaxMicros_ = 0;
#endif
    unexpectedInterruptMask_ = paletteApplyCount_ = frameCloneCount_ = 0;
    frameCloneBytes_ = pendingFrameReuseCount_ = 0;
    stressBytesWritten_ = stressChecksum_ = 0;
    nextRefillSequence_ = 0;
    haveCompletedSequence_ = false;
    timings_ = timings;
    rebuildPixelOutputLut();

    connectOutputPin(redPin, 0);
    connectOutputPin(greenPin, 2);
    connectOutputPin(bluePin, 4);
    connectOutputPin(hsyncPin, 6);
    connectOutputPin(vsyncPin, 7);
    stream_.begin();

    const auto dmaHeapBefore = heap_caps_get_free_size(
        MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    Serial.printf("PSRAM VGA DMA heap before allocation: free=%u, largest=%u bytes\n",
                  static_cast<unsigned>(dmaHeapBefore),
                  static_cast<unsigned>(heap_caps_get_largest_free_block(
                      MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
    if (!allocateAndBuild(timings)) return false;
    if (kEnableDmaEofInterrupt) {
        // Allocate the interrupt before starting any workers. Until play()
        // enables EOF there is no work for the refill task, so failure of the
        // subsequent stress-task creation can safely delete the blocked task.
#if defined(QL32_OLIMEX_DMA_INTERRUPT_CORE1_TEST)
        // Keep the time-sensitive EOF interrupt beside the refill worker when
        // the QL/32 Arduino loop is explicitly moved to the other core.
        interruptCore_ = 1;
#else
        interruptCore_ = xPortGetCoreID();
#endif
        fabgl::esp_intr_alloc_pinnedToCore(ETS_I2S1_INTR_SOURCE,
                ESP_INTR_FLAG_LEVEL1 | ESP_INTR_FLAG_IRAM, dmaInterrupt, this,
                &interruptHandle_, interruptCore_);
        if (!interruptHandle_) {
            Serial.println("PSRAM VGA scanout: unable to allocate I2S1 EOF interrupt");
            releaseAllocatedBuffers();
            return false;
        }
        if (!startRefillWorker()) {
            esp_intr_free(interruptHandle_);
            interruptHandle_ = nullptr;
            releaseAllocatedBuffers();
            return false;
        }
        if (diagnosticMode_ && kEnablePsramStress && !startPsramStressWorker()) {
            esp_intr_free(interruptHandle_);
            interruptHandle_ = nullptr;
            vTaskDelete(refillTask_);
            refillTask_ = nullptr;
            releaseAllocatedBuffers();
            return false;
        }
    }
    stream_.play(timings.frequency, descriptors_);
    if (kEnableDmaEofInterrupt) {
        I2S1.int_clr.val = 0xFFFFFFFF;
        I2S1.int_ena.out_eof = 1;
    }
    const auto hTotal = timings.HFrontPorch + timings.HSyncPulse +
        timings.HBackPorch + timings.HVisibleArea;
    const auto vTotal = timings.VVisibleArea + timings.VFrontPorch +
        timings.VSyncPulse + timings.VBackPorch;
    const float refresh = static_cast<float>(timings.frequency) /
        (hTotal * vTotal * timings.scanCount);
    Serial.printf("PSRAM VGA scanout started: %ux%u, scan-count=%u, pixel clock=%u Hz, nominal refresh=%.2f Hz; format=%s, stride=%u, source PSRAM=%u bytes; internal DMA=%u bytes (%u descriptors, %u x %u-byte line ring; refill every %u rows)\n",
                  static_cast<unsigned>(width_), static_cast<unsigned>(height_),
                  static_cast<unsigned>(timings.scanCount),
                  static_cast<unsigned>(timings.frequency), refresh,
                  kFramebufferFormat == PixelFormat::Rgb222 ? "RGB222" : "indexed4",
                  static_cast<unsigned>(framebuffer_.stride),
                  static_cast<unsigned>(psramBytes_),
                  static_cast<unsigned>(internalDmaBytes_),
                  static_cast<unsigned>(descriptorCount_),
                  static_cast<unsigned>(ringRows_),
                  static_cast<unsigned>(width_),
                  static_cast<unsigned>(refillRows_));
    Serial.printf("PSRAM VGA workers: EOF=%s, live refill=%s, refill core=%d, PSRAM stress=%s (%u bytes), DMA heap remaining=%u bytes\n",
                  kEnableDmaEofInterrupt ? "enabled" : "disabled",
                  kEnableLiveRefill ? "enabled" : "disabled",
                  static_cast<int>(kRefillTaskCore),
                  diagnosticMode_ && kEnablePsramStress ? "enabled" : "disabled",
                  static_cast<unsigned>(diagnosticMode_ && kEnablePsramStress
                      ? kPsramStressBytes : 0),
                  static_cast<unsigned>(heap_caps_get_free_size(
                      MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
    return true;
}

bool PsramDmaScanout::requestStop() {
    if (stopPending_) return true;
    if (!descriptors_) return true;
    if (interruptHandle_ && xPortGetCoreID() != interruptCore_) return false;
    portENTER_CRITICAL(&frameMux_);
    const bool frameInUse = stagingFrameIndex_ >= 0;
    portEXIT_CRITICAL(&frameMux_);
    if (frameInUse) return false;

    const unsigned workerCount = (refillTask_ ? 1u : 0u) +
                                 (stressTask_ ? 1u : 0u);
    if (workerCount) {
        stoppedWorkers_ = xSemaphoreCreateCounting(workerCount, 0);
        if (!stoppedWorkers_) return false;
    }
    if (interruptHandle_) {
        I2S1.int_ena.out_eof = 0;
        if (esp_intr_free(interruptHandle_) != ESP_OK) {
            I2S1.int_ena.out_eof = 1;
            if (stoppedWorkers_) vSemaphoreDelete(stoppedWorkers_);
            stoppedWorkers_ = nullptr;
            return false;
        }
        interruptHandle_ = nullptr;
    }
    stream_.stop();
    stopRequested_.store(true, std::memory_order_release);
    stopPending_ = true;
    stoppedWorkersRemaining_ = workerCount;
    if (refillTask_) xTaskNotifyGive(refillTask_);
    return true;
}

bool PsramDmaScanout::pollStop() {
    if (!stopPending_) return !descriptors_;
    while (stoppedWorkersRemaining_ &&
           xSemaphoreTake(stoppedWorkers_, 0) == pdTRUE)
        --stoppedWorkersRemaining_;
    if (stoppedWorkersRemaining_) return false;
    if (refillTask_) vTaskDelete(refillTask_);
    if (stressTask_) vTaskDelete(stressTask_);
    refillTask_ = stressTask_ = nullptr;
    if (stoppedWorkers_) vSemaphoreDelete(stoppedWorkers_);
    stoppedWorkers_ = nullptr;
    interruptCore_ = -1;
    releaseAllocatedBuffers();
    stopPending_ = false;
    return true;
}

bool PsramDmaScanout::end() {
    if (!requestStop()) return false;
    while (!pollStop()) vTaskDelay(1);
    return true;
}

void PsramDmaScanout::releaseAllocatedBuffers() noexcept {
#if defined(QL32_OLIMEX_SINGLE_FRAMEBUFFER)
    // Let producers that already hold the framebuffer finish; new ones are
    // refused once stop is requested. If one never returns, leak the 60 KB
    // rather than free memory it may still write.
    bool leakFramebuffer = false;
    for (int waited = 0; activeWriters_.load(std::memory_order_acquire) != 0; ++waited) {
        if (waited >= 200) {
            leakFramebuffer = true;
            Serial.println("PSRAM VGA scanout: framebuffer writer still active; framebuffer kept allocated");
            break;
        }
        vTaskDelay(1);
    }
    if (leakFramebuffer) {
        framebufferSlots_[0] = {};
    }
#endif
    for (auto& slot : framebufferSlots_) {
        heap_caps_free(slot.pixels);
        slot = {};
    }
    heap_caps_free(lineRing_);
    heap_caps_free(horizontalBlank_);
    heap_caps_free(verticalBlank_);
    heap_caps_free(verticalSync_);
    heap_caps_free(psramStress_);
    heap_caps_free(descriptors_);
    framebuffer_ = {};
    psramFrame_ = nullptr;
    lineRing_ = nullptr;
    horizontalBlank_ = nullptr;
    verticalBlank_ = nullptr;
    verticalSync_ = nullptr;
    psramStress_ = nullptr;
    descriptors_ = nullptr;
    descriptorCount_ = 0;
    psramBytes_ = 0;
    internalDmaBytes_ = 0;
    activeFrameIndex_ = 0;
    pendingFrameIndex_ = stagingFrameIndex_ = -1;
    for (auto& damage : staleFrameDamage_) damage = {};
    pendingFrameDamage_ = stagingFrameDamage_ = {};
}

bool PsramDmaScanout::allocateAndBuild(const fabgl::VGATimings& timings) {
    const auto stride = kFramebufferFormat == PixelFormat::Rgb222
        ? static_cast<std::size_t>(width_)
        : (static_cast<std::size_t>(width_) + 1) / 2;
    const std::size_t sourceBytes = stride * height_;
    const std::size_t horizontalBytes = static_cast<std::size_t>(
        timings.HFrontPorch + timings.HSyncPulse + timings.HBackPorch);
    const std::size_t rawLineBytes = horizontalBytes + width_;
    const std::size_t physicalActiveLines = static_cast<std::size_t>(
        timings.VVisibleArea) * timings.scanCount;
    const std::size_t porchLines = static_cast<std::size_t>(
        timings.VFrontPorch + timings.VSyncPulse + timings.VBackPorch) *
        timings.scanCount;
    const std::size_t count = physicalActiveLines * 2 + porchLines;
    if (count > std::numeric_limits<std::uint32_t>::max() / sizeof(lldesc_t))
        return false;

    for (std::size_t i = 0; i < kFramebufferCount; ++i) {
        framebufferSlots_[i].pixels = static_cast<std::uint8_t*>(heap_caps_malloc(
            sourceBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        framebufferSlots_[i].width = width_;
        framebufferSlots_[i].height = height_;
        framebufferSlots_[i].stride = stride;
        framebufferSlots_[i].format = kFramebufferFormat;
        if (framebufferSlots_[i].pixels)
            std::memset(framebufferSlots_[i].pixels, 0, sourceBytes);
    }
    activeFrameIndex_ = 0;
    pendingFrameIndex_ = -1;
    stagingFrameIndex_ = -1;
    for (auto& damage : staleFrameDamage_) damage = {};
    pendingFrameDamage_ = stagingFrameDamage_ = {};
    framebuffer_ = framebufferSlots_[activeFrameIndex_];
    psramFrame_ = framebuffer_.pixels;
    lineRing_ = dmaAlloc(ringRows_ * width_);
    horizontalBlank_ = dmaAlloc(horizontalBytes);
    verticalBlank_ = dmaAlloc(rawLineBytes);
    verticalSync_ = dmaAlloc(rawLineBytes);
    descriptors_ = static_cast<lldesc_t*>(heap_caps_malloc(
        count * sizeof(lldesc_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    bool allFramebuffersValid = framebuffer_.valid() &&
        framebuffer_.storageBytes() == sourceBytes;
    for (const auto& slot : framebufferSlots_) allFramebuffersValid &= slot.valid();
    if (!allFramebuffersValid ||
        !lineRing_ || !horizontalBlank_ || !verticalBlank_ ||
        !verticalSync_ || !descriptors_) {
        Serial.println("PSRAM VGA scanout: allocation failed");
        releaseAllocatedBuffers();
        return false;
    }
    if (diagnosticMode_ && kEnablePsramStress) {
        psramStress_ = static_cast<std::uint8_t*>(heap_caps_malloc(
            kPsramStressBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!psramStress_) {
            Serial.println("PSRAM VGA scanout: unable to allocate PSRAM stress buffer");
            releaseAllocatedBuffers();
            return false;
        }
    }
    if (!esp_ptr_external_ram(psramFrame_) || !esp_ptr_internal(lineRing_) ||
        !esp_ptr_dma_capable(lineRing_) || !esp_ptr_dma_capable(descriptors_) ||
        !esp_ptr_dma_capable(horizontalBlank_) || !esp_ptr_dma_capable(verticalBlank_) ||
        !esp_ptr_dma_capable(verticalSync_)) {
        Serial.println("PSRAM VGA scanout: allocator returned memory with unexpected capabilities");
        releaseAllocatedBuffers();
        return false;
    }

    // Test-only source patterns bypass the BASIC presenter.
    if (diagnosticMode_) {
#if defined(QL32_OLIMEX_DMA_TEST_TRIANGLE)
        // Framebuffers are zeroed at allocation, so the only visible content
        // in this profile is a solid right triangle on black.
        constexpr std::uint32_t triangleX = 80;
        constexpr std::uint32_t triangleY = 50;
        constexpr std::uint32_t triangleSize = 96;
        for (std::uint32_t row = 0; row < triangleSize; ++row)
            fillRect(framebuffer_, triangleX, triangleY + row, row + 1, 1, 4);
#elif defined(QL32_OLIMEX_DMA_TEST_HORIZONTAL_BANDS)
        // Eight full-width horizontal bands exercise row-to-row transitions
        // without changing the source image or row encoding over time.
        for (std::uint32_t y = 0; y < height_; ++y) {
            const auto band = (y * 8u) / height_;
            fillRect(framebuffer_, 0, y, width_, 1,
                     static_cast<std::uint8_t>((band & 1u) ? 4u : 0u));
        }
#elif defined(QL32_OLIMEX_DMA_TEST_GRADIENT)
        // A full-width 16-level indexed gradient; the refill path scrolls it
        // horizontally while the diagnostic loop can cycle its palette.
        for (std::uint32_t y = 0; y < height_; ++y)
            for (std::uint32_t x = 0; x < width_; ++x)
                (void)writePixel(framebuffer_, x, y,
                    static_cast<std::uint8_t>((x * 16u) / width_));
#elif defined(QL32_OLIMEX_DMA_TEST_CHECKER)
        // Stationary full-screen high-transition source. Unlike the moving
        // bars, this keeps both source pixels and palette fixed while testing
        // the same packed-4 row encoder and live refill path.
        for (std::uint32_t y = 0; y < height_; ++y)
            for (std::uint32_t x = 0; x < width_; ++x)
                (void)writePixel(framebuffer_, x, y,
                    static_cast<std::uint8_t>(((x + y) & 1u) ? 4u : 0u));
#else
        for (std::uint32_t y = 0; y < height_; ++y) {
            for (std::uint32_t x = 0; x < width_; ++x) {
#if defined(QL32_OLIMEX_DMA_TEST_PIXEL_EDGES)
                const auto index = static_cast<std::uint8_t>(
                    x < width_ / 2 ? ((x & 1u) ? 4u : 0u) :
                    (y < height_ / 2 ? ((y & 1u) ? 4u : 0u) :
                     (((x + y) & 1u) ? 4u : 0u)));
#else
                const auto bar = (x * 8u) / width_;
                const auto index = static_cast<std::uint8_t>(x < 8 ? (y & 7u) : bar);
#endif
                const auto value = kFramebufferFormat == PixelFormat::Rgb222
                    ? palette_.active()[index] : index;
                (void)writePixel(framebuffer_, x, y, value);
            }
        }
#if defined(QL32_OLIMEX_DMA_TEST_PIXEL_EDGES)
        // Put the same portable 5x7 glyph source directly into the raw
        // framebuffer. This distinguishes DMA scanout of glyph-shaped pixels
        // from the QL/32 canvas composition and frame-presenter path.
        constexpr char text[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
        constexpr std::uint32_t textX = 10;
        constexpr std::uint32_t textY = 154;
        constexpr std::uint32_t cellWidth = 6;
        fillRect(framebuffer_, 8, 150, width_ - 16, 16, 0);
        const auto ink = kFramebufferFormat == PixelFormat::Rgb222
            ? palette_.active()[4] : 4;
        for (std::uint32_t character = 0;
             character < sizeof(text) - 1; ++character) {
            const auto columns = graphics::classicGlyphColumns(text[character]);
            for (std::uint32_t column = 0; column < columns.size(); ++column)
                for (std::uint32_t row = 0; row < 7; ++row)
                    if (columns[column] & (1u << row))
                        (void)writePixel(framebuffer_,
                            textX + character * cellWidth + column,
                            textY + row, ink);
        }
#endif
#endif
    }

    // Seed the internal ring. The worker refills consumed rows in batches
    // after the DMA EOF interrupt confirms that they have been read.
    for (std::uint32_t y = 0; y < ringRows_; ++y) {
        auto* out = lineRing_ + (y % ringRows_) * width_;
        encodeRow(y % height_, out);
        lineReadySequence_[y] = y;
    }
    if (diagnosticMode_ && kEnablePsramStress) {
        for (std::size_t i = 0; i < kPsramStressBytes; ++i)
            psramStress_[i] = static_cast<std::uint8_t>((i * 37u) ^ (i >> 8));
    }

    for (std::size_t x = 0; x < horizontalBytes; ++x) {
        const auto inHSync = x >= static_cast<std::size_t>(timings.HFrontPorch) &&
            x < static_cast<std::size_t>(timings.HFrontPorch + timings.HSyncPulse);
        horizontalBlank_[x] = syncByte(timings, inHSync, false);
    }
    for (std::size_t x = 0; x < rawLineBytes; ++x) {
        const auto visible = x < width_;
        const auto hpos = visible ? horizontalBytes : x - width_;
        const auto inHSync = !visible &&
            hpos >= static_cast<std::size_t>(timings.HFrontPorch) &&
            hpos < static_cast<std::size_t>(timings.HFrontPorch + timings.HSyncPulse);
        verticalBlank_[x] = syncByte(timings, inHSync, false);
        verticalSync_[x] = syncByte(timings, inHSync, true);
    }

    // Visible-area-first modeline: each active raster row is a visible buffer
    // followed by horizontal porch/sync bytes. Vertical porch rows are blank.
    std::size_t index = 0;
    const auto physicalRowsPerRefill = refillRows_ * timings.scanCount;
    for (std::size_t y = 0; y < physicalActiveLines; ++y) {
        auto* row = lineRing_ + ((y / timings.scanCount) % ringRows_) * width_;
        fillDescriptor(descriptors_[index], row, width_, &descriptors_[index + 1]);
        descriptors_[index].eof = kEnableDmaEofInterrupt &&
            ((y + 1) % physicalRowsPerRefill) == 0;
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
    psramBytes_ = sourceBytes * kFramebufferCount +
        (diagnosticMode_ && kEnablePsramStress ? kPsramStressBytes : 0);
    internalDmaBytes_ = ringRows_ * width_ + horizontalBytes +
        2 * rawLineBytes + count * sizeof(lldesc_t);
    return true;
}

bool PsramDmaScanout::startRefillWorker() {
    const auto result = xTaskCreatePinnedToCore(refillWorker, "VgaRefill",
        kRefillTaskStackBytes, this, kRefillTaskPriority, &refillTask_,
        kRefillTaskCore);
    if (result != pdPASS) {
        Serial.println("PSRAM VGA scanout: unable to create line-refill task");
        return false;
    }
    return true;
}

bool PsramDmaScanout::startPsramStressWorker() {
    const auto result = xTaskCreatePinnedToCore(psramStressWorker, "VgaPsramStress",
        kStressTaskStackBytes, this, 1, &stressTask_, 0);
    if (result != pdPASS) {
        Serial.println("PSRAM VGA scanout: unable to create PSRAM stress task");
        return false;
    }
    return true;
}

void IRAM_ATTR PsramDmaScanout::dmaInterrupt(void* argument) {
    static_cast<PsramDmaScanout*>(argument)->handleDmaInterrupt();
}

void IRAM_ATTR PsramDmaScanout::handleDmaInterrupt() {
    BaseType_t higherPriorityTaskWoken = pdFALSE;
    const auto status = I2S1.int_st.val;
    // Acknowledge only the events observed in this sample. Reading status a
    // second time can clear a new EOF that arrived while handling the first.
    I2S1.int_clr.val = status;
    if (status & I2S_OUT_EOF_INT_ST_M) {
        const auto sequence = haveCompletedSequence_
            ? completedSequence_ + refillRows_
            : refillRows_ - 1;
        const auto nextSequence = sequence + 1;
        if (lineReadySequence_[nextSequence % ringRows_] != nextSequence)
            ++lateRefillCount_;
        completedSequence_ = sequence;
        haveCompletedSequence_ = true;
        ++eofEventCount_;
        frameCount_ = (sequence + 1) / height_;
        if (refillTask_) vTaskNotifyGiveFromISR(refillTask_, &higherPriorityTaskWoken);
    } else {
        ++spuriousInterruptCount_;
        if (status == 0) ++emptyInterruptCount_;
        else unexpectedInterruptMask_ |= status;
    }
    if (higherPriorityTaskWoken) portYIELD_FROM_ISR();
}

void QL32_REFILL_HOT PsramDmaScanout::refillWorker(void* argument) {
    auto* self = static_cast<PsramDmaScanout*>(argument);
    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (self->stopRequested_.load(std::memory_order_acquire)) break;
        self->refillCompletedRows();
    }
    xSemaphoreGive(self->stoppedWorkers_);
    vTaskSuspend(nullptr);
}

#if defined(QL32_OLIMEX_SINGLE_FRAMEBUFFER)
// Diagnostic single-framebuffer producer, run from the PSRAM stress task so it
// shares its stop handshake. Patterns: 1 checkerboard, 2 gradient,
// 3 random pixels, 4 continuous full-screen fill, 5 vertical scroll.
void PsramDmaScanout::stressWritePattern() {
#if defined(QL32_SF_STRESS_PATTERN)
    auto* fb = beginFrameUpdate();
    if (!fb) return;
    static std::uint32_t phase = 0;
    static std::uint32_t rng = 0x12345678u;
    ++phase;
#if QL32_SF_STRESS_PATTERN == 1
    for (std::uint32_t y = 0; y < fb->height; ++y)
        std::memset(fb->pixels + y * fb->stride, ((y + phase) & 1u) ? 0x40 : 0x04,
                    fb->stride);
#elif QL32_SF_STRESS_PATTERN == 2
    {
        std::uint8_t row[200];
        for (std::uint32_t i = 0; i < fb->stride; ++i) {
            const auto x = (i * 2u + phase) % fb->width;
            row[i] = static_cast<std::uint8_t>(((x * 16u / fb->width) << 4) |
                                               (((x + 1u) * 16u / fb->width) & 0x0Fu));
        }
        for (std::uint32_t y = 0; y < fb->height; ++y)
            std::memcpy(fb->pixels + y * fb->stride, row, fb->stride);
    }
#elif QL32_SF_STRESS_PATTERN == 3
    for (int i = 0; i < 4000; ++i) {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        (void)writePixel(*fb, rng % fb->width, (rng >> 12) % fb->height,
                         static_cast<std::uint8_t>(rng >> 24));
    }
#elif QL32_SF_STRESS_PATTERN == 4
    clear(*fb, static_cast<std::uint8_t>(phase & 15u));
#elif QL32_SF_STRESS_PATTERN == 5
    if (phase == 1)
        for (std::uint32_t y = 0; y < fb->height; ++y)
            fillRect(*fb, 0, y, fb->width, 1, static_cast<std::uint8_t>((y / 8u) & 15u));
    scrollVertical(*fb, 1, static_cast<std::uint8_t>((phase / 8u) & 15u));
#endif
    ++writerPassCount_;
    (void)publishFrameUpdate(fb);
#endif
}
#endif

void PsramDmaScanout::psramStressWorker(void* argument) {
    auto* self = static_cast<PsramDmaScanout*>(argument);
    auto* bytes = static_cast<volatile std::uint8_t*>(self->psramStress_);
    std::uint32_t round = 0;
    for (;;) {
        if (self->stopRequested_.load(std::memory_order_acquire)) break;
        std::uint32_t checksum = 0;
        for (std::size_t i = 0; i < kPsramStressBytes; ++i) {
            const auto value = static_cast<std::uint8_t>(i + round * 13u);
            bytes[i] = value;
            checksum += bytes[i];
        }
        self->stressChecksum_ = checksum;
        self->stressBytesWritten_ += kPsramStressBytes;
#if defined(QL32_OLIMEX_SINGLE_FRAMEBUFFER)
        self->stressWritePattern();
#endif
        ++round;
        vTaskDelay(1);
    }
    xSemaphoreGive(self->stoppedWorkers_);
    vTaskSuspend(nullptr);
}

void QL32_REFILL_HOT PsramDmaScanout::refillCompletedRows() {
    if (!kEnableLiveRefill) return;
    if (!haveCompletedSequence_) return;
#if defined(QL32_OLIMEX_DMA_FRAMEBUFFER_BENCHMARK) || \
    defined(QL32_OLIMEX_DMA_REFILL_PROFILE)
    const auto started = micros();
#endif
    const auto completed = completedSequence_;
    const auto window = makeRefillWindow(nextRefillSequence_, completed,
        static_cast<std::uint32_t>(ringRows_),
        static_cast<std::uint32_t>(refillRows_));
#if defined(QL32_OLIMEX_DMA_FRAMEBUFFER_BENCHMARK) || \
    defined(QL32_OLIMEX_DMA_REFILL_PROFILE)
    const auto backlogRows = nextRefillSequence_ <= completed
        ? completed - nextRefillSequence_ + 1 : 0;
    if (backlogRows > refillBacklogMaxRows_)
        refillBacklogMaxRows_ = backlogRows;
    refillObsoleteRowsSkipped_ += window.skippedRows;
    nextRefillSequence_ = window.firstSequence;
    std::uint32_t batchRows = 0;
#else
    nextRefillSequence_ = window.firstSequence;
#endif
    while (nextRefillSequence_ <= completed) {
        if (stopRequested_.load(std::memory_order_acquire)) break;
        refillRow(nextRefillSequence_);
        ++nextRefillSequence_;
        ++refillCount_;
#if defined(QL32_OLIMEX_DMA_FRAMEBUFFER_BENCHMARK) || \
    defined(QL32_OLIMEX_DMA_REFILL_PROFILE)
        ++batchRows;
#endif
    }
#if defined(QL32_OLIMEX_DMA_FRAMEBUFFER_BENCHMARK) || \
    defined(QL32_OLIMEX_DMA_REFILL_PROFILE)
    const auto elapsed = static_cast<std::uint32_t>(micros() - started);
    ++refillBatchCount_;
    refillBatchTotalMicros_ = refillBatchTotalMicros_ + elapsed;
    if (batchRows > refillBatchMaxRows_) refillBatchMaxRows_ = batchRows;
    if (elapsed > refillBatchMaxMicros_) refillBatchMaxMicros_ = elapsed;
#endif
}

void QL32_REFILL_HOT PsramDmaScanout::refillRow(std::uint32_t consumedSequence) {
    const auto targetSequence = consumedSequence + ringRows_;
    const auto targetRow = targetSequence % height_;
#if !defined(QL32_OLIMEX_SINGLE_FRAMEBUFFER)
    if (targetRow == 0) {
        portENTER_CRITICAL(&frameMux_);
        if (pendingFrameIndex_ >= 0) {
            const auto newActive = pendingFrameIndex_;
            const auto changed = pendingFrameDamage_;
            for (std::int8_t index = 0;
                 index < static_cast<std::int8_t>(kFramebufferCount); ++index) {
                if (index != newActive)
                    staleFrameDamage_[index] = unite(staleFrameDamage_[index], changed);
            }
            staleFrameDamage_[newActive] = {};
            activeFrameIndex_ = pendingFrameIndex_;
            pendingFrameIndex_ = -1;
            pendingFrameDamage_ = {};
            framebuffer_ = framebufferSlots_[activeFrameIndex_];
            psramFrame_ = framebuffer_.pixels;
        }
        portEXIT_CRITICAL(&frameMux_);
    }
#endif
    if (targetRow == 0 && palette_.applyAtFrameBoundary()) {
        rebuildPixelOutputLut();
        ++paletteApplyCount_;
    }
// Single-framebuffer mode scans the live image; the diagnostic horizontal
// shift (a 320-wide-only test pattern on the slow per-pixel path) never applies.
#if defined(QL32_OLIMEX_SINGLE_FRAMEBUFFER) || \
    defined(QL32_OLIMEX_DMA_TEST_PIXEL_EDGES) || \
    defined(QL32_OLIMEX_DMA_TEST_CHECKER) || \
    defined(QL32_OLIMEX_DMA_TEST_HORIZONTAL_BANDS) || \
    defined(QL32_OLIMEX_DMA_TEST_TRIANGLE)
    const auto horizontalShift = 0u;
#elif defined(QL32_OLIMEX_DMA_TEST_FORCE_SOURCE_SHIFT)
    const auto animationFrame = diagnosticMode_
        ? (targetSequence / height_) / 3u : 0u;
    const auto horizontalShift = animationFrame % width_;
#else
    const auto animationFrame = diagnosticMode_
        ? (targetSequence / height_) / 3u : 0u;
    // Keep high-resolution qualification focused on refill cadence. The
    // moving pattern is useful at 320x200, but forces the slower per-pixel
    // path and obscures whether the steady-state row encoder can keep up.
    const auto horizontalShift = width_ == 320 ? animationFrame % width_ : 0u;
#endif
    const auto slot = consumedSequence % ringRows_;
    auto* out = lineRing_ + slot * width_;
#if defined(QL32_OLIMEX_DMA_FRAMEBUFFER_BENCHMARK) || \
    defined(QL32_OLIMEX_DMA_REFILL_PROFILE)
    const auto rowStarted = micros();
    const auto completedSnapshot = completedSequence_;
    if (lineReadySequence_[slot] > completedSnapshot)
        ++refillActiveSlotOverwriteCount_;
    const auto markRowReady = [&]() {
        __sync_synchronize();
        lineReadySequence_[slot] = targetSequence;
        const auto elapsed = static_cast<std::uint32_t>(micros() - rowStarted);
        if (elapsed > refillRowMaxMicros_) refillRowMaxMicros_ = elapsed;
    };
#else
    const auto markRowReady = [&]() {
        __sync_synchronize();
        lineReadySequence_[slot] = targetSequence;
    };
#endif
    const auto* source = framebuffer_.pixels +
        static_cast<std::size_t>(targetRow) * framebuffer_.stride;
#if defined(QL32_OLIMEX_DMA_TEST_PACKED4_SHIFT_ENCODER)
    if (kFramebufferFormat == PixelFormat::Indexed4) {
        encodePacked4ShiftedRow(source, out, width_, horizontalShift,
                                packed4PairOutputLut_);
        markRowReady();
        return;
    }
#endif
    if (kFramebufferFormat == PixelFormat::Indexed4 && horizontalShift == 0) {
#if defined(QL32_OLIMEX_DMA_REFILL_BULK_COPY_TEST)
        // Pull the whole source row out of PSRAM with one bulk copy so the
        // encoder reads internal RAM only. Single refill task, so static.
        alignas(4) static std::uint8_t rowCache[1024];
        const auto rowBytes = (static_cast<std::size_t>(width_) + 1) / 2;
        if (rowBytes <= sizeof(rowCache)) {
            std::memcpy(rowCache, source, rowBytes);
            source = rowCache;
        }
#endif
        encodePacked4Row(source, out);
        markRowReady();
        return;
    }
    auto sourceX = horizontalShift;
    for (std::uint32_t x = 0; x < width_; ++x) {
        const auto stored = kFramebufferFormat == PixelFormat::Rgb222
            ? source[sourceX] & 0x3Fu
            : ((source[sourceX / 2] >> ((sourceX & 1u) ? 0 : 4)) & 0x0Fu);
        out[x] = pixelOutputLut_[stored];
        if (++sourceX == width_) sourceX = 0;
    }
    applyDmaPixelOrder(out, width_);
    markRowReady();
}

void PsramDmaScanout::rebuildPixelOutputLut() {
    const auto& palette = palette_.active();
    for (std::uint32_t value = 0; value < 256; ++value) {
        const auto color = kFramebufferFormat == PixelFormat::Rgb222
            ? static_cast<std::uint8_t>(value & 0x3Fu)
            : palette[value & 0x0Fu];
        pixelOutputLut_[value] = syncByte(timings_, false, false, outputRgb(color));
        if (kFramebufferFormat == PixelFormat::Indexed4) {
            const auto high = pixelOutputLut_[value >> 4];
            const auto low = pixelOutputLut_[value & 0x0Fu];
            packed4PairOutputLut_[value] = static_cast<std::uint16_t>(
                high | (static_cast<std::uint16_t>(low) << 8));
        }
    }
}

void QL32_REFILL_HOT PsramDmaScanout::encodePacked4Row(const std::uint8_t* source,
                                      std::uint8_t* output) const {
    // Each packed source byte already contains a pair of palette indices.
    // Precompute the pair conversion so a byte of PSRAM becomes one aligned
    // 16-bit output write. Emit each eight-pixel group in the VGA's physical
    // order directly, avoiding a second pass over the DMA line.
#if defined(QL32_OLIMEX_LEGACY_ENCODER)
    // A/B only: the pre-e7f8780 byte-store encoder.
    std::uint32_t x = 0;
    for (; x + 8 <= width_; x += 8) {
        std::uint32_t packed;
        const auto* aligned = static_cast<const std::uint8_t*>(
            __builtin_assume_aligned(source + x / 2, 4));
        std::memcpy(&packed, aligned, sizeof(packed));
        const auto pair1 = packed4PairOutputLut_[(packed >> 8) & 0xffu];
        const auto pair0 = packed4PairOutputLut_[packed & 0xffu];
        const auto pair3 = packed4PairOutputLut_[packed >> 24];
        const auto pair2 = packed4PairOutputLut_[(packed >> 16) & 0xffu];
        std::memcpy(output + x, &pair1, sizeof(pair1));
        std::memcpy(output + x + 2, &pair0, sizeof(pair0));
        std::memcpy(output + x + 4, &pair3, sizeof(pair3));
        std::memcpy(output + x + 6, &pair2, sizeof(pair2));
    }
#else
    std::uint32_t x = static_cast<std::uint32_t>(
        encodePacked4AlignedRow(source, output, width_, packed4PairOutputLut_));
#endif
    for (; x < width_; x += 2) {
        const auto packed = source[x / 2];
        output[x] = pixelOutputLut_[packed >> 4];
        if (x + 1 < width_) output[x + 1] = pixelOutputLut_[packed & 0x0Fu];
    }
}

void PsramDmaScanout::encodeRow(std::uint32_t sourceRow,
                               std::uint8_t* output) const {
    const auto* source = framebuffer_.pixels +
        static_cast<std::size_t>(sourceRow) * framebuffer_.stride;
    if (kFramebufferFormat == PixelFormat::Indexed4) {
        encodePacked4Row(source, output);
        return;
    }
    if (kFramebufferFormat == PixelFormat::Rgb222) {
        for (std::uint32_t x = 0; x < width_; ++x)
            output[x] = pixelOutputLut_[source[x] & 0x3Fu];
    }
    applyDmaPixelOrder(output, width_);
}

void PsramDmaScanout::reportStatus() const {
    Serial.printf("PSRAM VGA IRQ detail: empty=%u, unexpected-mask=0x%08x\n",
                  static_cast<unsigned>(emptyInterruptCount_),
                  static_cast<unsigned>(unexpectedInterruptMask_));
#if defined(QL32_OLIMEX_DMA_FRAMEBUFFER_BENCHMARK) || \
    defined(QL32_OLIMEX_DMA_REFILL_PROFILE)
    Serial.printf("PSRAM VGA scanout stats: frames=%u, rows=%u, EOF-events=%u, refills=%u, late-refills=%u, spurious-IRQ=%u, palette-applies=%u, PSRAM-stress-bytes=%u, checksum=%u, frame-clones=%u, clone-bytes=%u, pending-reuses=%u, refill-batches=%u, refill-max-us=%u, refill-row-max-us=%u, active-slot-overwrites=%u, refill-max-rows=%u, backlog-max-rows=%u, obsolete-skipped=%u, refill-total-us=%u\n",
                  static_cast<unsigned>(frameCount_),
                  static_cast<unsigned>(haveCompletedSequence_ ? completedSequence_ + 1 : 0),
                  static_cast<unsigned>(eofEventCount_),
                  static_cast<unsigned>(refillCount_),
                  static_cast<unsigned>(lateRefillCount_),
                  static_cast<unsigned>(spuriousInterruptCount_),
                  static_cast<unsigned>(paletteApplyCount_),
                  static_cast<unsigned>(stressBytesWritten_),
                  static_cast<unsigned>(stressChecksum_),
                  static_cast<unsigned>(frameCloneCount_),
                  static_cast<unsigned>(frameCloneBytes_),
                  static_cast<unsigned>(pendingFrameReuseCount_),
                  static_cast<unsigned>(refillBatchCount_),
                  static_cast<unsigned>(refillBatchMaxMicros_),
                  static_cast<unsigned>(refillRowMaxMicros_),
                  static_cast<unsigned>(refillActiveSlotOverwriteCount_),
                  static_cast<unsigned>(refillBatchMaxRows_),
                  static_cast<unsigned>(refillBacklogMaxRows_),
                  static_cast<unsigned>(refillObsoleteRowsSkipped_),
                  static_cast<unsigned>(refillBatchTotalMicros_));
#else
    Serial.printf("PSRAM VGA scanout stats: frames=%u, rows=%u, EOF-events=%u, refills=%u, late-refills=%u, spurious-IRQ=%u, palette-applies=%u, PSRAM-stress-bytes=%u, checksum=%u, frame-clones=%u, clone-bytes=%u, pending-reuses=%u\n",
                  static_cast<unsigned>(frameCount_),
                  static_cast<unsigned>(haveCompletedSequence_ ? completedSequence_ + 1 : 0),
                  static_cast<unsigned>(eofEventCount_),
                  static_cast<unsigned>(refillCount_),
                  static_cast<unsigned>(lateRefillCount_),
                  static_cast<unsigned>(spuriousInterruptCount_),
                  static_cast<unsigned>(paletteApplyCount_),
                  static_cast<unsigned>(stressBytesWritten_),
                  static_cast<unsigned>(stressChecksum_),
                  static_cast<unsigned>(frameCloneCount_),
                  static_cast<unsigned>(frameCloneBytes_),
                  static_cast<unsigned>(pendingFrameReuseCount_));
#endif
#if defined(QL32_OLIMEX_SINGLE_FRAMEBUFFER)
    Serial.printf("PSRAM VGA single-fb: writer-passes=%u, active-writers=%u, free-internal=%u, free-psram=%u\n",
                  static_cast<unsigned>(writerPassCount_),
                  static_cast<unsigned>(activeWriters_.load()),
                  static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                  static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
#endif
}

} // namespace ql32::platform::olimex_vga

#if defined(QL32_OLIMEX_DMA_REFILL_PROFILE)
namespace ql32::platform::olimex_vga {

void PsramDmaScanout::runEncoderBenchmark() const {
    if (kFramebufferFormat != PixelFormat::Indexed4 || !framebuffer_.pixels) {
        Serial.println("ENCBENCH: packed-4 framebuffer required");
        return;
    }
    constexpr std::uint32_t kBatch = 8;
    constexpr std::uint32_t kBatches = 128;
    constexpr std::uint32_t kRows = kBatch * kBatches;
    auto* scratch = dmaAlloc(width_);
    alignas(4) static std::uint8_t cache[1024];
    const auto rowBytes = (static_cast<std::size_t>(width_) + 1) / 2;
    if (!scratch || rowBytes > sizeof(cache)) {
        Serial.println("ENCBENCH: allocation failed");
        return;
    }
    portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    const auto rowAt = [this](std::uint32_t index) {
        // Stride through the whole frame so the PSRAM cache cannot hold it.
        return framebuffer_.pixels + static_cast<std::size_t>((index * 7u) % height_) *
            framebuffer_.stride;
    };
    enum Piece { kMicros, kCopy, kEncodeInternal, kEncodePsram, kCopyEncode, kCopySequential, kPieceCount };
    std::uint64_t cycles[kPieceCount]{};
    std::uint32_t sink = 0;
    for (int piece = 0; piece < kPieceCount; ++piece) {
        for (std::uint32_t batch = 0; batch < kBatches; ++batch) {
            const auto base = batch * kBatch;
            portENTER_CRITICAL(&mux);
            const auto start = ESP.getCycleCount();
            for (std::uint32_t i = 0; i < kBatch; ++i) {
                const auto* source = rowAt(base + i);
                switch (piece) {
                case kMicros: sink += static_cast<std::uint32_t>(micros()); break;
                case kCopy: std::memcpy(cache, source, rowBytes); break;
                case kEncodeInternal: encodePacked4Row(cache, scratch); break;
                case kEncodePsram: encodePacked4Row(source, scratch); break;
                case kCopyEncode:
                    std::memcpy(cache, source, rowBytes);
                    encodePacked4Row(cache, scratch);
                    break;
                case kCopySequential:
                    std::memcpy(cache, framebuffer_.pixels +
                        static_cast<std::size_t>((base + i) % height_) *
                        framebuffer_.stride, rowBytes);
                    break;
                }
            }
            cycles[piece] += static_cast<std::uint32_t>(ESP.getCycleCount() - start);
            portEXIT_CRITICAL(&mux);
            if (piece == kEncodeInternal) std::memcpy(cache, rowAt(base), rowBytes);
        }
    }
    heap_caps_free(scratch);
    const auto mhz = static_cast<double>(ESP.getCpuFreqMHz());
    const char* names[kPieceCount] = {"micros()", "psram->dram copy", "encode from dram",
                                      "encode from psram", "copy+encode", "copy sequential"};
    Serial.printf("ENCBENCH rows=%u cpu-mhz=%.0f width=%u sink=%u\n", kRows, mhz,
                  static_cast<unsigned>(width_), sink);
    for (int piece = 0; piece < kPieceCount; ++piece) {
        const double perRow = static_cast<double>(cycles[piece]) / kRows;
        Serial.printf("ENCBENCH %-18s cycles/row=%.0f us/row=%.2f\n", names[piece],
                      perRow, perRow / mhz);
    }
}

} // namespace ql32::platform::olimex_vga
#endif
