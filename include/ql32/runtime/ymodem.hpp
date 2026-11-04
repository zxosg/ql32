#pragma once

#include "ql32/runtime/channel.hpp"
#include "ql32/runtime/storage.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace ql32::runtime {

struct YModemIo {
    // Return -1 when the timeout expires, otherwise return one received byte.
    std::function<int(std::uint32_t)> readByte;
    std::function<void(const std::uint8_t*, std::size_t)> writeBytes;
    std::function<void()> ready;
};

// Single-file YMODEM using CRC-16 and 1024-byte data packets.
void ymodemSendFile(const std::string& path, StorageService& storage,
                    ChannelService& channels, SessionId session, const YModemIo& io);
void ymodemReceiveFile(const std::string& path, StorageService& storage,
                       ChannelService& channels, SessionId session, const YModemIo& io);

} // namespace ql32::runtime
