#include "ql32/platform/esp32/diagnostics.hpp"
#include "ql32/platform/olimex_vga/music.hpp"

#include "ql32/music/dac_format.hpp"
#include "ql32/music/mixer.hpp"
#include "ql32/music/tone.hpp"
#include "ql32/music/wav.hpp"
#include "ql32/music/vtx_source.hpp"
#include "ql32/music/song_loader.hpp"
#include "ql32/music/vtx.hpp"
#include "ql32/music/vtx_stream.hpp"
#include "ql32/music/vtx_player.hpp"
#include "ql32/music/ym_player.hpp"

#include <Arduino.h>
#include <driver/i2s.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <new>
#include <vector>

namespace ql32::platform::olimex_vga {
namespace {
constexpr i2s_port_t OutputPort = I2S_NUM_0;
constexpr std::size_t SamplesPerBlock = 256;
constexpr std::size_t MaximumFileBytes = music::maximumSongFileBytes();
constexpr std::uint32_t TestToneHz = 1000;
constexpr std::uint32_t UnderrunGapUs = 40000; // DMA ring holds 4 x 11.6 ms
constexpr std::uint32_t DiagnosticIntervalBlocks = 86; // about one second
constexpr std::uint32_t DiagnosticLastBlock = DiagnosticIntervalBlocks * 10;

void failIfEspError(esp_err_t error, const char* operation) {
    if (error != ESP_OK)
        throw runtime::ServiceError(runtime::ServiceErrorCode::Unavailable,
            std::string("music: ") + operation + " failed (ESP error " +
            std::to_string(static_cast<int>(error)) + ")");
}
} // namespace

struct MusicService::Impl {
    explicit Impl(std::shared_ptr<runtime::StorageService> storageService, int dacPin,
                  std::vector<std::string> cacheRoots)
        : storage(std::move(storageService)), dacGpio(dacPin),
          vtxCacheRoots(std::move(cacheRoots)),
          mutex(xSemaphoreCreateMutex()) {
        if (!mutex) throw std::bad_alloc();
    }

    ~Impl() {
        platform::esp32::diagnostics().musicTask.store(nullptr);
        platform::esp32::diagnostics().musicIoTask.store(nullptr);
        if (ioWorker) {
            vTaskDelete(ioWorker);
            ioWorker = nullptr;
        }
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
    std::shared_ptr<runtime::MemoryService> memory;
    std::vector<std::string> vtxCacheRoots;
    int dacGpio;
    SemaphoreHandle_t mutex = nullptr;
    static constexpr std::uint32_t IoStackBytes = 4096;
    TaskHandle_t worker = nullptr;
    TaskHandle_t ioWorker = nullptr;
    std::shared_ptr<music::AudioPrefetcher> prefetch; // guarded by `mutex`
    music::Mixer mixer; // Music slot: VTX or WAV stream; Effect slot: BEEP and MUSIC TEST
    std::uint8_t outputVolume = music::DefaultVolume;
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
            // The built-in DAC takes the upper byte of each slot directly; the
            // I2S one-bit MSB delay of STAND_I2S would shift the data.
            config.communication_format = I2S_COMM_FORMAT_STAND_MSB;
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
            // I2S right slot drives DAC1 (GPIO25); left slot drives DAC2 (GPIO26).
            failIfEspError(i2s_set_dac_mode(dacGpio == 26 ? I2S_DAC_CHANNEL_LEFT_EN
                                                          : I2S_DAC_CHANNEL_RIGHT_EN),
                           "DAC setup");
        }
        if (!worker) {
            if (xTaskCreatePinnedToCore(&Impl::taskEntry, "ql32_music", 6144, this,
                    3, &worker, tskNO_AFFINITY) != pdPASS) {
                worker = nullptr;
                throw runtime::ServiceError(runtime::ServiceErrorCode::Unavailable,
                                            "music: unable to start audio renderer task");
            }
            platform::esp32::diagnostics().musicTask.store(worker);
        }
        // Slow storage reads (VTX cache read-ahead) must not run in the audio task.
        // The I/O task stack lives in .bss: a fragmented heap often has no
        // contiguous 4 KiB left once a large BASIC app is loaded.
        if (!ioWorker) {
            static StaticTask_t ioTcb;
            static StackType_t ioStack[IoStackBytes];
            ioWorker = xTaskCreateStaticPinnedToCore(
                &Impl::ioTaskEntry, "ql32_music_io", IoStackBytes, this, 1,
                ioStack, &ioTcb, tskNO_AFFINITY);
            if (ioWorker) platform::esp32::diagnostics().musicIoTask.store(ioWorker);
        }
    }

