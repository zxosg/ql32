#include "ql32/interpreter.hpp"
#include "ql32/platform/linux/virtual_hardware.hpp"
#include <iostream>
#include <memory>
#include <sstream>

int main() {
    auto hardware = std::make_shared<ql32::platform::linux_host::VirtualHardware>();
    auto channels = std::make_shared<ql32::runtime::ChannelRegistry>(
        nullptr, ql32::runtime::ChannelRegistry::SerialWriter{}, hardware);
    ql32::Interpreter interpreter({}, channels);
    std::ostringstream output;
    try {
        interpreter.execute(
            "g=OPEN(5,7,0)\nother=OPEN(5,7,0)\nPRINT other\nPRINT ERR$(-other)\n"
            "PRINT IN(g)\nCLOSE g\nother=OPEN(5,7,0)\n"
            "PRINT other>=0\nCLOSE other\n", output);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    if (output.str() != "-22\nResource is already in use\n0\n1\n") {
        std::cerr << "unexpected pin-conflict result: " << output.str();
        return 1;
    }
    output.str("");
    output.clear();
    try {
        interpreter.executeInteractive("g=OPEN(5,7,0)\n", output);
        interpreter.executeInteractive("other=OPEN(5,7,0)\n", output);
        interpreter.executeInteractive("PRINT other\n", output);
        interpreter.executeInteractive("CLOSE g\n", output);
        interpreter.executeInteractive("other=OPEN(5,7,0)\n", output);
        interpreter.executeInteractive("PRINT other>=0\n", output);
        interpreter.executeInteractive("CLOSE other\n", output);
    } catch (const std::exception& error) {
        std::cerr << "interactive conflict: " << error.what() << '\n';
        return 1;
    }
    if (output.str() != "-22\n1\n") {
        std::cerr << "unexpected interactive result: " << output.str();
        return 1;
    }
    const auto owned = hardware->open(900, ql32::runtime::ChannelDevice::Gpio,
                                      {7.0, 0.0});
    output.str("");
    output.clear();
    try {
        interpreter.execute("other=OPEN(5,7,0)\nPRINT other\n", output);
        hardware->close(900, owned);
        interpreter.execute("other=OPEN(5,7,0)\nPRINT other>=0\nCLOSE other\n", output);
    } catch (const std::exception& error) {
        std::cerr << "cross-session conflict: " << error.what() << '\n';
        return 1;
    }
    if (output.str() != "-22\n1\n") {
        std::cerr << "unexpected cross-session result: " << output.str();
        return 1;
    }
    return 0;
}
