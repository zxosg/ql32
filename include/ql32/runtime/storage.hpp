#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "ql32/runtime/error.hpp"

namespace ql32::runtime {

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
    virtual bool exists(const std::string& path) = 0;
    virtual std::uintmax_t size(const std::string& path) = 0;
    virtual void rename(const std::string& path, const std::string& newPath) = 0;
    virtual void remove(const std::string& path) = 0;
    virtual void removeDirectory(const std::string&) {
        throw ServiceError(ServiceErrorCode::Unavailable, "storage: directory removal unavailable");
    }
    virtual void format(const std::string&) {
        throw ServiceError(ServiceErrorCode::Unavailable, "storage: format unavailable");
    }
};

} // namespace ql32::runtime