    static void ioTaskEntry(void* context) {
        static_cast<Impl*>(context)->runIo();
    }

    void runIo() {
        for (;;) {
            lock();
            if (!mixer.active(music::Mixer::Music)) prefetch.reset();
            const auto current = prefetch;
            unlock();
            if (current) current->run();
            vTaskDelay(pdMS_TO_TICKS(20));
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
        std::int64_t lastWriteDoneUs = 0;
        std::uint32_t maxGapUs = 0;
        std::uint32_t lateBlocks = 0;
        std::uint32_t reportedLate = 0;
        std::uint32_t reportedErrors = 0;
        int blockMin = std::numeric_limits<int>::max();
        int blockMax = std::numeric_limits<int>::min();
        for (;;) {            lock();
            const bool renderAudio = playbackState == runtime::MusicPlaybackState::Playing &&
                mixer.anyActive();
            if (renderAudio) {
                mixer.render(pcm.data(), pcm.size());
                if (!mixer.anyActive()) {
                    owner = 0;
                    playbackState = runtime::MusicPlaybackState::Stopped;
                }
                const std::uint8_t level = outputVolume;
                for (auto& sample : pcm) sample = music::applyVolume(sample, level);
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
                lastWriteDoneUs = 0;
                maxGapUs = 0;
                lateBlocks = 0;
                blockMin = std::numeric_limits<int>::max();
                blockMax = std::numeric_limits<int>::min();
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
                const std::uint16_t value = music::pcmToDac16(pcm[i]);
                // Duplicate mono PCM into both I2S slots and enable only the
                // DAC channel wired to the configured pin.
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
            const std::int64_t now = esp_timer_get_time();
            if (lastWriteDoneUs != 0) {
                const std::uint32_t gap = static_cast<std::uint32_t>(now - lastWriteDoneUs);
                maxGapUs = std::max(maxGapUs, gap);
                if (gap > UnderrunGapUs) {
                    ++lateBlocks;
                    platform::esp32::diagnostics().musicLateBlocks.fetch_add(1);
                }
                auto& peak = platform::esp32::diagnostics().musicMaxGapUs;
                if (gap > peak.load()) peak.store(gap);
            }
            lastWriteDoneUs = now;
            platform::esp32::diagnostics().musicBlocks.fetch_add(1);
            const auto limits = std::minmax_element(pcm.begin(), pcm.end());
            blockMin = std::min<int>(blockMin, *limits.first);
            blockMax = std::max<int>(blockMax, *limits.second);
            if (writeResult == ESP_OK && written == sizeof(dac)) {
                if (blockNumber <= 3) {
                    Serial.printf("music: I2S0 accepted block %u (%u bytes)\n",
                        static_cast<unsigned>(blockNumber),
                        static_cast<unsigned>(written));
                }
                writeErrorLogged = false;
            } else {
                platform::esp32::diagnostics().musicWriteErrors.fetch_add(1);
                if (!writeErrorLogged) {
                    Serial.printf("music: I2S0 write failed (error=%d, bytes=%u/%u)\n",
                        static_cast<int>(writeResult), static_cast<unsigned>(written),
                        static_cast<unsigned>(sizeof(dac)));
                    writeErrorLogged = true;
                }
            }
            // After the first-10-seconds diagnostics, report only when the cumulative
            // late or write-error counters have grown (at most once a second).
            if (blockNumber % DiagnosticIntervalBlocks == 0 && blockNumber > DiagnosticLastBlock) {
                auto& d = platform::esp32::diagnostics();
                const std::uint32_t late = d.musicLateBlocks.load();
                const std::uint32_t errors = d.musicWriteErrors.load();
                if (late != reportedLate || errors != reportedErrors) {
                    Serial.printf("music: total late=%u writeerrors=%u maxGap=%uus blocks=%u\n",
                        static_cast<unsigned>(late), static_cast<unsigned>(errors),
                        static_cast<unsigned>(d.musicMaxGapUs.load()),
                        static_cast<unsigned>(d.musicBlocks.load()));
                    reportedLate = late;
                    reportedErrors = errors;
                }
            }
            if (blockNumber % DiagnosticIntervalBlocks == 0 && blockNumber <= DiagnosticLastBlock) {
                Serial.printf("music: blocks=%u pcm=%d..%d dac=0x%04x..0x%04x maxGap=%uus late=%u stack=%u iostack=%u\n",
                    static_cast<unsigned>(blockNumber), blockMin, blockMax,
                    static_cast<unsigned>(music::pcmToDac16(static_cast<std::int16_t>(blockMin))),
                    static_cast<unsigned>(music::pcmToDac16(static_cast<std::int16_t>(blockMax))),
                    static_cast<unsigned>(maxGapUs), static_cast<unsigned>(lateBlocks),
                    static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)),
                    ioWorker ? static_cast<unsigned>(uxTaskGetStackHighWaterMark(ioWorker)) : 0u);
                blockMin = std::numeric_limits<int>::max();
                blockMax = std::numeric_limits<int>::min();
                maxGapUs = 0;
            }
        }
    }
};

