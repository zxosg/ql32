#pragma once

#include "ql32/runtime/storage.hpp"

#include <filesystem>

namespace ql32::platform::linux_host {

class DirectoryStorage final : public runtime::StorageService {
public:
    explicit DirectoryStorage(std::filesystem::path root);

    std::string readText(const std::string& path) override;
    void writeText(const std::string& path, const std::string& contents) override;
    std::vector<runtime::FileEntry> list(const std::string& path) override;

private:
    std::filesystem::path root_;
    std::filesystem::path resolve(const std::string& path, bool mayNotExist) const;
};

} // namespace ql32::platform::linux_host
