#include "ql32/platform/linux/directory_channels.hpp"
#include "ql32/platform/linux/directory_storage.hpp"
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
            "WRITE channel, \"alpha\"\n"
            "WRITE channel, \"\"\n"
            "CLOSE channel\n"
            "channel = OPEN(\"lines.txt\", \"READ\")\n"
            "PRINT READ$(channel)\n"
            "PRINT EOF(channel)\n"
            "PRINT READ$(channel)\n"
            "PRINT EOF(channel)\n"
            "CLOSE channel\n", output);
        require(output.str() == "alpha\n0\n\n1\n",
                "BASIC channel calls handled lines or EOF incorrectly");

        bool arityChecked = false;
        try {
            interpreter.execute("CLOSE\n", output);
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

void testBinaryChannelRandomAccess() {
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() /
        ("ql32-binary-channel-test-" + std::to_string(unique));
    std::filesystem::create_directories(root);
    try {
        {
            std::ofstream seed(root / "binary.bin", std::ios::binary);
            const char payload[] = {'A', '\0', 'B', 'C'};
            seed.write(payload, sizeof(payload));
        }
        {
            std::ofstream seed(root / "line.txt", std::ios::binary);
            seed << "from file\n";
        }
        auto channels = std::make_shared<ql32::platform::linux_host::DirectoryChannels>(root);
        ql32::Interpreter interpreter({}, channels);
        std::ostringstream output;
        interpreter.execute(
            "source = OPEN(\"binary.bin\", \"READ\")\n"
            "#source IN 2, first$\n"
            "#source AT 1\n"
            "#source IN 3, packet$\n"
            "CLOSE source\n"
            "target = OPEN(\"copy.bin\", \"WRITE\")\n"
            "#target OUT 3, packet$\n"
            "CLOSE target\n"
            "PRINT LEN(packet$)\n", output);
        require(output.str() == "3\n", "binary channel transfer or seek failed");
        std::ifstream copy(root / "copy.bin", std::ios::binary);
        const std::string copied{std::istreambuf_iterator<char>(copy), {}};
        require(copied.size() == 3 && copied[0] == '\0' && copied[1] == 'B' &&
                    copied[2] == 'C', "binary write did not preserve NUL or exact bytes");
        std::ostringstream inputOutput;
        interpreter.execute(
            "filech = OPEN(\"line.txt\", \"READ\")\n"
            "INPUT #1, \"File prompt: \", #filech, line$\n"
            "PRINT line$\nCLOSE filech\n", inputOutput);
        require(inputOutput.str() == "File prompt: from file\n",
                "explicit prompt and file input channels did not work independently");
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
        throw;
    }
    std::filesystem::remove_all(root);
}

void testInputLineAndSessionResume() {
    auto ui = std::make_shared<ql32::ui::QueuedService>();
    ql32::Interpreter interpreter({}, {}, ui);
    std::ostringstream output;
    auto status = interpreter.executeResumable(
        "INPUT \"Name?\"; name$\nPRINT name$\n", output);
    require(status == ql32::ExecutionStatus::Suspended,
            "keyboard INPUT did not suspend the session");
    const auto request = ui->takeNextRequest();
    require(request && request->kind == ql32::ui::RequestKind::Input,
            "INPUT did not publish a session-owned UI request");
    require(ui->submitInput(request->id, "Ada"), "input line was rejected");
    status = interpreter.resume(output);
    require(status == ql32::ExecutionStatus::Completed &&
                output.str().find("Name?Ada") != std::string::npos &&
                output.str().find("Ada\n") != std::string::npos,
            "INPUT did not store and return the submitted line");
}

void testDefaultAndRoutedOutputChannels() {
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() /
        ("ql32-output-routing-test-" + std::to_string(unique));
    std::filesystem::create_directories(root);
    try {
        auto channels = std::make_shared<ql32::platform::linux_host::DirectoryChannels>(root);
        auto storage = std::make_shared<ql32::platform::linux_host::DirectoryStorage>(root);
        std::ostringstream console;
        {
            ql32::Interpreter interpreter(storage, channels);
            interpreter.execute("file = OPEN(\"routed.txt\", \"WRITE\")\n", console);
            interpreter.execute("CHANNEL #1 TO #file\nPRINT \"print target\"\n", console);
            interpreter.execute("WRITE 1, \"write target\"\n", console);
            interpreter.executeInteractive("10 PRINT \"stored\"\n", console);
            interpreter.executeInteractive("LIST\n", console);
            interpreter.executeInteractive("DIR\n", console);
            interpreter.execute("CHANNEL #1 TO #0\nPRINT #1,\"console target\"\n",
                                console);
            interpreter.execute("WRITE 1, \"write console target\"\n", console);
        }
        std::ifstream result(root / "routed.txt", std::ios::binary);
        const std::string contents{std::istreambuf_iterator<char>(result), {}};
        require(contents == "print target\nwrite target\n10 PRINT \"stored\"\nrouted.txt\n",
                "PRINT, WRITE, LIST, or DIR did not follow channel routing");
        require(console.str() == "console target\nwrite console target\n",
                "explicit PRINT or WRITE did not follow its channel route to #0");
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
        throw;
    }
    std::filesystem::remove_all(root);
}

void testGetChannelInspection() {
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() /
        ("ql32-getchannel-test-" + std::to_string(unique));
    std::filesystem::create_directories(root);
    try {
        {
            std::ofstream header(root / "ql32.inc", std::ios::binary);
            header << "CONST KChannelCount = 0\n"
                      "CONST KChannelHandle = 1\n"
                      "CONST KChannelType = 2\n";
        }
        auto storage = std::make_shared<ql32::platform::linux_host::DirectoryStorage>(root);
        auto channels = std::make_shared<ql32::platform::linux_host::DirectoryChannels>(root);
        ql32::Interpreter interpreter(storage, channels);
        std::ostringstream output;
        interpreter.execute(
            "INCLUDE \"ql32.inc\"\n"
            "file = OPEN(\"out.txt\", \"WRITE\")\n"
            "PRINT GETCHANNEL(KChannelCount)\n"
            "PRINT GETCHANNEL(KChannelHandle, 0)\n"
            "PRINT GETCHANNEL(KChannelHandle, 1)\n"
            "PRINT GETCHANNEL(KChannelHandle, 2)\n"
            "PRINT GETCHANNEL(KChannelType, #file)\n"
            "CLOSE file\n"
            "PRINT GETCHANNEL(KChannelCount)\n", output);
        require(output.str() == "3\n0\n1\n2\nFILE\n2\n",
                "GETCHANNEL did not enumerate reserved and open channels correctly");

        bool badPositionRejected = false;
        try {
            interpreter.execute("INCLUDE \"ql32.inc\"\n"
                                "PRINT GETCHANNEL(KChannelHandle, 9)\n", output);
        } catch (const std::runtime_error& error) {
            badPositionRejected = std::string(error.what()).find("channel position out of range") !=
                                  std::string::npos;
        }
        require(badPositionRejected, "GETCHANNEL accepted an out-of-range position");

        bool closedHandleRejected = false;
        try {
            interpreter.execute("INCLUDE \"ql32.inc\"\n"
                                "PRINT GETCHANNEL(KChannelType, #2)\n", output);
        } catch (const std::runtime_error& error) {
            closedHandleRejected = std::string(error.what()).find("channel handle is not open") !=
                                   std::string::npos;
        }
        require(closedHandleRejected, "GETCHANNEL accepted a closed handle");
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
        testBinaryChannelRandomAccess();
        testInputLineAndSessionResume();
        testDefaultAndRoutedOutputChannels();
        testGetChannelInspection();
    } catch (const std::exception& error) {
        std::cerr << "channel test failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
