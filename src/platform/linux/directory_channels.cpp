#include "ql32/platform/linux/directory_channels.hpp"

#include <cerrno>
#include <system_error>
#include <utility>

namespace ql32::platform::linux_host {
namespace {

using runtime::ServiceError;
using runtime::ServiceErrorCode;

[[noreturn]] void fail(const std::filesystem::path& path,
                       const std::error_code& error) {
    ServiceErrorCode code = ServiceErrorCode::IoError;
    if (error == std::errc::no_such_file_or_directory) code = ServiceErrorCode::NotFound;
    else if (error == std::errc::permission_denied) code = ServiceErrorCode::PermissionDenied;
    throw ServiceError(code, "channel: " + path.string() + ": " + error.message());
}

bool isWithin(const std::filesystem::path& root,
              const std::filesystem::path& candidate) {
    auto rootPart = root.begin();
    auto candidatePart = candidate.begin();
    for (; rootPart != root.end(); ++rootPart, ++candidatePart) {
        if (candidatePart == candidate.end() || *rootPart != *candidatePart) return false;
    }
    return true;
}

} // namespace

DirectoryChannels::DirectoryChannels(std::filesystem::path root) {
    std::error_code error;
    root_ = std::filesystem::canonical(std::move(root), error);
    if (error || !std::filesystem::is_directory(root_))
        throw ServiceError(ServiceErrorCode::InvalidPath,
                           "channel root must be an existing directory");
}

std::filesystem::path DirectoryChannels::resolve(const std::string& path,
                                                 runtime::ChannelMode mode) const {
    const std::filesystem::path relative(path);
    if (relative.empty() || relative.is_absolute())
        throw ServiceError(ServiceErrorCode::InvalidPath,
                           "channel path must be relative");
    for (const auto& component : relative) {
        if (component == "..")
            throw ServiceError(ServiceErrorCode::InvalidPath,
                               "channel path cannot escape the configured root");
    }

    const auto candidate = root_ / relative;
    std::error_code error;
    const auto resolved = mode == runtime::ChannelMode::Read
        ? std::filesystem::canonical(candidate, error)
        : std::filesystem::weakly_canonical(candidate, error);
    if (error) fail(candidate, error);
    if (!isWithin(root_, resolved))
        throw ServiceError(ServiceErrorCode::InvalidPath,
                           "channel path resolves outside the configured root");
    return resolved;
}

runtime::ChannelId DirectoryChannels::open(runtime::SessionId session,
                                           const std::string& path,
                                           runtime::ChannelMode mode) {
    const auto resolved = resolve(path, mode);
    std::ios::openmode flags = std::ios::binary;
    switch (mode) {
    case runtime::ChannelMode::Read: flags |= std::ios::in; break;
    case runtime::ChannelMode::Write: flags |= std::ios::out | std::ios::trunc; break;
    case runtime::ChannelMode::Append: flags |= std::ios::out | std::ios::app; break;
    }

    OpenChannel opened{session, mode, {}};
    opened.file.open(resolved, flags);
    if (!opened.file) {
        std::error_code error(errno, std::generic_category());
        fail(resolved, error);
    }

    std::lock_guard lock(mutex_);
    while (nextId_ == 0 || channels_.contains(nextId_)) ++nextId_;
    const auto id = nextId_++;
    channels_.emplace(id, std::move(opened));
    return id;
}

DirectoryChannels::OpenChannel& DirectoryChannels::owned(
    runtime::SessionId session, runtime::ChannelId channel) {
    const auto found = channels_.find(channel);
    if (found == channels_.end() || found->second.session != session)
        throw ServiceError(ServiceErrorCode::InvalidHandle,
                           "channel is not open for this session");
    return found->second;
}

std::optional<std::string> DirectoryChannels::readLine(runtime::SessionId session,
                                                       runtime::ChannelId channel) {
    std::lock_guard lock(mutex_);
    OpenChannel& opened = owned(session, channel);
    if (opened.mode != runtime::ChannelMode::Read)
        throw ServiceError(ServiceErrorCode::InvalidHandle,
                           "channel is not open for reading");
    std::string line;
    if (std::getline(opened.file, line)) return line;
    if (opened.file.eof()) return std::nullopt;
    throw ServiceError(ServiceErrorCode::IoError, "channel read failed");
}

bool DirectoryChannels::atEnd(runtime::SessionId session, runtime::ChannelId channel) {
    std::lock_guard lock(mutex_);
    OpenChannel& opened = owned(session, channel);
    if (opened.mode != runtime::ChannelMode::Read)
        throw ServiceError(ServiceErrorCode::InvalidHandle,
                           "channel is not open for reading");
    const auto next = opened.file.peek();
    if (next == std::char_traits<char>::eof() && !opened.file.eof())
        throw ServiceError(ServiceErrorCode::IoError, "channel read failed");
    return next == std::char_traits<char>::eof();
}

void DirectoryChannels::write(runtime::SessionId session, runtime::ChannelId channel,
                              const std::string& text) {
    std::lock_guard lock(mutex_);
    OpenChannel& opened = owned(session, channel);
    if (opened.mode == runtime::ChannelMode::Read)
        throw ServiceError(ServiceErrorCode::InvalidHandle,
                           "channel is not open for writing");
    opened.file.write(text.data(), static_cast<std::streamsize>(text.size()));
    opened.file.flush();
    if (!opened.file)
        throw ServiceError(ServiceErrorCode::IoError, "channel write failed");
}

void DirectoryChannels::close(runtime::SessionId session, runtime::ChannelId channel) {
    std::lock_guard lock(mutex_);
    OpenChannel& opened = owned(session, channel);
    opened.file.clear();
    opened.file.close();
    if (opened.file.fail())
        throw ServiceError(ServiceErrorCode::IoError, "channel close failed");
    channels_.erase(channel);
}

void DirectoryChannels::closeSession(runtime::SessionId session) {
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

} // namespace ql32::platform::linux_host
