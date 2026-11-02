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
};

} // namespace ql32::runtime
