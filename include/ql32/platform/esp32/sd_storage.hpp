#pragma once

#include "ql32/runtime/bulk_write_gate.hpp"
#include "ql32/runtime/storage.hpp"

#include <FS.h>
#include <string>

namespace ql32::platform::esp32 {

// Storage adapter for Arduino-ESP32 filesystems such as SD and LittleFS.
// Paths are relative to the mounted filesystem root.
class SdStorage final : public runtime::StorageService {
public:
    // The Arduino fs::FS base class has no capacity API, so the board supplies
    // a provider returning {total, used} bytes.
    using SpaceProvider = bool (*)(std::uint64_t& total, std::uint64_t& used);

    explicit SdStorage(fs::FS& filesystem, std::string drive = "SDC",
                       SpaceProvider spaceProvider = nullptr)
        : filesystem_(filesystem), drive_(std::move(drive)),
          spaceProvider_(spaceProvider) {}

    std::string readText(const std::string& path) override;
    std::unique_ptr<runtime::StorageReadHandle> openRead(
        const std::string& path) override;
    std::unique_ptr<runtime::StorageWriteHandle> openWrite(
        const std::string& path) override;
    void writeText(const std::string& path, const std::string& contents) override;
    void writeTextStream(const std::string& path,
                         const runtime::TextStreamSource& source) override;
    std::vector<runtime::FileEntry> list(const std::string& path) override;
    bool exists(const std::string& path) override;
    std::uintmax_t size(const std::string& path) override;
    void rename(const std::string& path, const std::string& newPath) override;
    void remove(const std::string& path) override;
    void createDirectory(const std::string& path) override;
    void removeDirectory(const std::string& path) override;
    void format(const std::string& path) override;
    runtime::StorageSpace space(const std::string& path) override;

    // Optional: told when a large write begins/ends (internal flash only).
    void setBulkWriteGate(runtime::BulkWriteGate* gate) noexcept { bulkWriteGate_ = gate; }

private:
    runtime::BulkWriteGate* bulkWriteGate_ = nullptr;
    fs::FS& filesystem_;
    std::string drive_;
    SpaceProvider spaceProvider_;
    void ensureAvailable();
    std::string resolve(const std::string& path, bool allowRoot) const;
};

} // namespace ql32::platform::esp32
