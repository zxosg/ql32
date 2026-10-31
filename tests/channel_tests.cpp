#include "ql32/platform/linux/directory_channels.hpp"
#include "ql32/interpreter.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void testDirectoryChannels() {
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() /
        ("ql32-channel-test-" + std::to_string(unique));
    std::filesystem::create_directories(root);
    try {
        {
            std::ofstream seed(root / "input.txt", std::ios::binary);
            seed << "first\nsecond\n";
        }
        ql32::platform::linux_host::DirectoryChannels channels(root);
        const auto reader = channels.open(11, "input.txt", ql32::runtime::ChannelMode::Read);
        require(channels.readLine(11, reader) == "first", "first channel line was incorrect");
        require(channels.readLine(11, reader) == "second", "second channel line was incorrect");
        require(!channels.readLine(11, reader), "channel did not report EOF");

        bool ownershipEnforced = false;
        try {
            (void)channels.readLine(22, reader);
        } catch (const ql32::runtime::ServiceError& error) {
            ownershipEnforced = error.code() == ql32::runtime::ServiceErrorCode::InvalidHandle;
        }
        require(ownershipEnforced, "channel could be used by another session");
        channels.close(11, reader);

        const auto writer = channels.open(11, "output.txt", ql32::runtime::ChannelMode::Write);
        channels.write(11, writer, "one\n");
        channels.close(11, writer);
        const auto appender = channels.open(22, "output.txt", ql32::runtime::ChannelMode::Append);
        channels.write(22, appender, "two\n");
        channels.close(22, appender);
        require(std::filesystem::file_size(root / "output.txt") == 8,
                "write and append channels did not preserve all bytes");

        const auto sessionChannel = channels.open(11, "session.txt",
                                                   ql32::runtime::ChannelMode::Write);
        channels.write(11, sessionChannel, "owned\n");
        channels.closeSession(11);
        bool closedWithSession = false;
        try {
            channels.write(11, sessionChannel, "stale");
        } catch (const ql32::runtime::ServiceError& error) {
            closedWithSession = error.code() == ql32::runtime::ServiceErrorCode::InvalidHandle;
        }
        require(closedWithSession, "session cleanup left its channel usable");

        bool traversalRejected = false;
        try {
            (void)channels.open(11, "../outside.txt", ql32::runtime::ChannelMode::Write);
        } catch (const ql32::runtime::ServiceError& error) {
            traversalRejected = error.code() == ql32::runtime::ServiceErrorCode::InvalidPath;
        }
        require(traversalRejected, "channel path escaped the configured root");
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
        throw;
    }
    std::filesystem::remove_all(root);
}

void testBasicChannelCalls() {
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() /
        ("ql32-basic-channel-test-" + std::to_string(unique));
    std::filesystem::create_directories(root);
    try {
        auto channels = std::make_shared<ql32::platform::linux_host::DirectoryChannels>(root);
        ql32::Interpreter interpreter({}, channels);
        std::ostringstream output;
        interpreter.execute(
            "channel = OPEN(\"lines.txt\", \"WRITE\")\n"
            "WRITE(channel, \"alpha\")\n"
            "WRITE(channel, \"\")\n"
            "CLOSE(channel)\n"
            "channel = OPEN(\"lines.txt\", \"READ\")\n"
            "PRINT READ$(channel)\n"
            "PRINT EOF(channel)\n"
            "PRINT READ$(channel)\n"
            "PRINT EOF(channel)\n"
            "CLOSE(channel)\n", output);
        require(output.str() == "alpha\n0\n\n1\n",
                "BASIC channel calls handled lines or EOF incorrectly");

        bool arityChecked = false;
        try {
            interpreter.execute("CLOSE()\n", output);
        } catch (const std::runtime_error& error) {
            arityChecked = std::string(error.what()).find("incorrect argument count") !=
                           std::string::npos;
        }
        require(arityChecked, "BASIC channel built-in accepted an invalid argument count");
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
        throw;
    }
    std::filesystem::remove_all(root);
}

} // namespace

int main() {
    try {
        testDirectoryChannels();
        testBasicChannelCalls();
    } catch (const std::exception& error) {
        std::cerr << "channel test failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
