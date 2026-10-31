#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ql32::runtime {

enum class ServiceErrorCode {
    InvalidPath,
    NotFound,
    PermissionDenied,
    IoError,
    Unavailable,
    InvalidHandle
};

class ServiceError final : public std::runtime_error {
public:
    ServiceError(ServiceErrorCode code, std::string message)
        : std::runtime_error(std::move(message)), code_(code) {}

    ServiceErrorCode code() const noexcept { return code_; }

private:
    ServiceErrorCode code_;
};

struct FileEntry {
    std::string name;
    std::uintmax_t size = 0;
    bool isDirectory = false;
};

class StorageService {
public:
    virtual ~StorageService() = default;
    virtual std::string readText(const std::string& path) = 0;
    virtual void writeText(const std::string& path, const std::string& contents) = 0;
    virtual std::vector<FileEntry> list(const std::string& path) = 0;
};

} // namespace ql32::runtime
