#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <driver/gpio.h>
#include <fabgl.h>
#include <algorithm>
#include <cstdint>
#include <memory>
#include <ostream>
#include <optional>
#include <streambuf>
#include <string_view>
#include <utility>

#include "capabilities.hpp"
#include "pins.hpp"
#include "ql32/vm/scheduler.hpp"
#include "ql32/platform/esp32/sd_channels.hpp"
#include "ql32/platform/esp32/idf_sd.hpp"
#include "ql32/platform/esp32/clock.hpp"
#include "ql32/platform/esp32/line_ui_presenter.hpp"
#include "ql32/platform/esp32/memory.hpp"
#include "ql32/platform/esp32/network.hpp"
#include "ql32/platform/esp32/sd_storage.hpp"
#include "ql32/graphics/windowed_service.hpp"
#include "ql32/platform/fabgl_vga/graphics.hpp"
#include "ql32/platform/fabgl_vga/frame_presenter.hpp"
#include "ql32/platform/fabgl_vga/keyboard.hpp"

#if defined(QL32_BOARD_OLIMEX_ESP32_SBC_FABGL)
namespace ql32_board = ql32::boards::olimex_esp32_sbc_fabgl;
constexpr char kBoardName[] = "Olimex ESP32-SBC-FabGL";
#elif defined(QL32_BOARD_LILYGO_VGA32)
namespace ql32_board = ql32::boards::lilygo_vga32;
constexpr char kBoardName[] = "LILYGO VGA32 V1.4";
#else
#error "Select a supported FabGL VGA board profile"
#endif

namespace {

class SerialOutputBuffer final : public std::streambuf {
protected:
    int_type overflow(int_type character) override {
        if (traits_type::eq_int_type(character, traits_type::eof()))
            return traits_type::not_eof(character);
        return Serial.write(static_cast<std::uint8_t>(character)) == 1
            ? character : traits_type::eof();
    }

    std::streamsize xsputn(const char* data, std::streamsize size) override {
        if (size <= 0) return 0;
        return static_cast<std::streamsize>(Serial.write(
            reinterpret_cast<const std::uint8_t*>(data), static_cast<std::size_t>(size)));
    }
};

fabgl::VGAController display;
fabgl::Canvas canvas(&display);
fabgl::PS2Controller ps2;
std::shared_ptr<ql32::graphics::WindowedService> graphics;
std::optional<ql32::platform::fabgl_vga::FabglKeyboard> keyboardInput;
ql32::platform::esp32::Clock boardClock;
ql32::platform::esp32::MemoryService boardMemory;
ql32::platform::esp32::WifiNetworkService boardNetwork;
SPIClass sdBus(HSPI);
ql32::platform::esp32::SdStorage sdStorage(SD);
ql32::platform::esp32::SdChannels sdChannels(SD);
#if defined(QL32_BOARD_OLIMEX_ESP32_SBC_FABGL)
ql32::platform::esp32::IdfSdCard idfSdCard;
ql32::platform::esp32::IdfSdStorage idfSdStorage(idfSdCard);
ql32::platform::esp32::IdfSdChannels idfSdChannels(idfSdCard);
#endif
SerialOutputBuffer serialBuffer;
std::ostream serialOutput(&serialBuffer);
std::unique_ptr<ql32::vm::SessionScheduler> scheduler;
ql32::vm::TaskId replTask = 0;
std::shared_ptr<ql32::ui::QueuedService> uiService =
    std::make_shared<ql32::ui::QueuedService>();
std::unique_ptr<ql32::platform::esp32::LineUiPresenter> uiPresenter;
std::string inputLine;
bool serialSawCarriageReturn = false;
bool replReadyMessageShown = false;
bool replPromptShown = false;

void appendInputCharacter(char character) {
    if (inputLine.size() >= 256) return;
    inputLine.push_back(character);
    Serial.write(static_cast<std::uint8_t>(character));
}

void eraseInputCharacter() {
    if (inputLine.empty()) return;
    inputLine.pop_back();
    Serial.print("\b \b");
}

void submitInputLine() {
    Serial.println();
    if (uiPresenter && uiPresenter->active()) {
        (void)uiPresenter->consumeLine(inputLine);
    } else if (scheduler && !inputLine.empty() &&
               !scheduler->submit(scheduler->focusedTask(), inputLine + "\n")) {
        Serial.println("BASIC session is busy");
    }
    inputLine.clear();
    replPromptShown = false;
}

void receiveSerialCharacter(char character) {
    if (character == '\n' && serialSawCarriageReturn) {
        serialSawCarriageReturn = false;
        return;
    }
    if (character == '\r' || character == '\n') {
        serialSawCarriageReturn = character == '\r';
        submitInputLine();
        return;
    }
    serialSawCarriageReturn = false;
    if (character == '\b' || character == 127) {
        eraseInputCharacter();
    } else if (character >= 32 && character <= 126) {
        appendInputCharacter(character);
    }
}

bool acceptsInput() {
    if (uiPresenter && uiPresenter->active()) return true;
    if (!scheduler) return false;
    const auto state = scheduler->task(scheduler->focusedTask());
    return state && state->status == ql32::vm::TaskStatus::WaitingInput;
}

void advanceScheduler() {
    if (!scheduler) return;
    (void)scheduler->tick();

    const auto tasks = scheduler->tasks();
    for (const auto& task : tasks) {
        const std::string error = scheduler->takeError(task.id);
        if (!error.empty())
            Serial.printf("BASIC session %llu VM error: %s\n",
                          static_cast<unsigned long long>(task.id), error.c_str());
    }

    const bool sessionWaitingForUi = std::any_of(tasks.begin(), tasks.end(),
        [](const ql32::vm::TaskInfo& task) {
            return task.status == ql32::vm::TaskStatus::WaitingUi;
        });
    if (uiPresenter && !uiPresenter->active() && !uiPresenter->beginNext() &&
        sessionWaitingForUi)
        Serial.println("suspended session has no queued UI request");

    const auto focused = scheduler->focusedTask();
    const auto state = scheduler->task(focused);
    if (!state) return;
    if (state->status == ql32::vm::TaskStatus::WaitingInput &&
        (!uiPresenter || !uiPresenter->active())) {
        if (!replReadyMessageShown) {
            Serial.println("BASIC REPL ready (USB serial and PS/2 keyboard)");
            replReadyMessageShown = true;
        }
        if (!replPromptShown) {
            if (focused == replTask) Serial.print("> ");
            else Serial.printf("task %llu> ", static_cast<unsigned long long>(focused));
            replPromptShown = true;
        }
    } else {
        replPromptShown = false;
    }
}

} // namespace

