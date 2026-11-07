#pragma once

#include "ql32/runtime/storage.hpp"

#include <filesystem>

namespace ql32::platform::linux_host {

class DirectoryStorage final : public runtime::StorageService {
public:
    explicit DirectoryStorage(std::filesystem::path root);

    std::string readText(const std::string& path) override;
    void writeText(const std::string& path, const std::string& contents) override;
    void writeTextStream(const std::string& path,
                         const runtime::TextStreamSource& source) override;
    std::vector<runtime::FileEntry> list(const std::string& path) override;
    bool exists(const std::string& path) override;
    std::uintmax_t size(const std::string& path) override;
    void rename(const std::string& path, const std::string& newPath) override;
    void remove(const std::string& path) override;
    void removeDirectory(const std::string& path) override;
    void format(const std::string& path) override;

private:
    std::filesystem::path root_;
    std::filesystem::path resolve(const std::string& path, bool mayNotExist) const;
};

} // namespace ql32::platform::linux_host
