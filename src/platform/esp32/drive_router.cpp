#include "ql32/platform/esp32/drive_router.hpp"

#include <algorithm>
#include <cctype>

namespace ql32::platform::esp32 {
namespace {

std::string driveName(const std::string& path) {
    const auto colon = path.find(':');
    if (colon == std::string::npos) return "SDC";
    std::string drive = path.substr(0, colon);
    std::transform(drive.begin(), drive.end(), drive.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return drive;
}

bool isFfs(const std::string& path) {
    const auto drive = driveName(path);
    if (drive == "FFS") return true;
    if (drive == "SDC") return false;
    throw runtime::ServiceError(runtime::ServiceErrorCode::Unavailable,
        "storage drive is not available: " + drive);
}

} // namespace

runtime::StorageService& DriveRouterStorage::forPath(const std::string& path) const {
    return isFfs(path) ? ffs_ : sdc_;
}

std::string DriveRouterStorage::readText(const std::string& path) {
    return forPath(path).readText(path);
}

void DriveRouterStorage::writeText(const std::string& path, const std::string& contents) {
    forPath(path).writeText(path, contents);
}

void DriveRouterStorage::writeTextStream(
        const std::string& path, const runtime::TextStreamSource& source) {
    forPath(path).writeTextStream(path, source);
}

std::vector<runtime::FileEntry> DriveRouterStorage::list(const std::string& path) {
    return forPath(path).list(path);
}

bool DriveRouterStorage::exists(const std::string& path) {
    return forPath(path).exists(path);
}

std::uintmax_t DriveRouterStorage::size(const std::string& path) {
    return forPath(path).size(path);
}

void DriveRouterStorage::rename(const std::string& path, const std::string& newPath) {
    auto& source = forPath(path);
    auto& destination = forPath(newPath);
    if (&source != &destination)
        throw runtime::ServiceError(runtime::ServiceErrorCode::InvalidPath,
            "storage: cannot rename across drives");
    source.rename(path, newPath);
}

void DriveRouterStorage::remove(const std::string& path) {
    forPath(path).remove(path);
}

void DriveRouterStorage::removeDirectory(const std::string& path) {
    forPath(path).removeDirectory(path);
}

void DriveRouterStorage::format(const std::string& path) {
    forPath(path).format(path);
}

runtime::ChannelService& DriveRouterChannels::forPath(const std::string& path) const {
    return isFfs(path) ? ffs_ : sdc_;
}

runtime::ChannelId DriveRouterChannels::remember(runtime::SessionId session,
        runtime::ChannelService& service, runtime::ChannelId channel) {
    std::lock_guard lock(mutex_);
    while (nextChannel_ == 0 || channels_.find(nextChannel_) != channels_.end())
        ++nextChannel_;
    const auto result = nextChannel_++;
    channels_.emplace(result, RoutedChannel{session, &service, channel});
    return result;
}

DriveRouterChannels::RoutedChannel DriveRouterChannels::lookup(
        runtime::SessionId session, runtime::ChannelId channel) const {
    std::lock_guard lock(mutex_);
    const auto found = channels_.find(channel);
    if (found == channels_.end() || found->second.session != session)
        throw runtime::ServiceError(runtime::ServiceErrorCode::Unavailable,
            "channel is not open in this session");
    return found->second;
}

runtime::ChannelId DriveRouterChannels::open(runtime::SessionId session,
        const std::string& path, runtime::ChannelMode mode) {
    auto& service = forPath(path);
    return remember(session, service, service.open(session, path, mode));
}

std::optional<std::string> DriveRouterChannels::readLine(runtime::SessionId session,
        runtime::ChannelId channel) {
    const auto routed = lookup(session, channel);
    return routed.service->readLine(session, routed.channel);
}

std::string DriveRouterChannels::readBytes(runtime::SessionId session,
        runtime::ChannelId channel, std::size_t count) {
    const auto routed = lookup(session, channel);
    return routed.service->readBytes(session, routed.channel, count);
}

void DriveRouterChannels::writeBytes(runtime::SessionId session,
        runtime::ChannelId channel, std::string_view bytes) {
    const auto routed = lookup(session, channel);
    routed.service->writeBytes(session, routed.channel, bytes);
}

void DriveRouterChannels::seek(runtime::SessionId session, runtime::ChannelId channel,
        std::uint64_t offset) {
    const auto routed = lookup(session, channel);
    routed.service->seek(session, routed.channel, offset);
}

bool DriveRouterChannels::atEnd(runtime::SessionId session, runtime::ChannelId channel) {
    const auto routed = lookup(session, channel);
    return routed.service->atEnd(session, routed.channel);
}

void DriveRouterChannels::write(runtime::SessionId session, runtime::ChannelId channel,
        const std::string& text) {
    const auto routed = lookup(session, channel);
    routed.service->write(session, routed.channel, text);
}

void DriveRouterChannels::close(runtime::SessionId session, runtime::ChannelId channel) {
    const auto routed = lookup(session, channel);
    routed.service->close(session, routed.channel);
    std::lock_guard lock(mutex_);
    channels_.erase(channel);
}

void DriveRouterChannels::closeSession(runtime::SessionId session) {
    ffs_.closeSession(session);
    sdc_.closeSession(session);
    std::lock_guard lock(mutex_);
    for (auto item = channels_.begin(); item != channels_.end();) {
        if (item->second.session == session) item = channels_.erase(item);
        else ++item;
    }
}

std::vector<runtime::ChannelInfo> DriveRouterChannels::listOpen(
        runtime::SessionId session) {
    std::vector<runtime::ChannelInfo> result;
    for (const auto& [service, entries] : {
            std::pair<runtime::ChannelService*, std::vector<runtime::ChannelInfo>>{
                &ffs_, ffs_.listOpen(session)},
            std::pair<runtime::ChannelService*, std::vector<runtime::ChannelInfo>>{
                &sdc_, sdc_.listOpen(session)}}) {
        for (const auto& entry : entries) {
            std::lock_guard lock(mutex_);
            const auto routed = std::find_if(channels_.begin(), channels_.end(),
                [&](const auto& item) {
                    return item.second.session == session &&
                        item.second.service == service &&
                        item.second.channel == entry.handle;
                });
            if (routed != channels_.end())
                result.push_back({routed->first, entry.type});
        }
    }
    return result;
}

} // namespace ql32::platform::esp32