void setup() {
    Serial.begin(115200);

    if (!ql32_board::capabilities().valid()) {
        Serial.printf("QL/32 %s capability profile invalid\n", kBoardName);
        return;
    }

    display.begin(
        static_cast<gpio_num_t>(ql32_board::vgaRed),
        static_cast<gpio_num_t>(ql32_board::vgaGreen),
        static_cast<gpio_num_t>(ql32_board::vgaBlue),
        static_cast<gpio_num_t>(ql32_board::vgaHSync),
        static_cast<gpio_num_t>(ql32_board::vgaVSync));
    display.setResolution(VGA_320x200_70Hz);

    auto framePresenter = std::make_shared<ql32::platform::fabgl_vga::FabglFramePresenter>(canvas);
    const auto screenWidth = static_cast<std::uint32_t>(canvas.getWidth());
    const auto screenHeight = static_cast<std::uint32_t>(canvas.getHeight());
    const std::size_t fullScreen4BitBytes =
        (static_cast<std::size_t>(screenWidth) * screenHeight + 1) / 2;
    graphics = std::make_shared<ql32::graphics::WindowedService>(
        screenWidth, screenHeight, framePresenter, 4, fullScreen4BitBytes * 2);

    ps2.begin(fabgl::PS2Preset::KeyboardPort0);
    if (auto* keyboard = fabgl::PS2Controller::keyboard()) {
        Serial.println(keyboard->isKeyboardAvailable()
            ? "PS/2 keyboard detected on port 0"
            : "No PS/2 keyboard response on port 0");
        keyboardInput.emplace(*keyboard);
    } else {
        Serial.println("FabGL keyboard interface unavailable on port 0");
    }

#if defined(QL32_BOARD_OLIMEX_ESP32_SBC_FABGL)
    const bool sdReady = idfSdCard.mount({
        ql32_board::sdMosi, ql32_board::sdMiso, ql32_board::sdClock,
        ql32_board::sdChipSelect
    });
#else
    sdBus.begin(ql32_board::sdClock,
                ql32_board::sdMiso,
                ql32_board::sdMosi,
                ql32_board::sdChipSelect);
    const bool sdReady = SD.begin(ql32_board::sdChipSelect,
                                  sdBus, 4000000, "/sd", 5);
#endif

    Serial.printf("QL/32 %s bring-up ready\n", kBoardName);
    Serial.printf("Uptime ms: %llu\n",
                  static_cast<unsigned long long>(boardClock.monotonicMilliseconds()));
    const auto wallTime = boardClock.unixTimeSeconds();
    if (wallTime)
        Serial.printf("Unix time: %lld\n", static_cast<long long>(*wallTime));
    else
        Serial.println("Wall clock unsynchronized");
    if (sdReady) {
        Serial.println("SD storage ready");
    } else {
        Serial.println("SD storage unavailable");
#if defined(QL32_BOARD_OLIMEX_ESP32_SBC_FABGL)
        Serial.printf("SD diagnostic: %s\n", idfSdCard.error().c_str());
#endif
    }

    auto graphicsService = std::static_pointer_cast<ql32::graphics::Service>(graphics);
    std::string startupProgram =
        "INK 7\nTEXT 8,8,\"QL/32 VGA32 V1.4\"\n"
        "TEXT 8,24,\"FabGL VGA + PS/2 ready\"\n"
        "TEXT 8,36,\"BASIC prompt on PS/2 keyboard\"\n"
        "RECT 8,48,96,48\nLINE 8,48,103,95\nCIRCLE 150,72,24\n"
        "TEXT 8,112,\"BASIC VM ready\"\n"
        "PRINT \"QL/32 BASIC VM running\"\n";
    if (sdReady) {
        try {
#if defined(QL32_BOARD_OLIMEX_ESP32_SBC_FABGL)
            startupProgram = idfSdStorage.readText("startup.bas");
#else
            startupProgram = sdStorage.readText("startup.bas");
#endif
            Serial.println("Running SD:/startup.bas");
        } catch (const ql32::runtime::ServiceError& error) {
            if (error.code() != ql32::runtime::ServiceErrorCode::NotFound) {
                Serial.printf("SD startup read error: %s\n", error.what());
                return;
            }
            Serial.println("SD:/startup.bas not found; running built-in demo");
        }
    }

    ql32::vm::Services services;
#if defined(QL32_BOARD_OLIMEX_ESP32_SBC_FABGL)
    services.storage = std::shared_ptr<ql32::runtime::StorageService>(
        &idfSdStorage, [](ql32::runtime::StorageService*) {});
    auto fileChannels = std::shared_ptr<ql32::runtime::ChannelService>(
        &idfSdChannels, [](ql32::runtime::ChannelService*) {});
#else
    services.storage = std::shared_ptr<ql32::runtime::StorageService>(
        &sdStorage, [](ql32::runtime::StorageService*) {});
    auto fileChannels = std::shared_ptr<ql32::runtime::ChannelService>(
        &sdChannels, [](ql32::runtime::ChannelService*) {});
#endif
    services.channels = std::make_shared<ql32::runtime::ChannelRegistry>(
        std::move(fileChannels), [](std::string_view bytes) {
            Serial.write(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
        });
    services.ui = uiService;
    services.graphics = graphicsService;
    services.network = std::shared_ptr<ql32::runtime::NetworkService>(
        &boardNetwork, [](ql32::runtime::NetworkService*) {});
    services.memory = std::shared_ptr<ql32::runtime::MemoryService>(
        &boardMemory, [](ql32::runtime::MemoryService*) {});
    uiPresenter = std::make_unique<ql32::platform::esp32::LineUiPresenter>(
        uiService, serialOutput);
    scheduler = std::make_unique<ql32::vm::SessionScheduler>(std::move(services), 128);
    replTask = scheduler->launchInteractive(serialOutput, std::move(startupProgram));
}

void loop() {
    while (acceptsInput() && Serial.available() > 0) {
        receiveSerialCharacter(static_cast<char>(Serial.read()));
    }

    if (acceptsInput() && keyboardInput) {
        const auto event = keyboardInput->poll();
        if (event && event->pressed) {
            if (event->key == ql32::input::Key::Character &&
                event->character >= 32 && event->character <= 126) {
                appendInputCharacter(static_cast<char>(event->character));
            } else if (event->key == ql32::input::Key::Backspace && !inputLine.empty()) {
                eraseInputCharacter();
            } else if (event->key == ql32::input::Key::Enter) {
                submitInputLine();
            }
        }
    }
    advanceScheduler();
    delay(1);
}
