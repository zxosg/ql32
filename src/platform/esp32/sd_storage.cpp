#include "ql32/platform/esp32/sd_storage.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <limits>
#include <utility>

namespace ql32::platform::esp32 {
namespace {

using runtime::ServiceError;
using runtime::ServiceErrorCode;

std::string fileName(const char* path) {
    if (!path) return {};
    const std::string value(path);
    const auto slash = value.find_last_of('/');
    return slash == std::string::npos ? value : value.substr(slash + 1);
}

std::string entryPathRelativeTo(const std::string& directory,
                                const char* entryPath,
                                const char* fallbackName) {
    std::string path = entryPath ? entryPath : "";
    if (path.empty() && fallbackName) path = fallbackName;

    if (directory == "/") {
        while (!path.empty() && path.front() == '/') path.erase(path.begin());
        return path.empty() ? fileName(fallbackName) : path;
    }

    const std::string prefix = directory.back() == '/' ? directory : directory + "/";
    if (path.rfind(prefix, 0) == 0) return path.substr(prefix.size());
    if (!path.empty() && path.front() != '/') return path;
    return fileName(path.c_str());
}

class SdReadHandle final : public runtime::StorageReadHandle {
public:
    explicit SdReadHandle(fs::File file) : file_(std::move(file)), size_(file_.size()) {}
    ~SdReadHandle() override { file_.close(); }

    std::uint64_t size() const noexcept override { return size_; }

    std::size_t readAt(std::uint64_t offset, std::uint8_t* destination,
                       std::size_t capacity) override {
        if (offset >= size_ || capacity == 0) return 0;
        if (offset > std::numeric_limits<std::uint32_t>::max() ||
            !file_.seek(static_cast<std::uint32_t>(offset)))
            throw ServiceError(ServiceErrorCode::IoError, "storage: bounded seek failed");
        const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(
            capacity, size_ - offset));
        const auto read = file_.read(destination, count);
        if (read == 0 && count != 0)
            throw ServiceError(ServiceErrorCode::IoError, "storage: bounded read failed");
        return read;
    }

private:
    fs::File file_;
    std::uint64_t size_ = 0;
};

} // namespace

void SdStorage::ensureAvailable() {
    fs::File root = filesystem_.open("/", FILE_READ);
    if (!root || !root.isDirectory())
        throw ServiceError(ServiceErrorCode::Unavailable,
                           "storage: filesystem is not mounted");
    root.close();
}

