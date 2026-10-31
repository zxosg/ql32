#include "ql32/interpreter.hpp"
#include "ql32/platform/linux/directory_channels.hpp"
#include "ql32/platform/linux/directory_storage.hpp"
#include "ql32/platform/linux/console_ui.hpp"
#include "ql32/platform/linux/svg_graphics.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#if defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#endif

int main(int argc, char** argv) {
    if (argc > 2) {
        std::cerr << "usage: ql32 [program.bas]\n";
        return 2;
    }

    std::string source;
    if (argc == 2) {
        std::ifstream file(argv[1], std::ios::binary);
        if (!file) {
            std::cerr << "ql32: cannot open " << argv[1] << '\n';
            return 2;
        }
        source.assign(std::istreambuf_iterator<char>(file), {});
    } else {
#if defined(__unix__) || defined(__APPLE__)
        if (::isatty(STDIN_FILENO)) {
            auto storage = std::make_shared<ql32::platform::linux_host::DirectoryStorage>(
                std::filesystem::current_path());
            auto channels = std::make_shared<ql32::platform::linux_host::DirectoryChannels>(
                std::filesystem::current_path());
            auto ui = std::make_shared<ql32::ui::QueuedService>();
            auto graphics = std::make_shared<ql32::platform::linux_host::SvgGraphics>();
            ql32::Interpreter interpreter(storage, channels, ui, graphics);
            ql32::platform::linux_host::ConsoleUi console(ui, std::cin, std::cout);
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
        auto channels = std::make_shared<ql32::platform::linux_host::DirectoryChannels>(
            std::filesystem::current_path());
        auto ui = std::make_shared<ql32::ui::QueuedService>();
        auto graphics = std::make_shared<ql32::platform::linux_host::SvgGraphics>();
        ql32::Interpreter interpreter(storage, channels, ui, graphics);
        if (argc == 2) {
            ql32::platform::linux_host::ConsoleUi console(ui, std::cin, std::cout);
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
