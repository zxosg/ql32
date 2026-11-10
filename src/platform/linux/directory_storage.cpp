#include "ql32/platform/linux/directory_storage.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <fstream>
#include <iterator>
#include <limits>
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

class DirectoryReadHandle final : public runtime::StorageReadHandle {
public:
    explicit DirectoryReadHandle(const std::filesystem::path& path) : file_(path, std::ios::binary) {
        if (!file_) {
            std::error_code error(errno, std::generic_category());
            fail(path, error);
        }
        file_.seekg(0, std::ios::end);
        const auto end = file_.tellg();
        if (end < 0) throw ServiceError(ServiceErrorCode::IoError,
                                       "storage: failed to measure " + path.string());
        size_ = static_cast<std::uint64_t>(end);
        file_.seekg(0, std::ios::beg);
    }

    std::uint64_t size() const noexcept override { return size_; }

    std::size_t readAt(std::uint64_t offset, std::uint8_t* destination,
                       std::size_t capacity) override {
        if (offset >= size_ || capacity == 0) return 0;
        const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(
            capacity, size_ - offset));
        if (offset > static_cast<std::uint64_t>(
                std::numeric_limits<std::streamoff>::max()) ||
            count > static_cast<std::size_t>(
                std::numeric_limits<std::streamsize>::max()))
            throw ServiceError(ServiceErrorCode::InvalidPath,
                               "storage: read range exceeds supported file offsets");
        file_.clear();
        file_.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        file_.read(reinterpret_cast<char*>(destination),
                   static_cast<std::streamsize>(count));
        if (!file_ && !file_.eof())
            throw ServiceError(ServiceErrorCode::IoError, "storage: bounded read failed");
        return static_cast<std::size_t>(file_.gcount());
    }

private:
    std::ifstream file_;
    std::uint64_t size_ = 0;
};

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
    std::string normalized = path;
    const auto colon = normalized.find(':');
    if (colon != std::string::npos) normalized.erase(0, colon + 1);
    while (!normalized.empty() && normalized.front() == '/') normalized.erase(normalized.begin());
    const std::filesystem::path relative(normalized);
    if (relative.empty()) return root_;
    if (relative.is_absolute())
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

std::unique_ptr<runtime::StorageReadHandle> DirectoryStorage::openRead(
        const std::string& path) {
    return std::make_unique<DirectoryReadHandle>(resolve(path, false));
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

void DirectoryStorage::writeTextStream(
        const std::string& path, const runtime::TextStreamSource& source) {
    const auto resolved = resolve(path, true);
    std::ofstream file(resolved, std::ios::binary | std::ios::trunc);
    if (!file) {
        std::error_code error(errno, std::generic_category());
        fail(resolved, error);
    }
    class FileSink final : public runtime::TextStreamSink {
    public:
        explicit FileSink(std::ofstream& file) : file_(file) {}
        void write(std::string_view chunk) override {
            file_.write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
            if (!file_)
                throw ServiceError(ServiceErrorCode::IoError,
                                   "storage: streamed write failed");
        }
    private:
        std::ofstream& file_;
    } sink(file);
    source.writeTo(sink);
    file.flush();
    if (!file)
        throw ServiceError(ServiceErrorCode::IoError,
                           "storage: streamed write failed");
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

bool DirectoryStorage::exists(const std::string& path) {
    const auto resolved = resolve(path, true);
    std::error_code error;
    const bool found = std::filesystem::exists(resolved, error);
    if (error) fail(resolved, error);
    return found;
}

std::uintmax_t DirectoryStorage::size(const std::string& path) {
    const auto resolved = resolve(path, false);
    std::error_code error;
    if (!std::filesystem::is_regular_file(resolved, error)) {
        if (error) fail(resolved, error);
        throw ServiceError(ServiceErrorCode::InvalidPath,
                           "storage: path is not a file: " + path);
    }
    const auto result = std::filesystem::file_size(resolved, error);
    if (error) fail(resolved, error);
    return result;
}

void DirectoryStorage::rename(const std::string& path, const std::string& newPath) {
    const auto source = resolve(path, false);
    const auto destination = resolve(newPath, true);
    std::error_code error;
    if (!std::filesystem::is_regular_file(source, error)) {
        if (error) fail(source, error);
        throw ServiceError(ServiceErrorCode::InvalidPath,
                           "storage: path is not a file: " + path);
    }
    if (std::filesystem::exists(destination, error)) {
        if (error) fail(destination, error);
        throw ServiceError(ServiceErrorCode::IoError,
                           "storage: destination already exists: " + newPath);
    }
    if (error) fail(destination, error);
    std::filesystem::rename(source, destination, error);
    if (error) fail(source, error);
}

void DirectoryStorage::remove(const std::string& path) {
    const auto resolved = resolve(path, false);
    std::error_code error;
    if (!std::filesystem::is_regular_file(resolved, error)) {
        if (error) fail(resolved, error);
        throw ServiceError(ServiceErrorCode::InvalidPath,
                           "storage: path is not a file: " + path);
    }
    if (!std::filesystem::remove(resolved, error)) {
        if (error) fail(resolved, error);
        throw ServiceError(ServiceErrorCode::IoError,
                           "storage: could not remove file: " + path);
    }
}

void DirectoryStorage::removeDirectory(const std::string& path) {
    const auto resolved = resolve(path, false);
    if (resolved == root_)
        throw ServiceError(ServiceErrorCode::InvalidPath, "storage: cannot remove storage root");
    std::error_code error;
    if (!std::filesystem::is_directory(resolved, error)) {
        if (error) fail(resolved, error);
        throw ServiceError(ServiceErrorCode::InvalidPath, "storage: path is not a directory: " + path);
    }
    if (!std::filesystem::remove(resolved, error)) {
        if (error) fail(resolved, error);
        throw ServiceError(ServiceErrorCode::IoError, "storage: directory is not empty: " + path);
    }
}

void DirectoryStorage::format(const std::string& path) {
    const auto resolved = path.empty() ? root_ : resolve(path, false);
    std::error_code error;
    if (!std::filesystem::is_directory(resolved, error)) {
        if (error) fail(resolved, error);
        throw ServiceError(ServiceErrorCode::InvalidPath, "storage: path is not a directory: " + path);
    }
    for (std::filesystem::directory_iterator it(resolved, error), end; it != end; it.increment(error)) {
        if (error) fail(resolved, error);
        std::filesystem::remove_all(it->path(), error);
        if (error) fail(it->path(), error);
    }
}

} // namespace ql32::platform::linux_host
