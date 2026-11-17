#include "ql32/platform/olimex_vga/music.hpp"

#include "ql32/music/vtx.hpp"
#include "ql32/music/vtx_player.hpp"

#include <Arduino.h>
#include <driver/i2s.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <algorithm>
#include <array>
#include <limits>
#include <new>
#include <vector>

namespace ql32::platform::olimex_vga {
namespace {
constexpr i2s_port_t OutputPort = I2S_NUM_0;
constexpr std::size_t SamplesPerBlock = 256;
constexpr std::size_t MaximumFileBytes = music::VtxSong::MaximumFileBytes;

void failIfEspError(esp_err_t error, const char* operation) {
    if (error != ESP_OK)
        throw runtime::ServiceError(runtime::ServiceErrorCode::Unavailable,
            std::string("music: ") + operation + " failed (ESP error " +
            std::to_string(static_cast<int>(error)) + ")");
}
} // namespace

struct MusicService::Impl {
    explicit Impl(std::shared_ptr<runtime::StorageService> storageService)
        : storage(std::move(storageService)), mutex(xSemaphoreCreateMutex()) {
        if (!mutex) throw std::bad_alloc();
    }

    ~Impl() {
        if (worker) {
            vTaskDelete(worker);
            worker = nullptr;
        }
        if (driverInstalled) {
            i2s_stop(OutputPort);
            i2s_driver_uninstall(OutputPort);
        }
        if (mutex) vSemaphoreDelete(mutex);
    }

    std::shared_ptr<runtime::StorageService> storage;
    SemaphoreHandle_t mutex = nullptr;
    TaskHandle_t worker = nullptr;
    std::unique_ptr<music::VtxPlayer> player;
    runtime::MusicSessionId owner = 0;
    runtime::MusicPlaybackState playbackState = runtime::MusicPlaybackState::Stopped;
    bool driverInstalled = false;
    bool outputRunning = false;

    void lock() const { xSemaphoreTake(mutex, portMAX_DELAY); }
    void unlock() const { xSemaphoreGive(mutex); }

    void ensureOutput() {
        if (!driverInstalled) {
            i2s_config_t config{};
            config.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX |
                                                   I2S_MODE_DAC_BUILT_IN);
            config.sample_rate = music::VtxPlayer::DefaultSampleRate;
            config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
            config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
            config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
            config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
            config.dma_buf_count = 4;
            config.dma_buf_len = SamplesPerBlock;
            config.use_apll = false;
            config.tx_desc_auto_clear = true;
            config.fixed_mclk = 0;
            failIfEspError(i2s_driver_install(OutputPort, &config, 0, nullptr),
                           "I2S0 install");
            driverInstalled = true;
            failIfEspError(i2s_set_pin(OutputPort, nullptr), "I2S0 pin setup");
            failIfEspError(i2s_set_dac_mode(I2S_DAC_CHANNEL_RIGHT_EN),
                           "GPIO25 DAC setup");
        }
        if (!worker) {
            if (xTaskCreatePinnedToCore(&Impl::taskEntry, "ql32_music", 4096, this,
                    3, &worker, tskNO_AFFINITY) != pdPASS) {
                worker = nullptr;
                throw runtime::ServiceError(runtime::ServiceErrorCode::Unavailable,
                                            "music: unable to start audio renderer task");
            }
        }
    }

    static void taskEntry(void* context) {
        static_cast<Impl*>(context)->run();
    }

