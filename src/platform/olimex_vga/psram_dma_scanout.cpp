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
#include <soc/soc_memory_types.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstdio>
#include <cstring>
#include <limits>

namespace ql32::platform::olimex_vga {
namespace {

constexpr std::uint32_t kWidth = 320;
constexpr std::uint32_t kHeight = 200;
#if defined(QL32_OLIMEX_DMA_TEST_PACKED4)
constexpr PixelFormat kFramebufferFormat = PixelFormat::Indexed4;
#else
constexpr PixelFormat kFramebufferFormat = PixelFormat::Rgb222;
#endif
constexpr std::size_t kPsramStressBytes = 64 * 1024;
constexpr int kRefillTaskStackBytes = 4096;
constexpr int kStressTaskStackBytes = 3072;
#if defined(QL32_OLIMEX_DMA_DISPLAY_DIAGNOSTIC) || \
    defined(QL32_OLIMEX_DMA_FRAMEBUFFER_BENCHMARK) || \
    defined(QL32_OLIMEX_DMA_TEST_PIXEL_EDGES) || \
    defined(QL32_OLIMEX_DMA_TEST_TRIANGLE)
// Keep the low-transition scanout marker while isolating runtime scheduling
// from the optional competing PSRAM writer. The framebuffer benchmark itself
// supplies the PSRAM write load and must not be combined with this extra loop.
constexpr bool kEnablePsramStress = false;
#else
constexpr bool kEnablePsramStress = true;
#endif
constexpr bool kEnableLiveRefill = true;
constexpr bool kEnableDmaEofInterrupt = true;

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
    const auto* source = framebufferSlots_[activeFrameIndex_].pixels;
    portEXIT_CRITICAL(&frameMux_);

    std::memcpy(destination->pixels, source, destination->storageBytes());
    ++frameCloneCount_;
    frameCloneBytes_ += static_cast<std::uint32_t>(destination->storageBytes());
    return destination;
}

bool PsramDmaScanout::publishFrameUpdate(FramebufferView* frame) noexcept {
    if (!frame) return false;
    portENTER_CRITICAL(&frameMux_);
    if (stagingFrameIndex_ < 0 ||
        frame->pixels != framebufferSlots_[stagingFrameIndex_].pixels) {
        portEXIT_CRITICAL(&frameMux_);
        return false;
    }
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
        stagingFrameIndex_ = -1;
    }
    portEXIT_CRITICAL(&frameMux_);
}

