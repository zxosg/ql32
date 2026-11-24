#pragma once
#include "ql32/runtime/storage.hpp"
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

namespace ql32::runtime {
struct DirectoryLimits {
    std::size_t maxEntries = 256;
    std::size_t snapshotBytes = 14 * 1024;
    std::size_t perSessionBytes = 16 * 1024;
    std::size_t systemBytes = 32 * 1024;
    std::size_t maxChannels = 2;
    std::size_t batch = 16;
};
// Runtime-owned listing. A single allocation contains fixed metadata records
// and packed filenames; a stream never materializes the directory in an array.
class DirectoryStream {
public:
    DirectoryStream(std::unique_ptr<DirectoryCursor> cursor, std::string pattern,
                    std::uint32_t options, const DirectoryLimits& limits);
    ~DirectoryStream();
    bool prepare(); // bounded collection/sort work; true when OPEN can publish
    bool ready();   // bounded filtering work; true when next/EOF is known
    std::string_view nextName() const;
    void commitRead() noexcept; // only after BASIC result allocation succeeds
    bool atEnd() const noexcept;
    double info(unsigned selector, bool singlePrecision) const;
    std::string infoText(unsigned selector) const;
    std::size_t snapshotBytes() const noexcept { return allocationBytes_; }
private:
    struct Record { DirectoryMetadata metadata; std::uint16_t offset = 0; };
    std::unique_ptr<DirectoryCursor> cursor_;
    std::string pattern_;
    DirectoryEntry lookahead_;
    DirectoryMetadata current_;
    bool hasCurrent_ = false, hasNext_ = false, end_ = false, prepared_ = false;
    std::uint32_t options_;
    DirectoryLimits limits_;
    std::unique_ptr<char[]> snapshot_;
    Record* records_ = nullptr;
    char* names_ = nullptr;
    std::size_t allocationBytes_ = 0, namesUsed_ = 0;
    std::size_t count_ = 0, position_ = 0, buildHeap_ = 0, sortEnd_ = 0;
    bool collecting_ = true;
    bool matches(const DirectoryEntry&) const;
    bool less(const Record&, const Record&) const;
    const char* name(const Record& record) const noexcept { return names_ + record.offset; }
    void sift(std::size_t root, std::size_t end);
};
bool directoryWildcardMatch(std::string_view name, std::string_view pattern) noexcept;
std::uint64_t packDirectoryTime(int year, int month, int day, int hour, int minute, int second) noexcept;
} // namespace ql32::runtime
