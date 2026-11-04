#include "ql32/platform/linux/directory_channels.hpp"
#include "ql32/platform/linux/directory_storage.hpp"
#include "ql32/runtime/ymodem.hpp"

#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct Link {
    std::mutex mutex;
    std::condition_variable ready;
    std::deque<std::uint8_t> toA;
    std::deque<std::uint8_t> toB;

    ql32::runtime::YModemIo endpoint(bool a) {
        ql32::runtime::YModemIo io;
        io.readByte = [this, a](std::uint32_t timeout) {
            std::unique_lock lock(mutex);
            auto& queue = a ? toA : toB;
            if (!ready.wait_for(lock, std::chrono::milliseconds(timeout),
                                [&] { return !queue.empty(); })) return -1;
            const int value = queue.front();
            queue.pop_front();
            return value;
        };
        io.writeBytes = [this, a](const std::uint8_t* bytes, std::size_t size) {
            {
                std::lock_guard lock(mutex);
                auto& queue = a ? toB : toA;
                for (std::size_t i = 0; i < size; ++i) queue.push_back(bytes[i]);
            }
            ready.notify_all();
        };
        return io;
    }
};

void testBinaryRoundTripAndBackup() {
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() /
        ("ql32-ymodem-test-" + std::to_string(unique));
    const auto sourceRoot = root / "source";
    const auto targetRoot = root / "target";
    std::filesystem::create_directories(sourceRoot);
    std::filesystem::create_directories(targetRoot);
    try {
        ql32::platform::linux_host::DirectoryStorage sourceStorage(sourceRoot);
        ql32::platform::linux_host::DirectoryStorage targetStorage(targetRoot);
        ql32::platform::linux_host::DirectoryChannels sourceChannels(sourceRoot);
        ql32::platform::linux_host::DirectoryChannels targetChannels(targetRoot);

        std::string bytes(2501, '\0');
        for (std::size_t i = 0; i < bytes.size(); ++i)
            bytes[i] = static_cast<char>((i * 37) & 0xff);
        bytes[7] = '\0';
        bytes[1024] = '\r';
        bytes[2048] = '\n';
        sourceStorage.writeText("payload.bin", bytes);
        targetStorage.writeText("payload.bin", "previous file contents");

        Link link;
        const auto senderIo = link.endpoint(true);
        const auto receiverIo = link.endpoint(false);
        std::string sendError;
        std::string receiveError;
        std::thread sender([&] {
            try {
                ql32::runtime::ymodemSendFile("payload.bin", sourceStorage,
                    sourceChannels, 1, senderIo);
            } catch (const std::exception& error) { sendError = error.what(); }
        });
        std::thread receiver([&] {
            try {
                ql32::runtime::ymodemReceiveFile("payload.bin", targetStorage,
                    targetChannels, 2, receiverIo);
            } catch (const std::exception& error) { receiveError = error.what(); }
        });
        sender.join();
        receiver.join();

        require(sendError.empty(), "YMODEM sender failed during binary round-trip");
        require(receiveError.empty(), "YMODEM receiver failed during binary round-trip");
        require(targetStorage.readText("payload.bin") == bytes,
                "YMODEM changed binary file contents");
        require(!targetStorage.exists("payload.bin.BAK"),
                "successful YMODEM receive retained its backup");

        targetStorage.writeText("rollback.bin", "old contents");
        targetStorage.writeText("rollback.bin.BAK", "existing backup");
        ql32::runtime::YModemIo unused;
        unused.readByte = [](std::uint32_t) { return -1; };
        unused.writeBytes = [](const std::uint8_t*, std::size_t) {};
        bool conflictRejected = false;
        try {
            ql32::runtime::ymodemReceiveFile("rollback.bin", targetStorage,
                targetChannels, 3, unused);
        } catch (const std::exception&) { conflictRejected = true; }
        require(conflictRejected && targetStorage.readText("rollback.bin") == "old contents" &&
                    targetStorage.readText("rollback.bin.BAK") == "existing backup",
                "existing .BAK did not stop receive without changing files");
        targetStorage.remove("rollback.bin.BAK");

        bool failedReceive = false;
        try {
            ql32::runtime::ymodemReceiveFile("rollback.bin", targetStorage,
                targetChannels, 4, unused);
        } catch (const std::exception&) { failedReceive = true; }
        require(failedReceive && targetStorage.readText("rollback.bin") == "old contents" &&
                    !targetStorage.exists("rollback.bin.BAK"),
                "failed receive did not restore its previous file");
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
        throw;
    }
    std::filesystem::remove_all(root);
}

} // namespace

int main() {
    testBinaryRoundTripAndBackup();
}