bool PsramDmaScanout::begin(const char* modeline, int redPin, int greenPin,
                            int bluePin, int hsyncPin, int vsyncPin,
                            bool diagnosticMode) {
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
    diagnosticMode_ = diagnosticMode;
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
        if (!startRefillWorker() ||
            (diagnosticMode_ && kEnablePsramStress &&
             !startPsramStressWorker())) return false;

        fabgl::esp_intr_alloc_pinnedToCore(ETS_I2S1_INTR_SOURCE,
                ESP_INTR_FLAG_LEVEL1 | ESP_INTR_FLAG_IRAM, dmaInterrupt, this,
                &interruptHandle_, xPortGetCoreID());
        if (!interruptHandle_) {
            Serial.println("PSRAM VGA scanout: unable to allocate I2S1 EOF interrupt");
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
                  static_cast<unsigned>(kWidth), static_cast<unsigned>(kHeight),
                  static_cast<unsigned>(timings.scanCount),
                  static_cast<unsigned>(timings.frequency), refresh,
                  kFramebufferFormat == PixelFormat::Rgb222 ? "RGB222" : "indexed4",
                  static_cast<unsigned>(framebuffer_.stride),
                  static_cast<unsigned>(psramBytes_),
                  static_cast<unsigned>(internalDmaBytes_),
                  static_cast<unsigned>(descriptorCount_),
                  static_cast<unsigned>(kLineRingCount),
                  static_cast<unsigned>(kWidth),
                  static_cast<unsigned>(kRefillGroupSize));
    Serial.printf("PSRAM VGA workers: EOF=%s, live refill=%s, refill core=%d, PSRAM stress=%s (%u bytes), DMA heap remaining=%u bytes\n",
                  kEnableDmaEofInterrupt ? "enabled" : "disabled",
                  kEnableLiveRefill ? "enabled" : "disabled", 0,
                  diagnosticMode_ && kEnablePsramStress ? "enabled" : "disabled",
                  static_cast<unsigned>(diagnosticMode_ && kEnablePsramStress
                      ? kPsramStressBytes : 0),
                  static_cast<unsigned>(heap_caps_get_free_size(
                      MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
    return true;
}

bool PsramDmaScanout::allocateAndBuild(const fabgl::VGATimings& timings) {
    const auto stride = kFramebufferFormat == PixelFormat::Rgb222
        ? static_cast<std::size_t>(kWidth)
        : (static_cast<std::size_t>(kWidth) + 1) / 2;
    const std::size_t sourceBytes = stride * kHeight;
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

    for (std::size_t i = 0; i < kFramebufferCount; ++i) {
        framebufferSlots_[i].pixels = static_cast<std::uint8_t*>(heap_caps_malloc(
            sourceBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        framebufferSlots_[i].width = kWidth;
        framebufferSlots_[i].height = kHeight;
        framebufferSlots_[i].stride = stride;
        framebufferSlots_[i].format = kFramebufferFormat;
        if (framebufferSlots_[i].pixels)
            std::memset(framebufferSlots_[i].pixels, 0, sourceBytes);
    }
    activeFrameIndex_ = 0;
    pendingFrameIndex_ = -1;
    stagingFrameIndex_ = -1;
    framebuffer_ = framebufferSlots_[activeFrameIndex_];
    psramFrame_ = framebuffer_.pixels;
    lineRing_ = dmaAlloc(kLineRingCount * kWidth);
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
        return false;
    }
    if (diagnosticMode_ && kEnablePsramStress) {
        psramStress_ = static_cast<std::uint8_t*>(heap_caps_malloc(
            kPsramStressBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!psramStress_) {
            Serial.println("PSRAM VGA scanout: unable to allocate PSRAM stress buffer");
            return false;
        }
    }
    if (!esp_ptr_external_ram(psramFrame_) || !esp_ptr_internal(lineRing_) ||
        !esp_ptr_dma_capable(lineRing_) || !esp_ptr_dma_capable(descriptors_) ||
        !esp_ptr_dma_capable(horizontalBlank_) || !esp_ptr_dma_capable(verticalBlank_) ||
        !esp_ptr_dma_capable(verticalSync_)) {
        Serial.println("PSRAM VGA scanout: allocator returned memory with unexpected capabilities");
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
#else
        for (std::uint32_t y = 0; y < kHeight; ++y) {
            for (std::uint32_t x = 0; x < kWidth; ++x) {
#if defined(QL32_OLIMEX_DMA_TEST_PIXEL_EDGES)
                const auto index = static_cast<std::uint8_t>(
                    x < kWidth / 2 ? ((x & 1u) ? 4u : 0u) :
                    (y < kHeight / 2 ? ((y & 1u) ? 4u : 0u) :
                     (((x + y) & 1u) ? 4u : 0u)));
#else
                const auto bar = (x * 8u) / kWidth;
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
        fillRect(framebuffer_, 8, 150, kWidth - 16, 16, 0);
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
    for (std::uint32_t y = 0; y < kLineRingCount; ++y) {
        auto* out = lineRing_ + (y % kLineRingCount) * kWidth;
        encodeRow(y % kHeight, out);
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
    const auto physicalRowsPerRefill = kRefillGroupSize * timings.scanCount;
    for (std::size_t y = 0; y < physicalActiveLines; ++y) {
        auto* row = lineRing_ + ((y / timings.scanCount) % kLineRingCount) * kWidth;
        fillDescriptor(descriptors_[index], row, kWidth, &descriptors_[index + 1]);
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
    internalDmaBytes_ = kLineRingCount * kWidth + horizontalBytes +
        2 * rawLineBytes + count * sizeof(lldesc_t);
    return true;
}

bool PsramDmaScanout::startRefillWorker() {
    const auto result = xTaskCreatePinnedToCore(refillWorker, "VgaRefill",
        kRefillTaskStackBytes, this, kRefillTaskPriority, &refillTask_, 0);
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
    if (I2S1.int_st.out_eof) {
        const auto sequence = haveCompletedSequence_
            ? completedSequence_ + kRefillGroupSize
            : kRefillGroupSize - 1;
        const auto nextSequence = sequence + 1;
        if (lineReadySequence_[nextSequence % kLineRingCount] != nextSequence)
            ++lateRefillCount_;
        completedSequence_ = sequence;
        haveCompletedSequence_ = true;
        ++eofEventCount_;
        frameCount_ = (sequence + 1) / kHeight;
        if (refillTask_) vTaskNotifyGiveFromISR(refillTask_, &higherPriorityTaskWoken);
    } else {
        ++spuriousInterruptCount_;
    }
    I2S1.int_clr.val = I2S1.int_st.val;
    if (higherPriorityTaskWoken) portYIELD_FROM_ISR();
}

void PsramDmaScanout::refillWorker(void* argument) {
    auto* self = static_cast<PsramDmaScanout*>(argument);
    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        self->refillCompletedRows();
    }
}

void PsramDmaScanout::psramStressWorker(void* argument) {
    auto* self = static_cast<PsramDmaScanout*>(argument);
    auto* bytes = static_cast<volatile std::uint8_t*>(self->psramStress_);
    std::uint32_t round = 0;
    for (;;) {
        std::uint32_t checksum = 0;
        for (std::size_t i = 0; i < kPsramStressBytes; ++i) {
            const auto value = static_cast<std::uint8_t>(i + round * 13u);
            bytes[i] = value;
            checksum += bytes[i];
        }
        self->stressChecksum_ = checksum;
        self->stressBytesWritten_ += kPsramStressBytes;
        ++round;
        vTaskDelay(1);
    }
}

void PsramDmaScanout::refillCompletedRows() {
    if (!kEnableLiveRefill) return;
    if (!haveCompletedSequence_) return;
#if defined(QL32_OLIMEX_DMA_FRAMEBUFFER_BENCHMARK)
    const auto started = micros();
#endif
    const auto completed = completedSequence_;
    const auto window = makeRefillWindow(nextRefillSequence_, completed,
        static_cast<std::uint32_t>(kLineRingCount),
        static_cast<std::uint32_t>(kRefillGroupSize));
#if defined(QL32_OLIMEX_DMA_FRAMEBUFFER_BENCHMARK)
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
        refillRow(nextRefillSequence_);
        ++nextRefillSequence_;
        ++refillCount_;
#if defined(QL32_OLIMEX_DMA_FRAMEBUFFER_BENCHMARK)
        ++batchRows;
#endif
    }
#if defined(QL32_OLIMEX_DMA_FRAMEBUFFER_BENCHMARK)
    const auto elapsed = static_cast<std::uint32_t>(micros() - started);
    ++refillBatchCount_;
    if (batchRows > refillBatchMaxRows_) refillBatchMaxRows_ = batchRows;
    if (elapsed > refillBatchMaxMicros_) refillBatchMaxMicros_ = elapsed;
#endif
}

void PsramDmaScanout::refillRow(std::uint32_t consumedSequence) {
    const auto targetSequence = consumedSequence + kLineRingCount;
    const auto targetRow = targetSequence % kHeight;
    if (targetRow == 0) {
        portENTER_CRITICAL(&frameMux_);
        if (pendingFrameIndex_ >= 0) {
            activeFrameIndex_ = pendingFrameIndex_;
            pendingFrameIndex_ = -1;
            framebuffer_ = framebufferSlots_[activeFrameIndex_];
            psramFrame_ = framebuffer_.pixels;
        }
        portEXIT_CRITICAL(&frameMux_);
    }
    if (targetRow == 0 && palette_.applyAtFrameBoundary()) {
        rebuildPixelOutputLut();
        ++paletteApplyCount_;
    }
#if defined(QL32_OLIMEX_DMA_TEST_PIXEL_EDGES) || \
    defined(QL32_OLIMEX_DMA_TEST_TRIANGLE)
    const auto horizontalShift = 0u;
#else
    const auto animationFrame = diagnosticMode_
        ? (targetSequence / kHeight) / 3u : 0u;
    const auto horizontalShift = animationFrame % kWidth;
#endif
    const auto slot = consumedSequence % kLineRingCount;
    auto* out = lineRing_ + slot * kWidth;
    const auto* source = framebuffer_.pixels +
        static_cast<std::size_t>(targetRow) * framebuffer_.stride;
    if (kFramebufferFormat == PixelFormat::Indexed4 && horizontalShift == 0) {
        for (std::uint32_t x = 0; x < kWidth; x += 2) {
            const auto packed = source[x / 2];
            out[x] = pixelOutputLut_[packed >> 4];
            out[x + 1] = pixelOutputLut_[packed & 0x0Fu];
        }
        applyDmaPixelOrder(out, kWidth);
        __sync_synchronize();
        lineReadySequence_[slot] = targetSequence;
        return;
    }
    auto sourceX = horizontalShift;
    for (std::uint32_t x = 0; x < kWidth; ++x) {
        const auto stored = kFramebufferFormat == PixelFormat::Rgb222
            ? source[sourceX] & 0x3Fu
            : ((source[sourceX / 2] >> ((sourceX & 1u) ? 0 : 4)) & 0x0Fu);
        out[x] = pixelOutputLut_[stored];
        if (++sourceX == kWidth) sourceX = 0;
    }
    applyDmaPixelOrder(out, kWidth);
    __sync_synchronize();
    lineReadySequence_[slot] = targetSequence;
}

void PsramDmaScanout::rebuildPixelOutputLut() {
    const auto& palette = palette_.active();
    for (std::uint32_t value = 0; value < 256; ++value) {
        const auto color = kFramebufferFormat == PixelFormat::Rgb222
            ? static_cast<std::uint8_t>(value & 0x3Fu)
            : palette[value & 0x0Fu];
        pixelOutputLut_[value] = syncByte(timings_, false, false, outputRgb(color));
    }
}

void PsramDmaScanout::encodeRow(std::uint32_t sourceRow,
                               std::uint8_t* output) const {
    const auto* source = framebuffer_.pixels +
        static_cast<std::size_t>(sourceRow) * framebuffer_.stride;
    if (kFramebufferFormat == PixelFormat::Rgb222) {
        for (std::uint32_t x = 0; x < kWidth; ++x)
            output[x] = pixelOutputLut_[source[x] & 0x3Fu];
    } else {
        for (std::uint32_t x = 0; x < kWidth; x += 2) {
            const auto packed = source[x / 2];
            output[x] = pixelOutputLut_[packed >> 4];
            if (x + 1 < kWidth) output[x + 1] = pixelOutputLut_[packed & 0x0Fu];
        }
    }
    applyDmaPixelOrder(output, kWidth);
}

void PsramDmaScanout::reportStatus() const {
#if defined(QL32_OLIMEX_DMA_FRAMEBUFFER_BENCHMARK)
    Serial.printf("PSRAM VGA scanout stats: frames=%u, rows=%u, EOF-events=%u, refills=%u, late-refills=%u, spurious-IRQ=%u, palette-applies=%u, PSRAM-stress-bytes=%u, checksum=%u, frame-clones=%u, clone-bytes=%u, pending-reuses=%u, refill-batches=%u, refill-max-us=%u, refill-max-rows=%u, backlog-max-rows=%u, obsolete-skipped=%u\n",
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
                  static_cast<unsigned>(refillBatchMaxRows_),
                  static_cast<unsigned>(refillBacklogMaxRows_),
                  static_cast<unsigned>(refillObsoleteRowsSkipped_));
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
}

} // namespace ql32::platform::olimex_vga
