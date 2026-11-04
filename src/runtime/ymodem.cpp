#include "ql32/runtime/ymodem.hpp"

#include <array>
#include <charconv>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace ql32::runtime {
namespace {

constexpr std::uint8_t kSoh = 0x01;
constexpr std::uint8_t kStx = 0x02;
constexpr std::uint8_t kEot = 0x04;
constexpr std::uint8_t kAck = 0x06;
constexpr std::uint8_t kNak = 0x15;
constexpr std::uint8_t kCan = 0x18;
constexpr std::uint8_t kCrcRequest = 0x43;
constexpr std::size_t kPacketSize = 1024;
constexpr std::uint32_t kTimeoutMs = 10000;
constexpr unsigned kMaxRetries = 10;

std::uint16_t crc16(std::string_view bytes) {
    std::uint16_t crc = 0;
    for (const unsigned char byte : bytes) {
        crc ^= static_cast<std::uint16_t>(byte) << 8;
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc & 0x8000) ? static_cast<std::uint16_t>((crc << 1) ^ 0x1021)
                                 : static_cast<std::uint16_t>(crc << 1);
    }
    return crc;
}

void sendByte(const YModemIo& io, std::uint8_t byte) {
    io.writeBytes(&byte, 1);
}

int readByte(const YModemIo& io) {
    const int value = io.readByte(kTimeoutMs);
    if (value < 0) throw std::runtime_error("YMODEM receive timed out");
    return value & 0xff;
}

void waitFor(const YModemIo& io, std::uint8_t expected, const char* message) {
    for (unsigned attempt = 0; attempt < kMaxRetries; ++attempt) {
        const int value = io.readByte(kTimeoutMs);
        if (value == expected) return;
        if (value == kCan) throw std::runtime_error("YMODEM cancelled by peer");
        if (value < 0) continue;
    }
    throw std::runtime_error(message);
}

void sendPacket(const YModemIo& io, std::uint8_t marker, std::uint8_t sequence,
                std::string_view payload) {
    std::array<std::uint8_t, kPacketSize + 5> packet{};
    const std::size_t payloadSize = marker == kSoh ? 128 : kPacketSize;
    if (payload.size() != payloadSize)
        throw std::runtime_error("internal YMODEM packet size error");
    packet[0] = marker;
    packet[1] = sequence;
    packet[2] = static_cast<std::uint8_t>(~sequence);
    std::memcpy(packet.data() + 3, payload.data(), payloadSize);
    const auto crc = crc16(payload);
    packet[3 + payloadSize] = static_cast<std::uint8_t>(crc >> 8);
    packet[4 + payloadSize] = static_cast<std::uint8_t>(crc & 0xff);
    io.writeBytes(packet.data(), payloadSize + 5);
}

struct Packet {
    std::uint8_t marker = 0;
    std::uint8_t sequence = 0;
    std::array<std::uint8_t, kPacketSize> payload{};
    std::size_t payloadSize = 0;
};

bool readPacket(const YModemIo& io, Packet& packet) {
    const int marker = readByte(io);
    if (marker == kCan) throw std::runtime_error("YMODEM cancelled by peer");
    if (marker != kSoh && marker != kStx) return false;
    packet.marker = static_cast<std::uint8_t>(marker);
    packet.payloadSize = marker == kSoh ? 128 : kPacketSize;
    packet.sequence = static_cast<std::uint8_t>(readByte(io));
    const auto complement = static_cast<std::uint8_t>(readByte(io));
    for (std::size_t i = 0; i < packet.payloadSize; ++i)
        packet.payload[i] = static_cast<std::uint8_t>(readByte(io));
    const auto high = static_cast<std::uint8_t>(readByte(io));
    const auto low = static_cast<std::uint8_t>(readByte(io));
    const std::string_view payload(reinterpret_cast<const char*>(packet.payload.data()),
                                   packet.payloadSize);
    const auto expected = crc16(payload);
    return complement == static_cast<std::uint8_t>(~packet.sequence) &&
           expected == static_cast<std::uint16_t>((high << 8) | low);
}

std::string baseName(std::string_view path) {
    const auto separator = path.find_last_of("/:");
    return std::string(separator == std::string_view::npos ? path : path.substr(separator + 1));
}

std::string backupPath(const std::string& path) { return path + ".BAK"; }