MusicService::MusicService(std::shared_ptr<runtime::StorageService> storage, int dacGpio,
                           std::vector<std::string> vtxCacheRoots)
    : impl_(std::make_unique<Impl>(std::move(storage), dacGpio, std::move(vtxCacheRoots))) {}

MusicService::~MusicService() = default;

void MusicService::setMemoryService(std::shared_ptr<runtime::MemoryService> memory) {
    impl_->memory = std::move(memory);
}

void MusicService::prepareOutput(const char* what, bool loadsSong) {
    const bool running = impl_->driverInstalled && impl_->worker;
    if (impl_->memory) {
        const auto& policy = impl_->memory->admissionPolicy();
        const auto failure = running ?
            (loadsSong ? impl_->memory->admissionFailure(what, policy.musicPlayTotalBytes,
                                                    policy.musicPlayContiguousBytes) :
                         std::nullopt) :
            impl_->memory->admissionFailure(what, policy.musicStartTotalBytes,
                                            policy.musicStartContiguousBytes);
        if (failure)
            throw runtime::ServiceError(runtime::ServiceErrorCode::OutOfMemory,
                                        "music: " + *failure);
    }
    // Task stacks need contiguous heap, so create them before the song allocates.
    impl_->ensureOutput();
}

void MusicService::play(runtime::MusicSessionId session, const std::string& path) {
    if (!impl_->storage)
        throw runtime::ServiceError(runtime::ServiceErrorCode::Unavailable,
                                    "music: storage service unavailable");
    prepareOutput("MUSIC PLAY", true);
    auto source = impl_->storage->openRead(path);
    const std::uint64_t byteCount = source->size();
    std::uint8_t signature[2] = {};
    if (!impl_->vtxCacheRoots.empty() && byteCount >= 2 &&
        source->readAt(0, signature, 2) == 2 &&
        ((signature[0] == 'a' && signature[1] == 'y') ||
         (signature[0] == 'y' && signature[1] == 'm'))) {
        source.reset();
        try {
            startMusic(session, std::make_unique<music::VtxSource>(
                music::openVtxStreamed(*impl_->storage, path, impl_->vtxCacheRoots)));
        } catch (const runtime::ServiceError&) {
            throw;
        } catch (const std::bad_alloc&) {
            throw runtime::ServiceError(runtime::ServiceErrorCode::OutOfMemory,
                                        "music: not enough memory (heap fragmented)");
        } catch (const std::exception& error) {
            throw runtime::ServiceError(runtime::ServiceErrorCode::InvalidPath,
                                        std::string("music: ") + error.what());
        }
        return;
    }
    std::uint8_t lead[7] = {};
    const bool leadRead = byteCount >= sizeof lead && source->readAt(0, lead, sizeof lead) == sizeof lead;
    if (!impl_->vtxCacheRoots.empty() && leadRead &&
        byteCount <= music::YmSong::MaximumPackedBytes &&
        ((lead[0] == 'Y' && lead[1] == 'M') ||
         (byteCount >= 22 && (std::memcmp(lead + 2, "-lh5-", 5) == 0 ||
                              std::memcmp(lead + 2, "-lh0-", 5) == 0)))) {
        source.reset();
        try {
            startMusic(session, std::make_unique<music::YmSource>(
                music::openYmStreamed(*impl_->storage, path, impl_->vtxCacheRoots)));
        } catch (const runtime::ServiceError&) {
            throw;
        } catch (const std::bad_alloc&) {
            throw runtime::ServiceError(runtime::ServiceErrorCode::OutOfMemory,
                                        "music: not enough memory (heap fragmented)");
        } catch (const std::exception& error) {
            throw runtime::ServiceError(runtime::ServiceErrorCode::InvalidPath,
                                        std::string("music: ") + error.what());
        }
        return;
    }
    if (byteCount == 0 || byteCount > MaximumFileBytes)
        throw runtime::ServiceError(runtime::ServiceErrorCode::InvalidPath,
                                    "music: song file is empty or too large");
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
    startMusic(session, music::openSong(std::move(bytes)));
}

