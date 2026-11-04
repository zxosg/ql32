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
    throw ServiceError(ServiceErrorCode::InvalidPath,
                       "OPEN mode must be READ, WRITE, or APPEND");
}

} // namespace

ChannelRegistry::ChannelRegistry(std::shared_ptr<ChannelService> files,
                                 SerialWriter serialWriter,
                                 std::shared_ptr<HardwareChannelService> hardware)
    : files_(std::move(files)), serialWriter_(std::move(serialWriter)),
      hardware_(std::move(hardware)) {}

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
    if (opened.type != ChannelType::Hardware || !hardware_) unsupported("scalar input");
    return hardware_->readValue(session, opened.backend, reg);
}

void ChannelRegistry::writeValue(SessionId session, ChannelId channel, double value,
                                 std::optional<double> reg) {
    const Endpoint opened = endpoint(session, channel);
    if (opened.type != ChannelType::Hardware || !hardware_) unsupported("scalar output");
    hardware_->writeValue(session, opened.backend, value, reg);
}

std::size_t ChannelRegistry::readDeviceBytes(SessionId session, ChannelId channel,
                                             std::optional<double> reg,
                                             std::uint8_t* buffer,
                                             std::size_t capacity) {
    const Endpoint opened = endpoint(session, channel);
    if (opened.type != ChannelType::Hardware || !hardware_) unsupported("device byte input");
    return hardware_->readBytes(session, opened.backend, reg, buffer, capacity);
}

void ChannelRegistry::writeDeviceBytes(SessionId session, ChannelId channel,
                                       std::optional<double> reg,
                                       const std::uint8_t* buffer,
                                       std::size_t length) {
    const Endpoint opened = endpoint(session, channel);
    if (opened.type != ChannelType::Hardware || !hardware_) unsupported("device byte output");
    hardware_->writeBytes(session, opened.backend, reg, buffer, length);
}

void ChannelRegistry::transfer(SessionId session, ChannelId channel,
                               const std::uint8_t* tx, std::size_t txLength,
                               std::uint8_t* rx, std::size_t rxLength) {
    const Endpoint opened = endpoint(session, channel);
    if (opened.type != ChannelType::Hardware || !hardware_) unsupported("transfer");
    hardware_->transfer(session, opened.backend, tx, txLength, rx, rxLength);
}

std::optional<std::string> ChannelRegistry::readLine(SessionId session,
                                                     ChannelId channel) {
    const Endpoint opened = endpoint(session, channel);
    if (opened.type == ChannelType::File) return files_->readLine(session, opened.backend);
    if (opened.type == ChannelType::Hardware && hardware_)
        return hardware_->readLine(session, opened.backend);
    unsupported("line input");
}

bool ChannelRegistry::lineAvailable(SessionId session, ChannelId channel) {
    const Endpoint opened = endpoint(session, channel);
    return opened.type == ChannelType::Hardware && hardware_ &&
           hardware_->lineAvailable(session, opened.backend);
}

bool ChannelRegistry::isHardwareChannel(SessionId session, ChannelId channel) const {
    return endpoint(session, channel).type == ChannelType::Hardware;
}

std::string ChannelRegistry::readBytes(SessionId session, ChannelId channel,
                                       std::size_t count) {
    const Endpoint opened = endpoint(session, channel);
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
    if (opened.type != ChannelType::File) unsupported("seeking");
    files_->seek(session, opened.backend, offset);
}

bool ChannelRegistry::atEnd(SessionId session, ChannelId channel) {
    const Endpoint opened = endpoint(session, channel);
    if (opened.type != ChannelType::File) unsupported("EOF checks");
    return files_->atEnd(session, opened.backend);
}

void ChannelRegistry::write(SessionId session, ChannelId channel,
                            const std::string& text) {
    const Endpoint opened = endpoint(session, channel);
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

void ChannelRegistry::close(SessionId session, ChannelId channel) {
    const Endpoint opened = endpoint(session, channel);
    if (opened.type == ChannelType::File) files_->close(session, opened.backend);
    else if (opened.type == ChannelType::Hardware && hardware_)
        hardware_->close(session, opened.backend);
    std::lock_guard lock(mutex_);
    endpoints_.erase(channel);
}

void ChannelRegistry::closeSession(SessionId session) {
    if (files_) files_->closeSession(session);
    if (hardware_) hardware_->closeSession(session);
    std::lock_guard lock(mutex_);
    for (auto endpoint = endpoints_.begin(); endpoint != endpoints_.end();) {
        if (endpoint->second.session == session) endpoint = endpoints_.erase(endpoint);
        else ++endpoint;
    }
}

std::vector<ChannelInfo> ChannelRegistry::listOpen(SessionId session) {
    std::lock_guard lock(mutex_);
    std::vector<ChannelInfo> result;
    for (const auto& [handle, endpoint] : endpoints_)
        if (endpoint.session == session) result.push_back({handle, endpoint.type});
    return result;
}

} // namespace ql32::runtime