void sendCancel(const YModemIo& io) noexcept {
    try {
        const std::array<std::uint8_t, 2> cancel{kCan, kCan};
        io.writeBytes(cancel.data(), cancel.size());
    } catch (...) {}
}

std::uint64_t parseFileSize(const std::uint8_t* field, std::size_t length) {
    std::size_t end = 0;
    while (end < length && field[end] != 0) ++end;
    while (end < length && field[end] == 0) ++end;
    std::size_t sizeEnd = end;
    while (sizeEnd < length && field[sizeEnd] != 0 && field[sizeEnd] != ' ') ++sizeEnd;
    if (end == length || sizeEnd == end)
        throw std::runtime_error("YMODEM header has no file size");
    std::uint64_t size = 0;
    const char* first = reinterpret_cast<const char*>(field + end);
    const char* last = reinterpret_cast<const char*>(field + sizeEnd);
    const auto parsed = std::from_chars(first, last, size);
    if (parsed.ec != std::errc{} || parsed.ptr != last)
        throw std::runtime_error("YMODEM header has an invalid file size");
    return size;
}

void receiveProtocol(const YModemIo& io, ChannelService& channels, SessionId session,
                     ChannelId channel) {
    Packet packet;
    bool headerReceived = false;
    std::uint64_t remaining = 0;
    std::uint8_t expectedSequence = 1;
    for (unsigned attempt = 0; attempt < kMaxRetries; ++attempt) {
        sendByte(io, kCrcRequest);
        const int marker = io.readByte(kTimeoutMs);
        if (marker == kCan) throw std::runtime_error("YMODEM cancelled by peer");
        if (marker == kSoh) {
            // readPacket consumes the marker; put it back through a tiny adapter.
            YModemIo prefixed = io;
            bool first = true;
            prefixed.readByte = [&, original = io.readByte](std::uint32_t timeout) mutable -> int {
                if (first) { first = false; return static_cast<int>(kSoh); }
                return original(timeout);
            };
            if (!readPacket(prefixed, packet)) {
                sendByte(io, kNak);
                continue;
            }
            if (packet.sequence != 0 || packet.payload[0] == 0) {
                sendByte(io, kCan);
                sendByte(io, kCan);
                throw std::runtime_error("YMODEM sender provided no file header");
            }
            remaining = parseFileSize(packet.payload.data(), packet.payloadSize);
            headerReceived = true;
            sendByte(io, kAck);
            sendByte(io, kCrcRequest);
            break;
        }
    }
    if (!headerReceived) throw std::runtime_error("YMODEM sender did not start");

    while (remaining != 0) {
        bool accepted = false;
        for (unsigned attempt = 0; attempt < kMaxRetries; ++attempt) {
            const int marker = io.readByte(kTimeoutMs);
            if (marker == kCan) throw std::runtime_error("YMODEM cancelled by peer");
            if (marker == kEot) {
                sendByte(io, kCan);
                sendByte(io, kCan);
                throw std::runtime_error("YMODEM file ended before declared size");
            }
            if (marker != kStx) {
                sendByte(io, kNak);
                continue;
            }
            YModemIo prefixed = io;
            bool first = true;
            prefixed.readByte = [&, original = io.readByte](std::uint32_t timeout) mutable -> int {
                if (first) { first = false; return static_cast<int>(kStx); }
                return original(timeout);
            };
            if (!readPacket(prefixed, packet)) {
                sendByte(io, kNak);
                continue;
            }
            if (packet.sequence == static_cast<std::uint8_t>(expectedSequence - 1)) {
                sendByte(io, kAck);
                accepted = true;
                break;
            }
            if (packet.sequence != expectedSequence) {
                sendByte(io, kNak);
                continue;
            }
            const std::size_t count = static_cast<std::size_t>(
                std::min<std::uint64_t>(remaining, packet.payloadSize));
            channels.writeBytes(session, channel,
                std::string_view(reinterpret_cast<const char*>(packet.payload.data()), count));
            remaining -= count;
            ++expectedSequence;
            sendByte(io, kAck);
            accepted = true;
            break;
        }
        if (!accepted) throw std::runtime_error("YMODEM data block retry limit exceeded");
    }

    waitFor(io, kEot, "YMODEM end-of-file not received");
    sendByte(io, kNak);
    waitFor(io, kEot, "YMODEM second end-of-file not received");
    sendByte(io, kAck);
    sendByte(io, kCrcRequest);
    bool finished = false;
    for (unsigned attempt = 0; attempt < kMaxRetries; ++attempt) {
        if (readPacket(io, packet) && packet.sequence == 0 && packet.payload[0] == 0) {
            sendByte(io, kAck);
            finished = true;
            break;
        }
        sendByte(io, kNak);
    }
    if (!finished) throw std::runtime_error("YMODEM batch terminator not received");
}

} // namespace

