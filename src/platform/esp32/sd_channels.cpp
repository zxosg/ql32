#include "ql32/platform/esp32/sd_channels.hpp"

#include <array>
#include <limits>

namespace ql32::platform::esp32 {
namespace {

using runtime::ServiceError;
using runtime::ServiceErrorCode;

} // namespace

SdChannels::~SdChannels() {
    std::lock_guard lock(mutex_);
    for (auto& [id, channel] : channels_) {
        (void)id;
        channel.file.close();
    }
}

void SdChannels::ensureAvailable() {
    fs::File root = filesystem_.open("/", FILE_READ);
    if (!root || !root.isDirectory())
        throw ServiceError(ServiceErrorCode::Unavailable,
                           "channel: SD filesystem is not mounted");
    root.close();
}

std::string SdChannels::resolve(const std::string& path) {
    if (path.empty() || path.find('\0') != std::string::npos ||
        path.find('\\') != std::string::npos || path.front() == '/')
        throw ServiceError(ServiceErrorCode::InvalidPath,
                           "channel path must be relative to the SD root");

    std::string result;
    std::size_t start = 0;
    while (start <= path.size()) {
        const auto end = path.find('/', start);
        const auto length = (end == std::string::npos ? path.size() : end) - start;
        const std::string component = path.substr(start, length);
        if (component == "..")
            throw ServiceError(ServiceErrorCode::InvalidPath,
                               "channel path cannot escape the SD root");
        if (!component.empty() && component != ".") {
            result.push_back('/');
            result += component;
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    if (result.empty())
        throw ServiceError(ServiceErrorCode::InvalidPath,
                           "channel path must name a file");
    return result;
}

runtime::ChannelId SdChannels::open(runtime::SessionId session,
                                   const std::string& path,
                                   runtime::ChannelMode mode) {
    const std::string resolved = resolve(path);
    ensureAvailable();
    const char* flags = FILE_READ;
    switch (mode) {
    case runtime::ChannelMode::Read: flags = FILE_READ; break;
    case runtime::ChannelMode::Write: flags = FILE_WRITE; break;
    case runtime::ChannelMode::Append: flags = FILE_APPEND; break;
    }
    fs::File file = filesystem_.open(resolved.c_str(), flags);
    if (!file)
        throw ServiceError(mode == runtime::ChannelMode::Read
                               ? ServiceErrorCode::NotFound
                               : ServiceErrorCode::IoError,
                           "channel: cannot open file: " + path);
    if (file.isDirectory()) {
        file.close();
        throw ServiceError(ServiceErrorCode::InvalidPath,
                           "channel path names a directory: " + path);
    }

    std::lock_guard lock(mutex_);
    while (nextId_ == 0 || channels_.find(nextId_) != channels_.end()) ++nextId_;
    const auto id = nextId_++;
    channels_.emplace(id, OpenChannel{session, mode, std::move(file)});
    return id;
}

SdChannels::OpenChannel& SdChannels::owned(runtime::SessionId session,
                                          runtime::ChannelId channel) {
    const auto found = channels_.find(channel);
    if (found == channels_.end() || found->second.session != session)
        throw ServiceError(ServiceErrorCode::InvalidHandle,
                           "channel is not open for this session");
    return found->second;
}

std::optional<std::string> SdChannels::readLine(runtime::SessionId session,
                                               runtime::ChannelId channel) {
    std::lock_guard lock(mutex_);
    OpenChannel& opened = owned(session, channel);
    if (opened.mode != runtime::ChannelMode::Read)
        throw ServiceError(ServiceErrorCode::InvalidHandle,
                           "channel is not open for reading");

    std::string line;
    while (opened.file.available()) {
        const int character = opened.file.read();
        if (character < 0)
            throw ServiceError(ServiceErrorCode::IoError, "channel read failed");
        if (character == '\n') return line;
        line.push_back(static_cast<char>(character));
    }
    if (!line.empty()) return line;
    return std::nullopt;
}

std::string SdChannels::readBytes(runtime::SessionId session,
                                  runtime::ChannelId channel, std::size_t count) {
    std::lock_guard lock(mutex_);
    OpenChannel& opened = owned(session, channel);
    if (opened.mode != runtime::ChannelMode::Read)
        throw ServiceError(ServiceErrorCode::InvalidHandle, "channel is not open for reading");
    std::string bytes(count, '\0');
    const std::size_t received = opened.file.read(
        reinterpret_cast<std::uint8_t*>(bytes.data()), count);
    if (received != count)
        throw ServiceError(ServiceErrorCode::IoError, "channel has fewer bytes than requested");
    return bytes;
}

void SdChannels::writeBytes(runtime::SessionId session, runtime::ChannelId channel,
                            std::string_view bytes) {
    std::lock_guard lock(mutex_);
    OpenChannel& opened = owned(session, channel);
    if (opened.mode == runtime::ChannelMode::Read)
        throw ServiceError(ServiceErrorCode::InvalidHandle, "channel is not open for writing");
    const auto written = opened.file.write(
        reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
    opened.file.flush();
    if (written != bytes.size())
        throw ServiceError(ServiceErrorCode::IoError, "channel write failed");
}

void SdChannels::seek(runtime::SessionId session, runtime::ChannelId channel,
                     std::uint64_t offset) {
    std::lock_guard lock(mutex_);
    OpenChannel& opened = owned(session, channel);
    if (opened.mode == runtime::ChannelMode::Append)
        throw ServiceError(ServiceErrorCode::InvalidHandle,
                           "append channels do not support seeking");
    if (offset > std::numeric_limits<std::uint32_t>::max() ||
        !opened.file.seek(static_cast<std::uint32_t>(offset)))
        throw ServiceError(ServiceErrorCode::IoError, "channel seek failed");
}

bool SdChannels::atEnd(runtime::SessionId session, runtime::ChannelId channel) {
    std::lock_guard lock(mutex_);
    OpenChannel& opened = owned(session, channel);
    if (opened.mode != runtime::ChannelMode::Read)
        throw ServiceError(ServiceErrorCode::InvalidHandle,
                           "channel is not open for reading");
    return opened.file.available() == 0;
}

void SdChannels::write(runtime::SessionId session, runtime::ChannelId channel,
                       const std::string& text) {
    std::lock_guard lock(mutex_);
    OpenChannel& opened = owned(session, channel);
    if (opened.mode == runtime::ChannelMode::Read)
        throw ServiceError(ServiceErrorCode::InvalidHandle,
                           "channel is not open for writing");
    const auto written = opened.file.write(
        reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
    opened.file.flush();
    if (written != text.size())
        throw ServiceError(ServiceErrorCode::IoError, "channel write failed");
}

void SdChannels::close(runtime::SessionId session, runtime::ChannelId channel) {
    std::lock_guard lock(mutex_);
    OpenChannel& opened = owned(session, channel);
    opened.file.close();
    channels_.erase(channel);
}

void SdChannels::closeSession(runtime::SessionId session) {
    std::lock_guard lock(mutex_);
    for (auto channel = channels_.begin(); channel != channels_.end();) {
        if (channel->second.session == session) {
            channel->second.file.close();
            channel = channels_.erase(channel);
        } else {
            ++channel;
        }
    }
}

std::vector<runtime::ChannelInfo> SdChannels::listOpen(runtime::SessionId session) {
    std::lock_guard lock(mutex_);
    std::vector<runtime::ChannelInfo> result;
    for (const auto& [id, channel] : channels_)
        if (channel.session == session) result.push_back({id, runtime::ChannelType::File});
    return result;
}

} // namespace ql32::platform::esp32
