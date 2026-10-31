#pragma once

#include "ql32/runtime/storage.hpp"

#include <FS.h>

namespace ql32::platform::esp32 {

// Storage adapter for Arduino-ESP32 filesystems such as SD and LittleFS.
// Paths are relative to the mounted filesystem root.
class SdStorage final : public runtime::StorageService {
public:
    explicit SdStorage(fs::FS& filesystem) : filesystem_(filesystem) {}

    std::string readText(const std::string& path) override;
    void writeText(const std::string& path, const std::string& contents) override;
    std::vector<runtime::FileEntry> list(const std::string& path) override;

private:
    fs::FS& filesystem_;
    void ensureAvailable();
    static std::string resolve(const std::string& path, bool allowRoot);
};

} // namespace ql32::platform::esp32