void ymodemSendFile(const std::string& path, StorageService& storage,
                    ChannelService& channels, SessionId session, const YModemIo& io) {
    const std::uint64_t fileSize = storage.size(path);
    const ChannelId channel = channels.open(session, path, ChannelMode::Read);
    try {
        if (io.ready) io.ready();
        waitFor(io, kCrcRequest, "YMODEM receiver did not request CRC mode");
        std::array<char, 128> header{};
        const std::string name = baseName(path);
        if (name.empty() || name.size() >= header.size())
            throw std::runtime_error("YMODEM filename is empty or too long");
        std::memcpy(header.data(), name.data(), name.size());
        const std::string length = std::to_string(fileSize);
        if (name.size() + 1 + length.size() >= header.size())
            throw std::runtime_error("YMODEM metadata does not fit");
        std::memcpy(header.data() + name.size() + 1, length.data(), length.size());
        sendPacket(io, kSoh, 0, std::string_view(header.data(), header.size()));
        waitFor(io, kAck, "YMODEM file header was not acknowledged");
        waitFor(io, kCrcRequest, "YMODEM receiver did not request file data");

        std::array<char, kPacketSize> block{};
        std::uint64_t remaining = fileSize;
        std::uint8_t sequence = 1;
        while (remaining != 0) {
            block.fill(static_cast<char>(0x1a));
            const std::size_t count = static_cast<std::size_t>(
                std::min<std::uint64_t>(remaining, block.size()));
            const std::string bytes = channels.readBytes(session, channel, count);
            std::memcpy(block.data(), bytes.data(), count);
            bool acknowledged = false;
            for (unsigned attempt = 0; attempt < kMaxRetries; ++attempt) {
                sendPacket(io, kStx, sequence,
                           std::string_view(block.data(), block.size()));
                const int response = io.readByte(kTimeoutMs);
                if (response == kAck) { acknowledged = true; break; }
                if (response == kCan) throw std::runtime_error("YMODEM cancelled by peer");
                if (response != kNak && response >= 0) continue;
            }
            if (!acknowledged) throw std::runtime_error("YMODEM data block retry limit exceeded");
            remaining -= count;
            ++sequence;
        }
        sendByte(io, kEot);
        waitFor(io, kNak, "YMODEM first end-of-file was not acknowledged");
        sendByte(io, kEot);
        waitFor(io, kAck, "YMODEM end-of-file was not acknowledged");
        waitFor(io, kCrcRequest, "YMODEM receiver did not request batch end");
        header.fill(0);
        sendPacket(io, kSoh, 0, std::string_view(header.data(), header.size()));
        waitFor(io, kAck, "YMODEM batch end was not acknowledged");
        channels.close(session, channel);
    } catch (...) {
        sendCancel(io);
        try { channels.close(session, channel); } catch (...) {}
        throw;
    }
}

void ymodemReceiveFile(const std::string& path, StorageService& storage,
                       ChannelService& channels, SessionId session, const YModemIo& io) {
    const std::string backup = backupPath(path);
    if (storage.exists(backup))
        throw ServiceError(ServiceErrorCode::IoError,
                           "receive stopped: backup already exists: " + backup);
    const bool hadPrevious = storage.exists(path);
    if (hadPrevious) storage.rename(path, backup);

    ChannelId channel = 0;
    bool opened = false;
    try {
        channel = channels.open(session, path, ChannelMode::Write);
        opened = true;
        if (io.ready) io.ready();
        receiveProtocol(io, channels, session, channel);
        channels.close(session, channel);
        opened = false;
    } catch (const std::exception& error) {
        sendCancel(io);
        if (opened) {
            try { channels.close(session, channel); } catch (...) {}
        }
        try {
            if (storage.exists(path)) storage.remove(path);
            if (hadPrevious) storage.rename(backup, path);
        } catch (const std::exception& cleanupError) {
            throw std::runtime_error(std::string(error.what()) +
                "; cleanup failed: " + cleanupError.what());
        }
        throw;
    }
    if (hadPrevious) storage.remove(backup);
}

} // namespace ql32::runtime
