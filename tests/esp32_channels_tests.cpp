#include "ql32/platform/esp32/sd_channels.hpp"
#include "ql32/runtime/storage.hpp"

#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

template <typename Function>
void requireServiceError(ql32::runtime::ServiceErrorCode code, Function&& function) {
    try {
        function();
    } catch (const ql32::runtime::ServiceError& error) {
        require(error.code() == code, "channel returned unexpected service error");
        return;
    }
    throw std::runtime_error("channel operation unexpectedly succeeded");
}

void testReadWriteAppendAndOwnership() {
    fs::FS filesystem;
    filesystem.contents("/input.txt") = "first\nsecond";
    ql32::platform::esp32::SdChannels channels(filesystem);

    const auto reader = channels.open(10, "input.txt", ql32::runtime::ChannelMode::Read);
    require(channels.readLine(10, reader) == "first", "first line mismatch");
    require(channels.readLine(10, reader) == "second", "unterminated last line mismatch");
    require(!channels.readLine(10, reader), "EOF did not return no line");
    require(channels.atEnd(10, reader), "EOF query failed");
    requireServiceError(ql32::runtime::ServiceErrorCode::InvalidHandle,
                        [&] { channels.readLine(11, reader); });
    channels.close(10, reader);

    const auto writer = channels.open(10, "output.txt", ql32::runtime::ChannelMode::Write);
    channels.write(10, writer, "one\n");
    channels.close(10, writer);
    const auto appender = channels.open(10, "output.txt", ql32::runtime::ChannelMode::Append);
    channels.write(10, appender, "two\n");
    channels.close(10, appender);
    require(filesystem.contents("/output.txt") == "one\ntwo\n",
            "write and append contents mismatch");
}

void testPathConfinementAndUnmountedCard() {
    fs::FS filesystem;
    ql32::platform::esp32::SdChannels channels(filesystem);
    requireServiceError(ql32::runtime::ServiceErrorCode::InvalidPath,
                        [&] { channels.open(1, "../escape", ql32::runtime::ChannelMode::Read); });
    requireServiceError(ql32::runtime::ServiceErrorCode::InvalidPath,
                        [&] { channels.open(1, "/absolute", ql32::runtime::ChannelMode::Read); });

    filesystem.setMounted(false);
    requireServiceError(ql32::runtime::ServiceErrorCode::Unavailable,
                        [&] { channels.open(1, "file.txt", ql32::runtime::ChannelMode::Read); });
}

void testCloseSession() {
    fs::FS filesystem;
    filesystem.contents("/input.txt") = "value\n";
    ql32::platform::esp32::SdChannels channels(filesystem);
    const auto channel = channels.open(42, "input.txt", ql32::runtime::ChannelMode::Read);
    channels.closeSession(42);
    requireServiceError(ql32::runtime::ServiceErrorCode::InvalidHandle,
                        [&] { channels.atEnd(42, channel); });
}

} // namespace

int main() {
    try {
        testReadWriteAppendAndOwnership();
        testPathConfinementAndUnmountedCard();
        testCloseSession();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
