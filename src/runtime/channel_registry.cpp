#include "ql32/runtime/channel.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <utility>

namespace ql32::runtime {
namespace {

[[noreturn]] void unsupported(const char* operation) {
    throw ServiceError(ServiceErrorCode::Unavailable,
                       std::string("channel endpoint does not support ") + operation);
}

ChannelMode parseMode(std::string mode) {
    std::transform(mode.begin(), mode.end(), mode.begin(), [](unsigned char character) {
        return static_cast<char>(std::toupper(character));
    });
    if (mode == "READ") return ChannelMode::Read;
    if (mode == "WRITE") return ChannelMode::Write;
    if (mode == "APPEND") return ChannelMode::Append;
    if (mode == "UPDATE") return ChannelMode::Update;
    throw ServiceError(ServiceErrorCode::InvalidPath,
                       "OPEN mode must be READ, WRITE, APPEND, or UPDATE");
}

} // namespace

ChannelRegistry::ChannelRegistry(std::shared_ptr<ChannelService> files,
                                 SerialWriter serialWriter,
                                 std::shared_ptr<HardwareChannelService> hardware,
                                 bool touchAvailable)
    : files_(std::move(files)), serialWriter_(std::move(serialWriter)),
      hardware_(std::move(hardware)), touchAvailable_(touchAvailable) {}

ChannelRegistry::~ChannelRegistry() {
    std::vector<SessionId> sessions;
    {
        std::lock_guard lock(mutex_);
        sessions.reserve(endpoints_.size());
        for (const auto& [_, endpoint] : endpoints_)
            if (std::find(sessions.begin(), sessions.end(), endpoint.session) == sessions.end())
                sessions.push_back(endpoint.session);
    }
    for (const SessionId session : sessions) {
        try { closeSession(session); } catch (...) {}
    }
}

ChannelId ChannelRegistry::allocate(SessionId session, ChannelType type,
                                    ChannelId backend) {
    std::lock_guard lock(mutex_);
    if (nextId_ == 0)
        throw std::overflow_error("channel handle IDs exhausted");
    const ChannelId id = nextId_++;
    endpoints_.emplace(id, Endpoint{session, type, backend});
    return id;
}

ChannelRegistry::Endpoint ChannelRegistry::endpoint(SessionId session,
                                                    ChannelId channel) const {
    std::lock_guard lock(mutex_);
    const auto found = endpoints_.find(channel);
    if (found == endpoints_.end() || found->second.session != session)
        throw ServiceError(ServiceErrorCode::InvalidHandle,
                           "channel is not open for this session");
    return found->second;
}

ChannelId ChannelRegistry::open(SessionId session, const std::string& path,
                               ChannelMode mode) {
    if (!files_)
        throw ServiceError(ServiceErrorCode::Unavailable,
                           "file channel service unavailable");
    const ChannelId backend = files_->open(session, path, mode);
    try {
        return allocate(session, ChannelType::File, backend);
    } catch (...) {
        try { files_->close(session, backend); } catch (...) {}
        throw;
    }
}

ChannelId ChannelRegistry::openDevice(SessionId session, ChannelDevice device,
                                      const std::vector<std::string>& parameters) {
    switch (device) {
    case ChannelDevice::File:
        if (parameters.size() != 2)
            throw ServiceError(ServiceErrorCode::InvalidPath,
                               "OPEN file requires a path and mode");
        return open(session, parameters[0], parseMode(parameters[1]));
    case ChannelDevice::Serial:
        if (!parameters.empty())
            throw ServiceError(ServiceErrorCode::InvalidPath,
                               "OPEN serial takes no parameters yet");
        if (!serialWriter_)
            throw ServiceError(ServiceErrorCode::Unavailable,
                               "serial output endpoint unavailable");
        return allocate(session, ChannelType::Serial);
    case ChannelDevice::Null:
        if (!parameters.empty())
            throw ServiceError(ServiceErrorCode::InvalidPath,
                               "OPEN null takes no parameters");
        return allocate(session, ChannelType::Null);
    case ChannelDevice::Window:
        if (!parameters.empty())
            throw ServiceError(ServiceErrorCode::InvalidPath,
                               "OPEN window takes no parameters");
        return allocate(session, ChannelType::Window);
    case ChannelDevice::Touch:
        if (!touchAvailable_)
            throw ServiceError(ServiceErrorCode::Unavailable,
                               "screen touch input is unavailable");
        if (!parameters.empty())
            throw ServiceError(ServiceErrorCode::InvalidPath,
                               "OPEN touch takes no parameters");
        {
            const ChannelId handle = allocate(session, ChannelType::Touch);
            std::lock_guard lock(mutex_);
            touchEvents_.try_emplace(handle);
            return handle;
        }
    default:
        std::vector<DeviceArgument> typed;
        typed.reserve(parameters.size());
        for (const auto& parameter : parameters) typed.emplace_back(parameter);
        return openTypedDevice(session, device, typed);
    }
    throw ServiceError(ServiceErrorCode::InvalidPath, "unknown channel device");
}

ChannelId ChannelRegistry::openTypedDevice(SessionId session, ChannelDevice device,
                                           const std::vector<DeviceArgument>& parameters) {
    if (device == ChannelDevice::Touch)
        return openDevice(session, device, {});
    if (device == ChannelDevice::File || device == ChannelDevice::Null ||
        device == ChannelDevice::Window ||
        (device == ChannelDevice::Serial && parameters.empty())) {
        std::vector<std::string> strings;
        strings.reserve(parameters.size());
        for (const auto& parameter : parameters) {
            if (const auto* text = std::get_if<std::string>(&parameter)) strings.push_back(*text);
            else strings.push_back(std::to_string(std::get<double>(parameter)));
        }
        return openDevice(session, device, strings);
    }
    if (!hardware_)
        throw ServiceError(ServiceErrorCode::Unavailable,
                           "hardware channel service unavailable");
    const ChannelId backend = hardware_->open(session, device, parameters);
    try {
        return allocate(session, ChannelType::Hardware, backend);
    } catch (...) {
        try { hardware_->close(session, backend); } catch (...) {}
        throw;
    }
}

double ChannelRegistry::readValue(SessionId session, ChannelId channel,
                                  std::optional<double> reg) {
    const Endpoint opened = endpoint(session, channel);
    if (opened.directory) throw Error(ErrorCode::UnsupportedOperation, "operation unsupported on directory channel");
    if (opened.type != ChannelType::Hardware || !hardware_) unsupported("scalar input");
    return hardware_->readValue(session, opened.backend, reg);
}

void ChannelRegistry::writeValue(SessionId session, ChannelId channel, double value,
                                 std::optional<double> reg) {
    const Endpoint opened = endpoint(session, channel);
    if (opened.directory) throw Error(ErrorCode::UnsupportedOperation, "operation unsupported on directory channel");
    if (opened.type != ChannelType::Hardware || !hardware_) unsupported("scalar output");
    hardware_->writeValue(session, opened.backend, value, reg);
}

std::size_t ChannelRegistry::readDeviceBytes(SessionId session, ChannelId channel,
                                             std::optional<double> reg,
                                             std::uint8_t* buffer,
                                             std::size_t capacity) {
    const Endpoint opened = endpoint(session, channel);
    if (opened.directory) throw Error(ErrorCode::UnsupportedOperation, "operation unsupported on directory channel");
    if (opened.type != ChannelType::Hardware || !hardware_) unsupported("device byte input");
    return hardware_->readBytes(session, opened.backend, reg, buffer, capacity);
}

void ChannelRegistry::writeDeviceBytes(SessionId session, ChannelId channel,
                                       std::optional<double> reg,
                                       const std::uint8_t* buffer,
                                       std::size_t length) {
    const Endpoint opened = endpoint(session, channel);
    if (opened.directory) throw Error(ErrorCode::UnsupportedOperation, "operation unsupported on directory channel");
    if (opened.type != ChannelType::Hardware || !hardware_) unsupported("device byte output");
    hardware_->writeBytes(session, opened.backend, reg, buffer, length);
}

void ChannelRegistry::transfer(SessionId session, ChannelId channel,
                               const std::uint8_t* tx, std::size_t txLength,
                               std::uint8_t* rx, std::size_t rxLength) {
    const Endpoint opened = endpoint(session, channel);
    if (opened.directory) throw Error(ErrorCode::UnsupportedOperation, "operation unsupported on directory channel");
    if (opened.type != ChannelType::Hardware || !hardware_) unsupported("transfer");
    hardware_->transfer(session, opened.backend, tx, txLength, rx, rxLength);
}

std::optional<std::string> ChannelRegistry::readLine(SessionId session,
                                                     ChannelId channel) {
    const Endpoint opened = endpoint(session, channel);
    if (opened.type == ChannelType::Directory)
        throw Error(ErrorCode::UnsupportedOperation, "directory input requires READ$");
    if (opened.type == ChannelType::File) return files_->readLine(session, opened.backend);
    if (opened.type == ChannelType::Hardware && hardware_)
        return hardware_->readLine(session, opened.backend);
    if (opened.type == ChannelType::Touch) {
        std::lock_guard lock(mutex_);
        auto found = touchEvents_.find(channel);
        if (found == touchEvents_.end() || found->second.empty()) return std::nullopt;
        const TouchEvent event = found->second.front();
        found->second.pop_front();
        return std::to_string(static_cast<unsigned>(event.type)) + "," +
            std::to_string(event.x) + "," + std::to_string(event.y) + "," +
            std::to_string(event.window);
    }
    unsupported("line input");
}

bool ChannelRegistry::lineAvailable(SessionId session, ChannelId channel) {
    const Endpoint opened = endpoint(session, channel);
    if (opened.type == ChannelType::Touch) {
        std::lock_guard lock(mutex_);
        const auto found = touchEvents_.find(channel);
        return found != touchEvents_.end() && !found->second.empty();
    }
    return opened.type == ChannelType::Hardware && hardware_ &&
           hardware_->lineAvailable(session, opened.backend);
}

bool ChannelRegistry::isHardwareChannel(SessionId session, ChannelId channel) const {
    const auto type = endpoint(session, channel).type;
    return type == ChannelType::Hardware || type == ChannelType::Touch;
}

std::string ChannelRegistry::readBytes(SessionId session, ChannelId channel,
                                       std::size_t count) {
    const Endpoint opened = endpoint(session, channel);
    if (opened.directory) throw Error(ErrorCode::UnsupportedOperation, "operation unsupported on directory channel");
    if (opened.type == ChannelType::File) return files_->readBytes(session, opened.backend, count);
    if (opened.type == ChannelType::Hardware && hardware_) {
        std::string bytes(count, '\0');
        const auto size = hardware_->readBytes(session, opened.backend, std::nullopt,
            reinterpret_cast<std::uint8_t*>(bytes.data()), count);
        bytes.resize(size);
        return bytes;
    }
    unsupported("byte input");
}

void ChannelRegistry::writeBytes(SessionId session, ChannelId channel,
                                 std::string_view bytes) {
    const Endpoint opened = endpoint(session, channel);
    if (opened.directory) throw Error(ErrorCode::UnsupportedOperation, "operation unsupported on directory channel");
    switch (opened.type) {
    case ChannelType::File:
        files_->writeBytes(session, opened.backend, bytes);
        return;
    case ChannelType::Serial:
        serialWriter_(bytes);
        return;
    case ChannelType::Null:
        return;
    case ChannelType::Hardware:
        hardware_->writeText(session, opened.backend, bytes);
        return;
    default:
        unsupported("byte output");
    }
}

void ChannelRegistry::seek(SessionId session, ChannelId channel,
                           std::uint64_t offset) {
    const Endpoint opened = endpoint(session, channel);
    if (opened.directory) throw Error(ErrorCode::UnsupportedOperation, "operation unsupported on directory channel");
    if (opened.type != ChannelType::File) unsupported("seeking");
    files_->seek(session, opened.backend, offset);
}

std::uint64_t ChannelRegistry::position(SessionId session, ChannelId channel) {
    const Endpoint opened = endpoint(session, channel);
    if (opened.type != ChannelType::File) unsupported("position queries");
    return files_->position(session, opened.backend);
}

std::uint64_t ChannelRegistry::length(SessionId session, ChannelId channel) {
    const Endpoint opened = endpoint(session, channel);
    if (opened.type != ChannelType::File) unsupported("length queries");
    return files_->length(session, opened.backend);
}

bool ChannelRegistry::atEnd(SessionId session, ChannelId channel) {
    const Endpoint opened = endpoint(session, channel);
    if (opened.directory) return opened.directory->atEnd();
    if (opened.type != ChannelType::File) unsupported("EOF checks");
    return files_->atEnd(session, opened.backend);
}

void ChannelRegistry::write(SessionId session, ChannelId channel,
                            const std::string& text) {
    const Endpoint opened = endpoint(session, channel);
    if (opened.directory) throw Error(ErrorCode::UnsupportedOperation, "operation unsupported on directory channel");
    switch (opened.type) {
    case ChannelType::File:
        files_->write(session, opened.backend, text);
        return;
    case ChannelType::Serial:
        serialWriter_(text);
        return;
    case ChannelType::Null:
        return;
    case ChannelType::Hardware:
        hardware_->writeText(session, opened.backend, text);
        return;
    default:
        unsupported("text output");
    }
}

std::size_t ChannelRegistry::writeNonBlocking(SessionId session, ChannelId channel,
                                              const std::string& text) {
    const Endpoint opened = endpoint(session, channel);
    if (opened.type == ChannelType::Serial && serialTryWriter_) {
        const std::size_t accepted = std::min(serialTryWriter_(text), text.size());
        return text.size() - accepted;
    }
    write(session, channel, text);
    return 0;
}

void ChannelRegistry::close(SessionId session, ChannelId channel) {
    const Endpoint opened = endpoint(session, channel);
    if (opened.type == ChannelType::File) files_->close(session, opened.backend);
    else if (opened.type == ChannelType::Hardware && hardware_)
        hardware_->close(session, opened.backend);
    std::lock_guard lock(mutex_);
    endpoints_.erase(channel);
    touchEvents_.erase(channel);
}

void ChannelRegistry::closeSession(SessionId session) {
    cancelPendingOperations(session);
    if (files_) files_->closeSession(session);
    if (hardware_) hardware_->closeSession(session);
    std::lock_guard lock(mutex_);
    for (auto endpoint = endpoints_.begin(); endpoint != endpoints_.end();) {
        if (endpoint->second.session == session) endpoint = endpoints_.erase(endpoint);
        else ++endpoint;
    }
    for (auto eventQueue = touchEvents_.begin(); eventQueue != touchEvents_.end();) {
        if (endpoints_.find(eventQueue->first) == endpoints_.end())
            eventQueue = touchEvents_.erase(eventQueue);
        else ++eventQueue;
    }
}

std::vector<ChannelInfo> ChannelRegistry::listOpen(SessionId session) {
    std::lock_guard lock(mutex_);
    std::vector<ChannelInfo> result;
    for (const auto& [handle, endpoint] : endpoints_)
        if (endpoint.session == session) result.push_back({handle, endpoint.type});
    return result;
}

void ChannelRegistry::publishTouch(SessionId session, const TouchEvent& event) {
    std::lock_guard lock(mutex_);
    for (auto& [handle, endpoint] : endpoints_) {
        if (endpoint.session != session || endpoint.type != ChannelType::Touch) continue;
        auto& events = touchEvents_[handle];
        // Bound per-channel memory. Pointer motion is lossy by nature; retain
        // the newest position while preserving edge events whenever possible.
        constexpr std::size_t maximumQueuedEvents = 32;
        if (events.size() >= maximumQueuedEvents) {
            if (event.type == TouchEventType::Move) {
                const auto lastMove = std::find_if(events.rbegin(), events.rend(),
                    [](const TouchEvent& queued) {
                        return queued.type == TouchEventType::Move;
                    });
                if (lastMove != events.rend()) {
                    *lastMove = event;
                    continue;
                }
            }
            const auto move = std::find_if(events.begin(), events.end(),
                [](const TouchEvent& queued) {
                    return queued.type == TouchEventType::Move;
                });
            if (move != events.end()) events.erase(move);
            else if (event.type == TouchEventType::Move) continue;
            else events.pop_front();
        }
        events.push_back(event);
    }
}

void ChannelRegistry::directoryLimits(DirectoryLimits limits) {
    std::lock_guard lock(mutex_);
    if (!endpoints_.empty() || !pendingDirectories_.empty())
        throw Error(ErrorCode::InvalidArgument, "configure directory limits before opening channels");
    directoryLimits_ = limits;
}
std::optional<ChannelId> ChannelRegistry::pollDirectoryOpen(SessionId session,
        const std::string& path, const std::string& pattern, std::uint32_t options) {
    // Scheduler serializes service jobs; no filesystem call is made with the
    // registry mutex held. Each pending job is owned by exactly one session.
    if (!storage_) throw Error(ErrorCode::ServiceUnavailable, "directory storage unavailable");
    auto job = pendingDirectories_.find(session);
    if (job == pendingDirectories_.end()) {
        std::size_t channels = 0, localBytes = 0, systemBytes = 0;
        for (const auto& item : endpoints_) {
            if (!item.second.directory) continue;
            const auto bytes = item.second.directory->snapshotBytes();
            systemBytes += bytes;
            if (item.second.session == session) { ++channels; localBytes += bytes; }
        }
        for (const auto& item : pendingDirectories_) systemBytes += item.second->snapshotBytes();
        if (channels >= directoryLimits_.maxChannels)
            throw Error(ErrorCode::TaskLimit, "directory channel limit reached");
        const std::size_t bytes = (options & 126u) ? directoryLimits_.snapshotBytes : 0;
        if (localBytes + bytes > directoryLimits_.perSessionBytes || systemBytes + bytes > directoryLimits_.systemBytes)
            throw Error(ErrorCode::TaskLimit, "directory snapshot memory limit reached");
        if (path.size() > 1023) throw Error(ErrorCode::InvalidPath, "directory path too long");
        // Validate masks/patterns before touching storage or allocating a snapshot.
        const auto sort = options & 126u;
        if ((options & ~127u) || (sort && (sort & (sort - 1))) || pattern.size() > 255 || pattern.find_first_of("/\\:") != std::string::npos)
            throw Error(ErrorCode::InvalidArgument, "invalid directory options or wildcard");
        auto cursor = storage_->openDirectory(path);
        auto listing = std::make_shared<DirectoryStream>(std::move(cursor), pattern, options, directoryLimits_);
        job = pendingDirectories_.emplace(session, std::move(listing)).first;
    }
    try {
        if (!job->second->prepare()) return std::nullopt;
        const auto handle = allocate(session, ChannelType::Directory);
        endpoints_.at(handle).directory = job->second;
        pendingDirectories_.erase(job);
        return handle;
    } catch (...) { pendingDirectories_.erase(session); throw; }
}
void ChannelRegistry::cancelPendingOperations(SessionId session) noexcept { pendingDirectories_.erase(session); }
bool ChannelRegistry::isDirectoryChannel(SessionId session, ChannelId channel) const {
    return endpoint(session, channel).type == ChannelType::Directory;
}
bool ChannelRegistry::directoryReady(SessionId session, ChannelId channel) {
    const auto opened = endpoint(session, channel);
    if (!opened.directory) throw Error(ErrorCode::UnsupportedOperation, "not a directory channel");
    return opened.directory->ready();
}
std::string_view ChannelRegistry::directoryName(SessionId session, ChannelId channel) {
    const auto opened = endpoint(session, channel);
    if (!opened.directory) throw Error(ErrorCode::UnsupportedOperation, "not a directory channel");
    return opened.directory->nextName();
}
void ChannelRegistry::commitDirectoryRead(SessionId session, ChannelId channel) noexcept {
    const auto found = endpoints_.find(channel);
    if (found != endpoints_.end() && found->second.session == session && found->second.directory)
        found->second.directory->commitRead();
}
double ChannelRegistry::directoryInfo(SessionId session, ChannelId channel, unsigned selector, bool single) {
    const auto opened = endpoint(session, channel);
    if (!opened.directory) throw Error(ErrorCode::UnsupportedOperation, "not a directory channel");
    return opened.directory->info(selector, single);
}
std::string ChannelRegistry::directoryInfoText(SessionId session, ChannelId channel, unsigned selector) {
    const auto opened = endpoint(session, channel);
    if (!opened.directory) throw Error(ErrorCode::UnsupportedOperation, "not a directory channel");
    return opened.directory->infoText(selector);
}

} // namespace ql32::runtime