void MusicService::playWav(runtime::MusicSessionId session, const std::string& path) {
    if (!impl_->storage)
        throw runtime::ServiceError(runtime::ServiceErrorCode::Unavailable,
                                    "music: storage service unavailable");
    prepareOutput("MUSIC WAV", true);
    auto source = std::make_unique<music::WavSource>(
        impl_->storage->openRead(path), music::VtxPlayer::DefaultSampleRate);
    startMusic(session, std::move(source));
}

void MusicService::startMusic(runtime::MusicSessionId session,
                              std::unique_ptr<music::AudioSource> source) {
    impl_->ensureOutput();
    impl_->lock();
    if (impl_->owner != 0 && impl_->owner != session) {
        impl_->unlock();
        throw runtime::ServiceError(runtime::ServiceErrorCode::ResourceBusy,
                                    "music: audio output is owned by another session");
    }
    impl_->prefetch = source->prefetcher();
    impl_->mixer.set(music::Mixer::Music, std::move(source));
    impl_->owner = session;
    impl_->playbackState = runtime::MusicPlaybackState::Playing;
    if (impl_->worker) xTaskNotifyGive(impl_->worker);
    impl_->unlock();
}

void MusicService::testTone(runtime::MusicSessionId session) {
    prepareOutput("MUSIC TEST", false);
    impl_->lock();
    if (impl_->owner != 0 && impl_->owner != session) {
        impl_->unlock();
        throw runtime::ServiceError(runtime::ServiceErrorCode::ResourceBusy,
                                    "music: audio output is owned by another session");
    }
    impl_->mixer.set(music::Mixer::Effect, std::make_unique<music::ToneSource>(
        TestToneHz, 0, music::VtxPlayer::DefaultSampleRate, music::Waveform::Sine, 6000));
    impl_->owner = session;
    impl_->playbackState = runtime::MusicPlaybackState::Playing;
    if (impl_->worker) xTaskNotifyGive(impl_->worker);
    impl_->unlock();
    Serial.printf("music: 1 kHz test tone active on GPIO%d; use MUSIC STOP to end\n",
                  impl_->dacGpio);
}

