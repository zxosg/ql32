#include "ql32/interpreter.hpp"
#include "ql32/platform/linux/directory_channels.hpp"
#include "ql32/platform/linux/virtual_hardware.hpp"
#include "ql32/platform/linux/directory_storage.hpp"
#include "ql32/platform/linux/console_ui.hpp"
#include "ql32/graphics/windowed_service.hpp"
#include "ql32/platform/linux/memory.hpp"
#include "ql32/platform/linux/ppm_presenter.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#if defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#endif

int main(int argc, char** argv) {
    const bool consoleOnly = argc > 1 && std::string(argv[1]) == "--console";
    const bool hasProgram = consoleOnly ? argc == 3 : argc == 2;
    if ((!consoleOnly && argc > 2) || (consoleOnly && argc > 3)) {
        std::cerr << "usage: ql32 [--console] [program.bas]\n";
        return 2;
    }

    std::string source;
    if (hasProgram) {
        std::ifstream file(argv[consoleOnly ? 2 : 1], std::ios::binary);
        if (!file) {
            std::cerr << "ql32: cannot open " << argv[consoleOnly ? 2 : 1] << '\n';
            return 2;
        }
        source.assign(std::istreambuf_iterator<char>(file), {});
    } else {
#if defined(__unix__) || defined(__APPLE__)
        if (!consoleOnly && argc == 1 && ::isatty(STDIN_FILENO)) {
            auto storage = std::make_shared<ql32::platform::linux_host::DirectoryStorage>(
                std::filesystem::current_path());
            auto fileChannels = std::make_shared<ql32::platform::linux_host::DirectoryChannels>(
                std::filesystem::current_path());
            auto hardware = std::make_shared<ql32::platform::linux_host::VirtualHardware>();
            hardware->setSerialLoopback(true);
            auto channels = std::make_shared<ql32::runtime::ChannelRegistry>(
                fileChannels, [](std::string_view bytes) {
                    std::cout.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
                    std::cout.flush();
                }, hardware);
            auto ui = std::make_shared<ql32::ui::QueuedService>();
            auto presenter = std::make_shared<ql32::platform::linux_host::PpmPresenter>(
                "ql32-screen.ppm");
            auto graphics = std::make_shared<ql32::graphics::WindowedService>(
                640, 480, presenter, 4);
            auto memory = std::make_shared<ql32::platform::linux_host::MemoryService>();
            ql32::Interpreter interpreter(storage, channels, ui, graphics, {}, memory);
            ql32::platform::linux_host::ConsoleUi console(ui, std::cin, std::cout, graphics);
            interpreter.runRepl(std::cin, std::cout, std::cerr, true,
                                [&] { return console.dispatchNext(); });
            return 0;
        }
#endif
        source.assign(std::istreambuf_iterator<char>(std::cin), {});
    }

    try {
        auto storage = std::make_shared<ql32::platform::linux_host::DirectoryStorage>(
            std::filesystem::current_path());
        auto fileChannels = std::make_shared<ql32::platform::linux_host::DirectoryChannels>(
            std::filesystem::current_path());
        auto hardware = std::make_shared<ql32::platform::linux_host::VirtualHardware>();
        hardware->setSerialLoopback(true);
        auto channels = std::make_shared<ql32::runtime::ChannelRegistry>(
            fileChannels, [](std::string_view bytes) {
                std::cout.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
                std::cout.flush();
            }, hardware);
        auto ui = std::make_shared<ql32::ui::QueuedService>();
        auto presenter = std::make_shared<ql32::platform::linux_host::PpmPresenter>(
            "ql32-screen.ppm");
        std::shared_ptr<ql32::graphics::Service> graphics;
        if (!consoleOnly)
            graphics = std::make_shared<ql32::graphics::WindowedService>(
                640, 480, presenter, 4);
        auto memory = std::make_shared<ql32::platform::linux_host::MemoryService>();
        ql32::Interpreter interpreter(storage, channels, ui, graphics, {}, memory);
        if (hasProgram) {
            ql32::platform::linux_host::ConsoleUi console(
                ui, std::cin, std::cout,
#if defined(__unix__) || defined(__APPLE__)
                ::isatty(STDIN_FILENO) ? std::dynamic_pointer_cast<ql32::graphics::WindowedService>(graphics) : nullptr
#else
                nullptr
#endif
            );
            auto status = interpreter.executeResumable(source, std::cout);
            while (status == ql32::ExecutionStatus::Suspended) {
                if (!console.dispatchNext())
                    throw std::runtime_error("suspended UI request was not queued");
                status = interpreter.resume(std::cout);
            }
        } else {
            interpreter.executeInteractive(source, std::cout);
        }
    } catch (const std::exception& error) {
        std::cerr << "ql32: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
