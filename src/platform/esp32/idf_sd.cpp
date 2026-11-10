#include "ql32/platform/esp32/idf_sd.hpp"

#include <dirent.h>
#include <cstring>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

extern "C" {
#include <driver/spi_master.h>
#include <esp_err.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>
}

#include <algorithm>
#include <array>
#include <cctype>
#include <limits>
#include <utility>

namespace ql32::platform::esp32 {
namespace {

using runtime::ServiceError;
using runtime::ServiceErrorCode;

std::string errorText(const char* operation) {
    return std::string(operation) + ": " + std::strerror(errno);
}

std::string joinPath(const std::string& root, const std::string& relative) {
    if (relative.empty() || relative == "/") return root;
    return root + relative;
}

std::string normalizePath(const std::string& path, bool allowRoot,
                          const char* subject) {
    std::string relative = path;
    const auto colon = relative.find(':');
    if (colon != std::string::npos) {
        std::string drive = relative.substr(0, colon);
        std::transform(drive.begin(), drive.end(), drive.begin(),
            [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        if (drive != "SDC")
            throw ServiceError(ServiceErrorCode::Unavailable,
                               std::string(subject) + " drive is not available: " + drive);
        relative.erase(0, colon + 1);
        if (!relative.empty() && relative.front() == '/') relative.erase(relative.begin());
    }
    if (relative.find('\0') != std::string::npos || relative.find('\\') != std::string::npos ||
        (!relative.empty() && relative.front() == '/'))
        throw ServiceError(ServiceErrorCode::InvalidPath,
                           std::string(subject) + " path must be relative to the SD root");

    std::string result;
    std::size_t start = 0;
    while (start <= relative.size()) {
        const auto end = relative.find('/', start);
        const auto length = (end == std::string::npos ? relative.size() : end) - start;
        const std::string component = relative.substr(start, length);
        if (component == "..")
            throw ServiceError(ServiceErrorCode::InvalidPath,
                               std::string(subject) + " path cannot escape the SD root");
        if (!component.empty() && component != ".") {
            result.push_back('/');
            result += component;
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    if (result.empty() && !allowRoot)
        throw ServiceError(ServiceErrorCode::InvalidPath,
                           std::string(subject) + " file path must not be empty");
    return result.empty() ? "/" : result;
}

class IdfSdReadHandle final : public runtime::StorageReadHandle {
public:
    IdfSdReadHandle(int descriptor, std::uint64_t size)
        : descriptor_(descriptor), size_(size) {}
    ~IdfSdReadHandle() override {
        if (descriptor_ >= 0) ::close(descriptor_);
    }

    std::uint64_t size() const noexcept override { return size_; }

    std::size_t readAt(std::uint64_t offset, std::uint8_t* destination,
                       std::size_t capacity) override {
        if (offset >= size_ || capacity == 0) return 0;
        if (offset > static_cast<std::uint64_t>(std::numeric_limits<off_t>::max()) ||
            ::lseek(descriptor_, static_cast<off_t>(offset), SEEK_SET) < 0)
            throw ServiceError(ServiceErrorCode::IoError, errorText("storage seek"));
        const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(
            capacity, size_ - offset));
        std::size_t total = 0;
        while (total < count) {
            const auto result = ::read(descriptor_, destination + total, count - total);
            if (result < 0)
                throw ServiceError(ServiceErrorCode::IoError, errorText("storage read"));
            if (result == 0) break;
            total += static_cast<std::size_t>(result);
        }
        return total;
    }

private:
    int descriptor_ = -1;
    std::uint64_t size_ = 0;
};

} // namespace

IdfSdCard::~IdfSdCard() {
    unmount();
}

bool IdfSdCard::mount(SdSpiPins pins, std::string mountPoint) {
    unmount();
    error_.clear();
    mountPoint_ = std::move(mountPoint);

    spi_bus_config_t bus{};
    bus.mosi_io_num = pins.mosi;
    bus.miso_io_num = pins.miso;
    bus.sclk_io_num = pins.clock;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = 2048;
    esp_err_t result = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH1);
    if (result != ESP_OK) {
        error_ = std::string("SPI2 bus initialization failed: ") + esp_err_to_name(result);
        return false;
    }
    busInitialized_ = true;

    esp_vfs_fat_sdmmc_mount_config_t mount{};
    mount.format_if_mount_failed = false;
    mount.max_files = 8;
    mount.allocation_unit_size = 16 * 1024;

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    sdspi_device_config_t device{};
    device.host_id = SPI2_HOST;
    device.gpio_cs = static_cast<gpio_num_t>(pins.chipSelect);
    device.gpio_cd = SDSPI_SLOT_NO_CD;
    device.gpio_wp = SDSPI_SLOT_NO_WP;
    device.gpio_int = GPIO_NUM_NC;

    sdmmc_card_t* card = nullptr;
    result = esp_vfs_fat_sdspi_mount(mountPoint_.c_str(), &host, &device, &mount, &card);
    if (result != ESP_OK) {
        error_ = std::string("SD card mount failed: ") + esp_err_to_name(result);
        spi_bus_free(SPI2_HOST);
        busInitialized_ = false;
        card_ = nullptr;
        return false;
    }
    card_ = card;
    mounted_ = true;
    return true;
}

void IdfSdCard::unmount() {
    if (mounted_) {
        esp_vfs_fat_sdcard_unmount(mountPoint_.c_str(), static_cast<sdmmc_card_t*>(card_));
        card_ = nullptr;
        mounted_ = false;
    }
    if (busInitialized_) {
        spi_bus_free(SPI2_HOST);
        busInitialized_ = false;
    }
}

void IdfSdStorage::ensureAvailable() const {
    if (!card_.mounted())
        throw ServiceError(ServiceErrorCode::Unavailable, "storage: SD filesystem is not mounted");
}

std::string IdfSdStorage::resolve(const std::string& path, bool allowRoot) const {
    return joinPath(card_.mountPoint(), normalizePath(path, allowRoot, "storage"));
}

std::string IdfSdStorage::readText(const std::string& path) {
    ensureAvailable();
    const std::string fullPath = resolve(path, false);
    const int descriptor = ::open(fullPath.c_str(), O_RDONLY);
    if (descriptor < 0) {
        if (errno == ENOENT)
            throw ServiceError(ServiceErrorCode::NotFound, "storage: file not found: " + path);
        throw ServiceError(ServiceErrorCode::IoError, errorText("storage read"));
    }
    std::string contents;
    std::array<char, 256> buffer{};
    for (;;) {
        const auto readCount = ::read(descriptor, buffer.data(), buffer.size());
        if (readCount == 0) break;
        if (readCount < 0) {
            ::close(descriptor);
            throw ServiceError(ServiceErrorCode::IoError, errorText("storage read"));
        }
        contents.append(buffer.data(), static_cast<std::size_t>(readCount));
    }
    ::close(descriptor);
    return contents;
}

std::unique_ptr<runtime::StorageReadHandle> IdfSdStorage::openRead(
        const std::string& path) {
    ensureAvailable();
    const std::string fullPath = resolve(path, false);
    const int descriptor = ::open(fullPath.c_str(), O_RDONLY);
    if (descriptor < 0) {
        if (errno == ENOENT)
            throw ServiceError(ServiceErrorCode::NotFound,
                               "storage: file not found: " + path);
        throw ServiceError(ServiceErrorCode::IoError, errorText("storage open"));
    }
    struct stat status{};
    if (::fstat(descriptor, &status) < 0 || !S_ISREG(status.st_mode) || status.st_size < 0) {
        ::close(descriptor);
        throw ServiceError(ServiceErrorCode::InvalidPath,
                           "storage: path is not a regular file: " + path);
    }
    return std::make_unique<IdfSdReadHandle>(descriptor,
        static_cast<std::uint64_t>(status.st_size));
}

void IdfSdStorage::writeText(const std::string& path, const std::string& contents) {
    ensureAvailable();
    const std::string fullPath = resolve(path, false);
    const int descriptor = ::open(fullPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (descriptor < 0)
        throw ServiceError(ServiceErrorCode::IoError, errorText("storage write"));
    std::size_t offset = 0;
    while (offset < contents.size()) {
        const auto count = ::write(descriptor, contents.data() + offset, contents.size() - offset);
        if (count <= 0) {
            ::close(descriptor);
            throw ServiceError(ServiceErrorCode::IoError, errorText("storage write"));
        }
        offset += static_cast<std::size_t>(count);
    }
    ::close(descriptor);
}

void IdfSdStorage::writeTextStream(
        const std::string& path, const runtime::TextStreamSource& source) {
    ensureAvailable();
    const std::string fullPath = resolve(path, false);
    int descriptor = ::open(fullPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (descriptor < 0)
        throw ServiceError(ServiceErrorCode::IoError, errorText("storage stream open"));
    class FileSink final : public runtime::TextStreamSink {
    public:
        explicit FileSink(int descriptor) : descriptor_(descriptor) {}
        void write(std::string_view chunk) override {
            std::size_t offset = 0;
            while (offset < chunk.size()) {
                const auto written = ::write(descriptor_, chunk.data() + offset,
                                             chunk.size() - offset);
                if (written <= 0)
                    throw ServiceError(ServiceErrorCode::IoError,
                                       errorText("storage stream write"));
                offset += static_cast<std::size_t>(written);
            }
        }
    private:
        int descriptor_;
    } sink(descriptor);
    try {
        source.writeTo(sink);
        if (::fsync(descriptor) != 0)
            throw ServiceError(ServiceErrorCode::IoError,
                               errorText("storage stream sync"));
        const int closing = descriptor;
        descriptor = -1;
        if (::close(closing) != 0)
            throw ServiceError(ServiceErrorCode::IoError,
                               errorText("storage stream close"));
    } catch (...) {
        if (descriptor >= 0) ::close(descriptor);
        throw;
    }
}

std::vector<runtime::FileEntry> IdfSdStorage::list(const std::string& path) {
    ensureAvailable();
    const std::string fullPath = resolve(path, true);
    DIR* directory = ::opendir(fullPath.c_str());
    if (!directory) {
        if (errno == ENOENT)
            throw ServiceError(ServiceErrorCode::NotFound, "storage: directory not found: " + path);
        throw ServiceError(ServiceErrorCode::IoError, errorText("storage list"));
    }
    std::vector<runtime::FileEntry> entries;
    while (dirent* entry = ::readdir(directory)) {
        if (std::string_view(entry->d_name) == "." || std::string_view(entry->d_name) == "..") continue;
        const std::string entryPath = fullPath + "/" + entry->d_name;
        struct stat status{};
        if (::stat(entryPath.c_str(), &status) != 0) continue;
        entries.push_back({entry->d_name, static_cast<std::uintmax_t>(status.st_size),
                           S_ISDIR(status.st_mode)});
    }
    ::closedir(directory);
    std::sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
        return left.name < right.name;
    });
    return entries;
}

bool IdfSdStorage::exists(const std::string& path) {
    ensureAvailable();
    const std::string fullPath = resolve(path, false);
    struct stat status{};
    if (::stat(fullPath.c_str(), &status) == 0) return true;
    if (errno == ENOENT) return false;
    throw ServiceError(ServiceErrorCode::IoError, errorText("storage stat"));
}

std::uintmax_t IdfSdStorage::size(const std::string& path) {
    ensureAvailable();
    const std::string fullPath = resolve(path, false);
    struct stat status{};
    if (::stat(fullPath.c_str(), &status) != 0) {
        if (errno == ENOENT)
            throw ServiceError(ServiceErrorCode::NotFound,
                               "storage: file not found: " + path);
        throw ServiceError(ServiceErrorCode::IoError, errorText("storage stat"));
    }
    if (!S_ISREG(status.st_mode))
        throw ServiceError(ServiceErrorCode::InvalidPath,
                           "storage: path is not a file: " + path);
    return static_cast<std::uintmax_t>(status.st_size);
}

void IdfSdStorage::rename(const std::string& path, const std::string& newPath) {
    ensureAvailable();
    const std::string source = resolve(path, false);
    const std::string destination = resolve(newPath, false);
    struct stat status{};
    if (::stat(source.c_str(), &status) != 0) {
        if (errno == ENOENT)
            throw ServiceError(ServiceErrorCode::NotFound,
                               "storage: file not found: " + path);
        throw ServiceError(ServiceErrorCode::IoError, errorText("storage stat"));
    }
    if (S_ISDIR(status.st_mode))
        throw ServiceError(ServiceErrorCode::InvalidPath,
                           "storage: path is a directory: " + path);
    if (::stat(destination.c_str(), &status) == 0)
        throw ServiceError(ServiceErrorCode::IoError,
                           "storage: destination already exists: " + newPath);
    if (errno != ENOENT)
        throw ServiceError(ServiceErrorCode::IoError, errorText("storage stat"));
    if (::rename(source.c_str(), destination.c_str()) != 0)
        throw ServiceError(ServiceErrorCode::IoError, errorText("storage rename"));
}

void IdfSdStorage::remove(const std::string& path) {
    ensureAvailable();
    const std::string fullPath = resolve(path, false);
    struct stat status{};
    if (::stat(fullPath.c_str(), &status) != 0) {
        if (errno == ENOENT)
            throw ServiceError(ServiceErrorCode::NotFound,
                               "storage: file not found: " + path);
        throw ServiceError(ServiceErrorCode::IoError, errorText("storage stat"));
    }
    if (!S_ISREG(status.st_mode))
        throw ServiceError(ServiceErrorCode::InvalidPath,
                           "storage: path is not a file: " + path);
    if (::unlink(fullPath.c_str()) != 0)
        throw ServiceError(ServiceErrorCode::IoError, errorText("storage remove"));
}

void IdfSdStorage::removeDirectory(const std::string& path) {
    ensureAvailable();
    const std::string fullPath = resolve(path, false);
    struct stat status{};
    if (::stat(fullPath.c_str(), &status) != 0) {
        if (errno == ENOENT)
            throw ServiceError(ServiceErrorCode::NotFound, "storage: directory not found: " + path);
        throw ServiceError(ServiceErrorCode::IoError, errorText("storage stat"));
    }
    if (!S_ISDIR(status.st_mode))
        throw ServiceError(ServiceErrorCode::InvalidPath, "storage: path is not a directory: " + path);
    if (::rmdir(fullPath.c_str()) != 0)
        throw ServiceError(ServiceErrorCode::IoError, errorText("storage remove directory"));
}

void IdfSdStorage::format(const std::string& path) {
    const std::string resolved = normalizePath(path, true, "storage");
    const std::string relative = resolved == "/" ? std::string{} : resolved.substr(1);
    const auto entries = list(relative);
    for (const auto& entry : entries) {
        const std::string child = relative.empty() ? entry.name : relative + "/" + entry.name;
        if (entry.isDirectory) {
            format(child);
            removeDirectory(child);
        } else remove(child);
    }
}

IdfSdChannels::~IdfSdChannels() {
    std::lock_guard lock(mutex_);
    for (auto& [id, channel] : channels_) {
        (void)id;
        ::close(channel.descriptor);
    }
}

void IdfSdChannels::ensureAvailable() const {
    if (!card_.mounted())
        throw ServiceError(ServiceErrorCode::Unavailable, "channel: SD filesystem is not mounted");
}

std::string IdfSdChannels::resolve(const std::string& path) const {
    return joinPath(card_.mountPoint(), normalizePath(path, false, "channel"));
}

runtime::ChannelId IdfSdChannels::open(runtime::SessionId session, const std::string& path,
                                        runtime::ChannelMode mode) {
    ensureAvailable();
    const std::string fullPath = resolve(path);
    int flags = O_RDONLY;
    if (mode == runtime::ChannelMode::Write) flags = O_WRONLY | O_CREAT | O_TRUNC;
    if (mode == runtime::ChannelMode::Append) flags = O_WRONLY | O_CREAT | O_APPEND;
    const int descriptor = ::open(fullPath.c_str(), flags, 0666);
    if (descriptor < 0) {
        if (errno == ENOENT && mode == runtime::ChannelMode::Read)
            throw ServiceError(ServiceErrorCode::NotFound, "channel: cannot open file: " + path);
        throw ServiceError(ServiceErrorCode::IoError, errorText("channel open"));
    }
    std::lock_guard lock(mutex_);
    while (nextId_ == 0 || channels_.find(nextId_) != channels_.end()) ++nextId_;
    const auto id = nextId_++;
    channels_.emplace(id, OpenChannel{session, mode, descriptor});
    return id;
}

IdfSdChannels::OpenChannel& IdfSdChannels::owned(runtime::SessionId session,
                                                   runtime::ChannelId channel) {
    const auto found = channels_.find(channel);
    if (found == channels_.end() || found->second.session != session)
        throw ServiceError(ServiceErrorCode::InvalidHandle,
                           "channel is not open for this session");
    return found->second;
}

std::optional<std::string> IdfSdChannels::readLine(runtime::SessionId session,
                                                    runtime::ChannelId channel) {
    std::lock_guard lock(mutex_);
    OpenChannel& opened = owned(session, channel);
    if (opened.mode != runtime::ChannelMode::Read)
        throw ServiceError(ServiceErrorCode::InvalidHandle, "channel is not open for reading");
    std::string line;
    char character = 0;
    while (true) {
        const auto count = ::read(opened.descriptor, &character, 1);
        if (count == 0) return line.empty() ? std::nullopt : std::optional<std::string>{line};
        if (count < 0) throw ServiceError(ServiceErrorCode::IoError, errorText("channel read"));
        if (character == '\n') return line;
        line.push_back(character);
    }
}

std::string IdfSdChannels::readBytes(runtime::SessionId session, runtime::ChannelId channel,
                                     std::size_t count) {
    std::lock_guard lock(mutex_);
    OpenChannel& opened = owned(session, channel);
    if (opened.mode != runtime::ChannelMode::Read)
        throw ServiceError(ServiceErrorCode::InvalidHandle, "channel is not open for reading");
    std::string bytes(count, '\0');
    std::size_t offset = 0;
    while (offset < count) {
        const auto received = ::read(opened.descriptor, bytes.data() + offset, count - offset);
        if (received == 0)
            throw ServiceError(ServiceErrorCode::IoError, "channel has fewer bytes than requested");
        if (received < 0) throw ServiceError(ServiceErrorCode::IoError, errorText("channel read"));
        offset += static_cast<std::size_t>(received);
    }
    return bytes;
}

void IdfSdChannels::writeAll(int descriptor, std::string_view bytes) {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto count = ::write(descriptor, bytes.data() + offset, bytes.size() - offset);
        if (count <= 0) throw ServiceError(ServiceErrorCode::IoError, errorText("channel write"));
        offset += static_cast<std::size_t>(count);
    }
}

void IdfSdChannels::writeBytes(runtime::SessionId session, runtime::ChannelId channel,
                               std::string_view bytes) {
    std::lock_guard lock(mutex_);
    OpenChannel& opened = owned(session, channel);
    if (opened.mode == runtime::ChannelMode::Read)
        throw ServiceError(ServiceErrorCode::InvalidHandle, "channel is not open for writing");
    writeAll(opened.descriptor, bytes);
}

void IdfSdChannels::seek(runtime::SessionId session, runtime::ChannelId channel,
                         std::uint64_t offset) {
    std::lock_guard lock(mutex_);
    OpenChannel& opened = owned(session, channel);
    if (opened.mode == runtime::ChannelMode::Append)
        throw ServiceError(ServiceErrorCode::InvalidHandle,
                           "append channels do not support seeking");
    if (offset > static_cast<std::uint64_t>(std::numeric_limits<off_t>::max()) ||
        ::lseek(opened.descriptor, static_cast<off_t>(offset), SEEK_SET) < 0)
        throw ServiceError(ServiceErrorCode::IoError, errorText("channel seek"));
}

bool IdfSdChannels::atEnd(runtime::SessionId session, runtime::ChannelId channel) {
    std::lock_guard lock(mutex_);
    OpenChannel& opened = owned(session, channel);
    if (opened.mode != runtime::ChannelMode::Read)
        throw ServiceError(ServiceErrorCode::InvalidHandle, "channel is not open for reading");
    const auto current = ::lseek(opened.descriptor, 0, SEEK_CUR);
    const auto end = ::lseek(opened.descriptor, 0, SEEK_END);
    if (current < 0 || end < 0 || ::lseek(opened.descriptor, current, SEEK_SET) < 0)
        throw ServiceError(ServiceErrorCode::IoError, errorText("channel EOF"));
    return current >= end;
}

void IdfSdChannels::write(runtime::SessionId session, runtime::ChannelId channel,
                          const std::string& text) {
    writeBytes(session, channel, text);
}

void IdfSdChannels::close(runtime::SessionId session, runtime::ChannelId channel) {
    std::lock_guard lock(mutex_);
    OpenChannel& opened = owned(session, channel);
    ::close(opened.descriptor);
    channels_.erase(channel);
}

void IdfSdChannels::closeSession(runtime::SessionId session) {
    std::lock_guard lock(mutex_);
    for (auto found = channels_.begin(); found != channels_.end();) {
        if (found->second.session == session) {
            ::close(found->second.descriptor);
            found = channels_.erase(found);
        } else {
            ++found;
        }
    }
}

std::vector<runtime::ChannelInfo> IdfSdChannels::listOpen(runtime::SessionId session) {
    std::lock_guard lock(mutex_);
    std::vector<runtime::ChannelInfo> result;
    for (const auto& [id, channel] : channels_)
        if (channel.session == session) result.push_back({id, runtime::ChannelType::File});
    return result;
}

} // namespace ql32::platform::esp32
