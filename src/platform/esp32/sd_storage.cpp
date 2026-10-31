#include "ql32/platform/esp32/sd_storage.hpp"

#include <algorithm>
#include <array>
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

} // namespace

void SdStorage::ensureAvailable() {
    fs::File root = filesystem_.open("/", FILE_READ);
    if (!root || !root.isDirectory())
        throw ServiceError(ServiceErrorCode::Unavailable,
                           "storage: SD filesystem is not mounted");
    root.close();
}

std::string SdStorage::resolve(const std::string& path, bool allowRoot) {
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
        info.name = fileName(entry.name());
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

} // namespace ql32::platform::esp32