std::string SdStorage::resolve(const std::string& input, bool allowRoot) const {
    std::string path = input;
    const auto colon = path.find(':');
    if (colon != std::string::npos) {
        std::string drive = path.substr(0, colon);
        std::transform(drive.begin(), drive.end(), drive.begin(),
            [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        std::string expected = drive_;
        std::transform(expected.begin(), expected.end(), expected.begin(),
            [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        if (drive != expected)
            throw ServiceError(ServiceErrorCode::Unavailable,
                               "storage drive is not available: " + drive);
        path.erase(0, colon + 1);
        if (!path.empty() && path.front() == '/') path.erase(path.begin());
    }
    if (path.find('\0') != std::string::npos || path.find('\\') != std::string::npos ||
        (!path.empty() && path.front() == '/'))
        throw ServiceError(ServiceErrorCode::InvalidPath,
                           "storage path must be relative to the SD root");

    std::string resolved;
    std::size_t start = 0;
    while (start <= path.size()) {
        const auto end = path.find('/', start);
        const auto length = (end == std::string::npos ? path.size() : end) - start;
        const std::string component = path.substr(start, length);
        if (component == "..")
            throw ServiceError(ServiceErrorCode::InvalidPath,
                               "storage path cannot escape the SD root");
        if (!component.empty() && component != ".") {
            resolved.push_back('/');
            resolved += component;
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }

    if (resolved.empty()) {
        if (allowRoot) return "/";
        throw ServiceError(ServiceErrorCode::InvalidPath,
                           "storage file path must not be empty");
    }
    return resolved;
}

std::string SdStorage::readText(const std::string& path) {
    const std::string resolved = resolve(path, false);
    ensureAvailable();
    fs::File file = filesystem_.open(resolved.c_str(), FILE_READ);
    if (!file)
        throw ServiceError(ServiceErrorCode::NotFound,
                           "storage: file not found: " + path);
    if (file.isDirectory()) {
        file.close();
        throw ServiceError(ServiceErrorCode::InvalidPath,
                           "storage: path names a directory: " + path);
    }

    std::string contents;
    contents.reserve(file.size());
    std::array<std::uint8_t, 128> buffer{};
    while (file.available()) {
        const std::size_t count = file.read(buffer.data(), buffer.size());
        if (count == 0) {
            file.close();
            throw ServiceError(ServiceErrorCode::IoError,
                               "storage: read failed: " + path);
        }
        contents.append(reinterpret_cast<const char*>(buffer.data()), count);
    }
    file.close();
    return contents;
}

std::unique_ptr<runtime::StorageReadHandle> SdStorage::openRead(
        const std::string& path) {
    const std::string resolved = resolve(path, false);
    ensureAvailable();
    fs::File file = filesystem_.open(resolved.c_str(), FILE_READ);
    if (!file)
        throw ServiceError(ServiceErrorCode::NotFound,
                           "storage: file not found: " + path);
    if (file.isDirectory()) {
        file.close();
        throw ServiceError(ServiceErrorCode::InvalidPath,
                           "storage: path names a directory: " + path);
    }
    return std::make_unique<SdReadHandle>(std::move(file));
}

void SdStorage::writeText(const std::string& path, const std::string& contents) {
    const std::string resolved = resolve(path, false);
    ensureAvailable();
    fs::File file = filesystem_.open(resolved.c_str(), FILE_WRITE);
    if (!file)
        throw ServiceError(ServiceErrorCode::IoError,
                           "storage: cannot open for writing: " + path);
    const std::size_t written = file.write(
        reinterpret_cast<const std::uint8_t*>(contents.data()), contents.size());
    file.flush();
    file.close();
    if (written != contents.size())
        throw ServiceError(ServiceErrorCode::IoError,
                           "storage: incomplete write: " + path);
}

void SdStorage::writeTextStream(
        const std::string& path, const runtime::TextStreamSource& source) {
    const std::string resolved = resolve(path, false);
    ensureAvailable();
    fs::File file = filesystem_.open(resolved.c_str(), FILE_WRITE);
    if (!file)
        throw ServiceError(ServiceErrorCode::IoError,
                           "storage: cannot open for writing: " + path);
    class FileSink final : public runtime::TextStreamSink {
    public:
        explicit FileSink(fs::File& file) : file_(file) {}
        void write(std::string_view chunk) override {
            const auto* data = reinterpret_cast<const std::uint8_t*>(chunk.data());
            std::size_t offset = 0;
            while (offset < chunk.size()) {
                const auto written = file_.write(data + offset, chunk.size() - offset);
                if (written == 0)
                    throw ServiceError(ServiceErrorCode::IoError,
                                       "storage: incomplete streamed write");
                offset += written;
            }
        }
    private:
        fs::File& file_;
    } sink(file);
    try {
        source.writeTo(sink);
        file.flush();
        file.close();
    } catch (...) {
        file.close();
        throw;
    }
}

std::vector<runtime::FileEntry> SdStorage::list(const std::string& path) {
    const std::string resolved = resolve(path, true);
    ensureAvailable();
    fs::File directory = filesystem_.open(resolved.c_str(), FILE_READ);
    if (!directory)
        throw ServiceError(ServiceErrorCode::NotFound,
                           "storage: directory not found: " + path);
    if (!directory.isDirectory()) {
        directory.close();
        throw ServiceError(ServiceErrorCode::InvalidPath,
                           "storage: path is not a directory: " + path);
    }

    std::vector<runtime::FileEntry> entries;
    while (fs::File entry = directory.openNextFile()) {
        runtime::FileEntry info;
        // Keep the path from the queried directory. SPIFFS can enumerate
        // entries from nested paths; reducing these to basenames creates
        // duplicates and makes LOAD resolve to a directory with the same name.
        info.name = entryPathRelativeTo(resolved, entry.path(), entry.name());
        info.isDirectory = entry.isDirectory();
        if (!info.isDirectory) info.size = entry.size();
        entry.close();
        entries.push_back(std::move(info));
    }
    directory.close();
    std::sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
        return left.name < right.name;
    });
    return entries;
}

bool SdStorage::exists(const std::string& path) {
    const std::string resolved = resolve(path, false);
    ensureAvailable();
    return filesystem_.exists(resolved.c_str());
}

std::uintmax_t SdStorage::size(const std::string& path) {
    const std::string resolved = resolve(path, false);
    ensureAvailable();
    fs::File file = filesystem_.open(resolved.c_str(), FILE_READ);
    if (!file)
        throw ServiceError(ServiceErrorCode::NotFound,
                           "storage: file not found: " + path);
    if (file.isDirectory()) {
        file.close();
        throw ServiceError(ServiceErrorCode::InvalidPath,
                           "storage: path names a directory: " + path);
    }
    const std::uintmax_t result = file.size();
    file.close();
    return result;
}

void SdStorage::rename(const std::string& path, const std::string& newPath) {
    const std::string source = resolve(path, false);
    const std::string destination = resolve(newPath, false);
    ensureAvailable();
    if (!filesystem_.exists(source.c_str()))
        throw ServiceError(ServiceErrorCode::NotFound,
                           "storage: file not found: " + path);
    fs::File sourceFile = filesystem_.open(source.c_str(), FILE_READ);
    const bool sourceIsDirectory = sourceFile && sourceFile.isDirectory();
    if (sourceFile) sourceFile.close();
    if (sourceIsDirectory)
        throw ServiceError(ServiceErrorCode::InvalidPath,
                           "storage: path names a directory: " + path);
    if (filesystem_.exists(destination.c_str()))
        throw ServiceError(ServiceErrorCode::IoError,
                           "storage: destination already exists: " + newPath);
    if (!filesystem_.rename(source.c_str(), destination.c_str()))
        throw ServiceError(ServiceErrorCode::IoError,
                           "storage: rename failed: " + path);
}

void SdStorage::remove(const std::string& path) {
    const std::string resolved = resolve(path, false);
    ensureAvailable();
    fs::File file = filesystem_.open(resolved.c_str(), FILE_READ);
    if (!file)
        throw ServiceError(ServiceErrorCode::NotFound,
                           "storage: file not found: " + path);
    const bool directory = file.isDirectory();
    file.close();
    if (directory)
        throw ServiceError(ServiceErrorCode::InvalidPath,
                           "storage: path names a directory: " + path);
    if (!filesystem_.remove(resolved.c_str()))
        throw ServiceError(ServiceErrorCode::IoError,
                           "storage: remove failed: " + path);
}

void SdStorage::removeDirectory(const std::string& path) {
    const std::string resolved = resolve(path, false);
    ensureAvailable();
    fs::File directory = filesystem_.open(resolved.c_str(), FILE_READ);
    if (!directory)
        throw ServiceError(ServiceErrorCode::NotFound, "storage: directory not found: " + path);
    const bool isDirectory = directory.isDirectory();
    directory.close();
    if (!isDirectory)
        throw ServiceError(ServiceErrorCode::InvalidPath, "storage: path is not a directory: " + path);
    if (!filesystem_.rmdir(resolved.c_str()))
        throw ServiceError(ServiceErrorCode::IoError, "storage: directory is not empty: " + path);
}

void SdStorage::format(const std::string& path) {
    const std::string resolved = resolve(path, true);
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

} // namespace ql32::platform::esp32
