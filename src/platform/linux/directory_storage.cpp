#include "ql32/platform/linux/directory_storage.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <fstream>
#include <iterator>
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
    throw ServiceError(code, "storage: " + path.string() + ": " + error.message());
}

bool isWithin(const std::filesystem::path& root, const std::filesystem::path& candidate) {
    auto rootPart = root.begin();
    auto candidatePart = candidate.begin();
    for (; rootPart != root.end(); ++rootPart, ++candidatePart) {
        if (candidatePart == candidate.end() || *rootPart != *candidatePart) return false;
    }
    return true;
}

} // namespace

DirectoryStorage::DirectoryStorage(std::filesystem::path root) {
    std::error_code error;
    root_ = std::filesystem::canonical(std::move(root), error);
    if (error || !std::filesystem::is_directory(root_))
        throw ServiceError(ServiceErrorCode::InvalidPath,
                           "storage root must be an existing directory");
}

std::filesystem::path DirectoryStorage::resolve(const std::string& path,
                                                bool mayNotExist) const {
    const std::filesystem::path relative(path);
    if (relative.empty() || relative.is_absolute())
        throw ServiceError(ServiceErrorCode::InvalidPath, "storage path must be relative");
    for (const auto& component : relative) {
        if (component == "..")
            throw ServiceError(ServiceErrorCode::InvalidPath,
                               "storage path cannot escape the configured root");
    }

    const auto candidate = root_ / relative;
    std::error_code error;
    const auto resolved = mayNotExist ? std::filesystem::weakly_canonical(candidate, error)
                                      : std::filesystem::canonical(candidate, error);
    if (error) fail(candidate, error);
    if (!isWithin(root_, resolved))
        throw ServiceError(ServiceErrorCode::InvalidPath,
                           "storage path resolves outside the configured root");
    return resolved;
}

std::string DirectoryStorage::readText(const std::string& path) {
    const auto resolved = resolve(path, false);
    std::ifstream file(resolved, std::ios::binary);
    if (!file) {
        std::error_code error(errno, std::generic_category());
        fail(resolved, error);
    }
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void DirectoryStorage::writeText(const std::string& path, const std::string& contents) {
    const auto resolved = resolve(path, true);
    static std::atomic<std::uint64_t> sequence{0};
    const auto temporary = resolved.parent_path() /
        (resolved.filename().string() + ".ql32-tmp-" +
         std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)));
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) {
            std::error_code error(errno, std::generic_category());
            fail(temporary, error);
        }
        file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        file.flush();
        if (!file) {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            throw ServiceError(ServiceErrorCode::IoError,
                               "storage: failed writing " + resolved.string());
        }
    }
    std::error_code error;
    std::filesystem::rename(temporary, resolved, error);
    if (error) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        fail(resolved, error);
    }
}

std::vector<runtime::FileEntry> DirectoryStorage::list(const std::string& path) {
    const auto resolved = path.empty() ? root_ : resolve(path, false);
    std::error_code error;
    std::filesystem::directory_iterator iterator(resolved, error);
    if (error) fail(resolved, error);
    std::vector<runtime::FileEntry> entries;
    for (const auto& entry : iterator) {
        const auto status = entry.status(error);
        if (error) fail(entry.path(), error);
        runtime::FileEntry info;
        info.name = entry.path().filename().string();
        info.isDirectory = std::filesystem::is_directory(status);
        if (!info.isDirectory) {
            info.size = entry.file_size(error);
            if (error) fail(entry.path(), error);
        }
        entries.push_back(std::move(info));
    }
    std::sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
        return left.name < right.name;
    });
    return entries;
}

} // namespace ql32::platform::linux_host