void MusicService::beep(runtime::MusicSessionId session, std::uint32_t durationMs,
                        std::uint32_t pitchHz, runtime::ToneWaveform waveform) {
    prepareOutput("BEEP", false);
    impl_->lock();
    if (impl_->owner != 0 && impl_->owner != session) {
        impl_->unlock();
        throw runtime::ServiceError(runtime::ServiceErrorCode::ResourceBusy,
                                    "audio output is owned by another session");
    }
    constexpr std::uint32_t Rate = music::VtxPlayer::DefaultSampleRate;
    impl_->mixer.set(music::Mixer::Effect, std::make_unique<music::ToneSource>(
        pitchHz, static_cast<std::uint64_t>(durationMs) * Rate / 1000 + 1,
        Rate, static_cast<music::Waveform>(waveform)));
    impl_->owner = session;
    if (!impl_->mixer.active(music::Mixer::Music)) impl_->playbackState = runtime::MusicPlaybackState::Playing;
    if (impl_->worker) xTaskNotifyGive(impl_->worker);
    impl_->unlock();
}

void MusicService::setVolume(std::uint8_t volume) {
    impl_->lock();
    impl_->outputVolume = volume;
    impl_->unlock();
}

std::uint8_t MusicService::volume() const noexcept {
    impl_->lock();
    const std::uint8_t result = impl_->outputVolume;
    impl_->unlock();
    return result;
}

void MusicService::stop(runtime::MusicSessionId session) {
    impl_->lock();
    if (impl_->owner != 0 && impl_->owner != session) {
        impl_->unlock();
        throw runtime::ServiceError(runtime::ServiceErrorCode::ResourceBusy,
                                    "music: audio output is owned by another session");
    }
    impl_->mixer.clearAll();
    impl_->owner = 0;
    impl_->playbackState = runtime::MusicPlaybackState::Stopped;
    impl_->unlock();
}

void MusicService::pause(runtime::MusicSessionId session) {
    impl_->lock();
    if (impl_->owner != session || !impl_->mixer.anyActive()) {
        impl_->unlock();
        throw runtime::ServiceError(runtime::ServiceErrorCode::InvalidHandle,
                                    "music: this session does not own a playing stream");
    }
    impl_->playbackState = runtime::MusicPlaybackState::Paused;
    impl_->unlock();
}

void MusicService::resume(runtime::MusicSessionId session) {
    impl_->lock();
    if (impl_->owner != session || !impl_->mixer.anyActive()) {
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

std::uint64_t MusicService::positionMs() const noexcept {
    impl_->lock();
    const auto* song = impl_->mixer.source(music::Mixer::Music);
    const std::uint64_t result = song ? song->positionMs() : 0;
    impl_->unlock();
    return result;
}

std::uint64_t MusicService::lengthMs() const noexcept {
    impl_->lock();
    const auto* song = impl_->mixer.source(music::Mixer::Music);
    const std::uint64_t result = song ? song->lengthMs() : 0;
    impl_->unlock();
    return result;
}

std::string MusicService::title() const {
    impl_->lock();
    const auto* song = impl_->mixer.source(music::Mixer::Music);
    std::string result = song ? song->title() : std::string{};
    impl_->unlock();
    return result;
}

void MusicService::stopSession(runtime::MusicSessionId session) noexcept {
    impl_->lock();
    if (impl_->owner == session) {
        impl_->mixer.clearAll();
        impl_->owner = 0;
        impl_->playbackState = runtime::MusicPlaybackState::Stopped;
    }
    impl_->unlock();
}

} // namespace ql32::platform::olimex_vga
