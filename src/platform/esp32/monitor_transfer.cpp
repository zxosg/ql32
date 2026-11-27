#include "ql32/platform/esp32/monitor_transfer.hpp"

#include "ql32/runtime/ymodem.hpp"

#include "ql32/platform/esp32/diagnostics.hpp"
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstdio>

#include <algorithm>
#include <cctype>
#include <limits>
#include <stdexcept>
#include <string>

namespace ql32::platform::esp32 {
namespace {

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r");
    return value.substr(first, last - first + 1);
}

std::string uppercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return value;
}

bool readPath(std::string_view input, std::string& path) {
    if (input.empty() || input.front() != '"') return false;
    for (std::size_t i = 1; i < input.size(); ++i) {
        if (input[i] != '"') {
            path.push_back(input[i]);
            continue;
        }
        if (i + 1 < input.size() && input[i + 1] == '"') {
            path.push_back('"');
            ++i;
            continue;
        }
        return trim(std::string(input.substr(i + 1))).empty() && !path.empty();
    }
    return false;
}

} // namespace

bool handleMonitorTransferCommand(std::string_view input,
                                  runtime::StorageService& storage,
                                  runtime::ChannelService& channels,
                                  Stream& serial,
                                  const std::function<runtime::YModemSource()>&
                                      screenCapture) {
    const std::string line = trim(std::string(input));
    const std::size_t separator = line.find_first_of(" \t");
    const std::string command = uppercase(line.substr(0, separator));
    if (command == "HEAP") {
#if defined(QL32_HEAP_DIAGNOSTICS)
        const std::string arg = separator == std::string::npos ? std::string() :
            uppercase(trim(line.substr(separator + 1)));
        if (arg == "FREEMAP") {
            // Diagnostic: claim the largest free block repeatedly to list the free blocks
            // (address, size), then release them.
            static void* held[600];
            static std::size_t heldSize[600];
            std::size_t n = 0;
            serial.println("FREEMAP|BEGIN");
            while (n < 600) {
                const std::size_t big = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
                if (big < 512) break;
                void* p = heap_caps_malloc(big, MALLOC_CAP_8BIT);
                if (!p) break;
                heldSize[n] = big;
                held[n++] = p;
                char row[64];
                std::snprintf(row, sizeof(row), "FREEMAP|%p|%u", p, static_cast<unsigned>(big));
                serial.println(row);
            }
            while (n) heap_caps_free(held[--n]);
            serial.println("FREEMAP|END");
            return true;
        }
        if (arg == "STATS") {
            multi_heap_info_t info{};
            heap_caps_get_info(&info, MALLOC_CAP_8BIT);
            auto& d = ql32::platform::esp32::diagnostics();
            auto stack = [](void* h) {
                return h ? static_cast<unsigned>(uxTaskGetStackHighWaterMark(
                               static_cast<TaskHandle_t>(h))) : 0u;
            };
            char buf[280];
            std::snprintf(buf, sizeof(buf),
                "HEAPSTATS|free=%u|largest=%u|min=%u|loopstack=%u|musicstack=%u|"
                "musiciostack=%u|failedalloc=%u|lastfail=%u|musicblocks=%u|musiclate=%u|"
                "musicwriteerr=%u|musicmaxgap=%u",
                static_cast<unsigned>(info.total_free_bytes),
                static_cast<unsigned>(info.largest_free_block),
                static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT)),
                static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)),
                stack(d.musicTask.load()), stack(d.musicIoTask.load()),
                static_cast<unsigned>(d.failedAllocations.load()),
                static_cast<unsigned>(d.lastFailedAllocBytes.load()),
                static_cast<unsigned>(d.musicBlocks.load()),
                static_cast<unsigned>(d.musicLateBlocks.load()),
                static_cast<unsigned>(d.musicWriteErrors.load()),
                static_cast<unsigned>(d.musicMaxGapUs.load()));
            serial.println(buf);
            return true;
        }
        if (!arg.empty()) {
            serial.println("Usage: HEAP [STATS]");
            return true;
        }
        serial.println("HEAPDUMP|BEGIN");
        serial.flush();
        heap_caps_dump_all();
        serial.println("HEAPDUMP|END");
        serial.flush();
#else
        serial.println("Heap dump is disabled in this firmware profile");
#endif
        return true;
    }
    if (command != "RECV" && command != "SEND" && command != "SCREEN") return false;

    std::string path;
    const std::string_view argument = separator == std::string::npos
        ? std::string_view{} : std::string_view(line).substr(separator + 1);
    if (command == "SCREEN" && !trim(std::string(argument)).empty()) {
        serial.println("Usage: SCREEN");
        return true;
    }
    if (command == "SCREEN" && !screenCapture) {
        serial.println("Screen capture is unavailable on this target");
        return true;
    }
    if (command != "SCREEN" && !readPath(argument, path)) {
        serial.println(command == "RECV"
            ? "Usage: RECV \"drive:/path\""
            : "Usage: SEND \"drive:/path\"");
        return true;
    }

    // A terminal may send CRLF. The CR has already submitted the monitor
    // command; consume a queued LF so it cannot be mistaken for a YMODEM byte.
    if (serial.available() > 0 && serial.peek() == '\n') (void)serial.read();

    runtime::YModemIo io;
    io.readByte = [&serial](std::uint32_t timeoutMs) -> int {
        const std::uint32_t started = millis();
        while (serial.available() == 0 &&
               static_cast<std::uint32_t>(millis() - started) < timeoutMs)
            delay(1);
        return serial.available() == 0 ? -1 : serial.read();
    };
    io.writeBytes = [&serial](const std::uint8_t* bytes, std::size_t count) {
        serial.write(bytes, count);
        serial.flush();
    };
    io.ready = [&serial, &command] {
        serial.print("YMODEM READY ");
        serial.print(command.c_str());
        serial.print("\r\n");
        serial.flush();
    };

    constexpr runtime::SessionId transferSession =
        std::numeric_limits<runtime::SessionId>::max();
    try {
        if (command == "RECV")
            runtime::ymodemReceiveFile(path, storage, channels, transferSession, io);
        else if (command == "SEND")
            runtime::ymodemSendFile(path, storage, channels, transferSession, io);
        else
            runtime::ymodemSendSource(screenCapture(), io);
        if (command == "RECV") serial.println("Transfer received successfully");
        else if (command == "SCREEN") serial.println("Screen capture sent successfully");
        else serial.println("Transfer sent successfully");
    } catch (const std::exception& error) {
        const auto* message = error.what();
        serial.print("Transfer failed: ");
        serial.println(message && *message ? message : "unknown transfer error");
    }
    return true;
}

} // namespace ql32::platform::esp32