    void run() {
        std::array<std::int16_t, SamplesPerBlock> pcm{};
        std::array<std::uint16_t, SamplesPerBlock * 2> dac{};
        bool firstBlockLogged = false;
        std::uint32_t submittedBlocks = 0;
        bool startErrorLogged = false;
        bool writeErrorLogged = false;
        for (;;) {
            lock();
            const bool renderAudio = player &&
                playbackState == runtime::MusicPlaybackState::Playing;
            if (renderAudio) {
                player->render(pcm.data(), pcm.size());
                if (!player->playing()) {
                    player.reset();
                    owner = 0;
                    playbackState = runtime::MusicPlaybackState::Stopped;
                }
            } else {
                pcm.fill(0);
            }
            unlock();

            if (!renderAudio) {
                if (outputRunning) {
                    i2s_zero_dma_buffer(OutputPort);
                    i2s_stop(OutputPort);
                    outputRunning = false;
                }
                firstBlockLogged = false;
                submittedBlocks = 0;
                startErrorLogged = false;
                writeErrorLogged = false;
                (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
                continue;
            }
            if (!outputRunning) {
                const esp_err_t startResult = i2s_start(OutputPort);
                if (startResult != ESP_OK) {
                    if (!startErrorLogged) {
                        Serial.printf("music: I2S0 start failed (%d)\n",
                                      static_cast<int>(startResult));
                        startErrorLogged = true;
                    }
                    vTaskDelay(pdMS_TO_TICKS(10));
                    continue;
                }
                outputRunning = true;
                startErrorLogged = false;
            }

            if (!firstBlockLogged) {
                const auto limits = std::minmax_element(pcm.begin(), pcm.end());
                Serial.printf("music: first PCM block range %d..%d; submitting to I2S0\n",
                    static_cast<int>(*limits.first), static_cast<int>(*limits.second));
                firstBlockLogged = true;
            }

            for (std::size_t i = 0; i < SamplesPerBlock; ++i) {
                const std::uint16_t value = static_cast<std::uint16_t>(
                    (static_cast<std::int32_t>(pcm[i]) + 32768) >> 8) << 8;
                // The right DAC channel is GPIO25; duplicate mono PCM into
                // both I2S slots and enable only that DAC channel.
                dac[i * 2] = value;
                dac[i * 2 + 1] = value;
            }
            std::size_t written = 0;
            const std::uint32_t blockNumber = ++submittedBlocks;
            if (blockNumber <= 3) {
                Serial.printf("music: submitting I2S0 block %u\n",
                              static_cast<unsigned>(blockNumber));
            }
            const esp_err_t writeResult = i2s_write(OutputPort, dac.data(),
                sizeof(dac), &written, portMAX_DELAY);
            if (writeResult == ESP_OK && written == sizeof(dac)) {
                if (blockNumber <= 3) {
                    Serial.printf("music: I2S0 accepted block %u (%u bytes)\n",
                        static_cast<unsigned>(blockNumber),
                        static_cast<unsigned>(written));
                }
                writeErrorLogged = false;
            } else if (!writeErrorLogged) {
                Serial.printf("music: I2S0 write failed (error=%d, bytes=%u/%u)\n",
                    static_cast<int>(writeResult), static_cast<unsigned>(written),
                    static_cast<unsigned>(sizeof(dac)));
                writeErrorLogged = true;
            }
        }
    }
};

MusicService::MusicService(std::shared_ptr<runtime::StorageService> storage)
    : impl_(std::make_unique<Impl>(std::move(storage))) {}

MusicService::~MusicService() = default;

void MusicService::play(runtime::MusicSessionId session, const std::string& path) {
    if (!impl_->storage)
        throw runtime::ServiceError(runtime::ServiceErrorCode::Unavailable,
                                    "music: storage service unavailable");
    auto source = impl_->storage->openRead(path);
    const std::uint64_t byteCount = source->size();
    if (byteCount == 0 || byteCount > MaximumFileBytes)
        throw runtime::ServiceError(runtime::ServiceErrorCode::InvalidPath,
                                    "music: VTX file is empty or exceeds 64 KiB");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(byteCount));
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const std::size_t received = source->readAt(offset, bytes.data() + offset,
                                                     bytes.size() - offset);
        if (received == 0)
            throw runtime::ServiceError(runtime::ServiceErrorCode::IoError,
                                        "music: short read while loading VTX file");
        offset += received;
    }
    auto newPlayer = std::make_unique<music::VtxPlayer>(music::VtxSong::parse(std::move(bytes)));

    impl_->ensureOutput();
    impl_->lock();
    if (impl_->owner != 0 && impl_->owner != session) {
        impl_->unlock();
        throw runtime::ServiceError(runtime::ServiceErrorCode::ResourceBusy,
                                    "music: audio output is owned by another session");
    }
    impl_->player = std::move(newPlayer);
    impl_->owner = session;
    impl_->playbackState = runtime::MusicPlaybackState::Playing;
    if (impl_->worker) xTaskNotifyGive(impl_->worker);
    impl_->unlock();
}

void MusicService::stop(runtime::MusicSessionId session) {
    impl_->lock();
    if (impl_->owner != 0 && impl_->owner != session) {
        impl_->unlock();
        throw runtime::ServiceError(runtime::ServiceErrorCode::ResourceBusy,
                                    "music: audio output is owned by another session");
    }
    impl_->player.reset();
    impl_->owner = 0;
    impl_->playbackState = runtime::MusicPlaybackState::Stopped;
    impl_->unlock();
}

void MusicService::pause(runtime::MusicSessionId session) {
    impl_->lock();
    if (impl_->owner != session || !impl_->player) {
        impl_->unlock();
        throw runtime::ServiceError(runtime::ServiceErrorCode::InvalidHandle,
                                    "music: this session does not own a playing stream");
    }
    impl_->playbackState = runtime::MusicPlaybackState::Paused;
    impl_->unlock();
}

void MusicService::resume(runtime::MusicSessionId session) {
    impl_->lock();
    if (impl_->owner != session || !impl_->player) {
        impl_->unlock();
        throw runtime::ServiceError(runtime::ServiceErrorCode::InvalidHandle,
                                    "music: this session does not own a paused stream");
    }
    impl_->playbackState = runtime::MusicPlaybackState::Playing;
    if (impl_->worker) xTaskNotifyGive(impl_->worker);
    impl_->unlock();
}

runtime::MusicPlaybackState MusicService::state() const noexcept {
    impl_->lock();
    const auto result = impl_->playbackState;
    impl_->unlock();
    return result;
}

void MusicService::stopSession(runtime::MusicSessionId session) noexcept {
    impl_->lock();
    if (impl_->owner == session) {
        impl_->player.reset();
        impl_->owner = 0;
        impl_->playbackState = runtime::MusicPlaybackState::Stopped;
    }
    impl_->unlock();
}

} // namespace ql32::platform::olimex_vga
