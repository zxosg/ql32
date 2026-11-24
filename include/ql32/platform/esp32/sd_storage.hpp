#pragma once

#include "ql32/runtime/storage.hpp"

#include <FS.h>
#include <string>

namespace ql32::platform::esp32 {

// Storage adapter for Arduino-ESP32 filesystems such as SD and LittleFS.
// Paths are relative to the mounted filesystem root.
class SdStorage final : public runtime::StorageService {
public:
    explicit SdStorage(fs::FS& filesystem, std::string drive = "SDC", std::string mountPoint = {})
        : filesystem_(filesystem), drive_(std::move(drive)), mountPoint_(std::move(mountPoint)) {}

    std::string readText(const std::string& path) override;
    std::unique_ptr<runtime::StorageReadHandle> openRead(
        const std::string& path) override;
    void writeText(const std::string& path, const std::string& contents) override;
    void writeTextStream(const std::string& path,
                         const runtime::TextStreamSource& source) override;
    std::unique_ptr<runtime::DirectoryCursor> openDirectory(const std::string& path) override;
    std::vector<runtime::FileEntry> list(const std::string& path) override;
    bool exists(const std::string& path) override;
    std::uintmax_t size(const std::string& path) override;
    void rename(const std::string& path, const std::string& newPath) override;
    void remove(const std::string& path) override;
    void removeDirectory(const std::string& path) override;
    void format(const std::string& path) override;

private:
    fs::FS& filesystem_;
    std::string drive_;
    std::string mountPoint_;
    void ensureAvailable();
    std::string resolve(const std::string& path, bool allowRoot) const;
};

} // namespace ql32::platform::esp32
