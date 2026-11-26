#include "ql32/runtime/directory.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <new>

namespace ql32::runtime {
namespace {
unsigned char fold(unsigned char c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; }
std::size_t scalarEnd(std::string_view text, std::size_t at) {
    if (at >= text.size()) return at;
    ++at; while (at < text.size() && (static_cast<unsigned char>(text[at]) & 0xc0) == 0x80) ++at;
    return at;
}
int compareNames(std::string_view a, std::string_view b) {
    for (std::size_t i = 0; i < std::min(a.size(), b.size()); ++i) {
        const auto left = fold(a[i]), right = fold(b[i]);
        if (left != right) return left < right ? -1 : 1;
    }
    if (a.size() != b.size()) return a.size() < b.size() ? -1 : 1;
    return 0;
}
[[noreturn]] void quota() { throw Error(ErrorCode::TaskLimit, "directory snapshot budget exceeded"); }
}
bool directoryWildcardMatch(std::string_view text, std::string_view pattern) noexcept {
    std::size_t i = 0, p = 0, star = std::string_view::npos, retry = 0;
    while (i < text.size()) {
        if (p < pattern.size() && pattern[p] == '*') { star = p++; retry = i; }
        else if (p < pattern.size() && pattern[p] == '?') { ++p; i = scalarEnd(text, i); }
        else if (p < pattern.size() && fold(pattern[p]) == fold(text[i])) { ++p; ++i; }
        else if (star != std::string_view::npos && retry < text.size()) { retry = scalarEnd(text, retry); i = retry; p = star + 1; }
        else return false;
    }
    while (p < pattern.size() && pattern[p] == '*') ++p;
    return p == pattern.size();
}
std::uint64_t packDirectoryTime(int y, int m, int d, int h, int n, int s) noexcept {
    if (y < 1 || y > 9999 || m < 1 || m > 12 || d < 1 || d > 31 || h < 0 || h > 23 || n < 0 || n > 59 || s < 0 || s > 60) return 0;
    return (((((static_cast<std::uint64_t>(y) * 100 + m) * 100 + d) * 100 + h) * 100 + n) * 100 + s);
}
DirectoryStream::DirectoryStream(std::unique_ptr<DirectoryCursor> cursor, std::string pattern,
                                 std::uint32_t options, const DirectoryLimits& limits)
    : cursor_(std::move(cursor)), pattern_(std::move(pattern)), options_(options), limits_(limits) {
    const auto sort = options & 126u;
    if ((options & ~127u) || (sort && (sort & (sort - 1))))
        throw Error(ErrorCode::InvalidArgument, "invalid directory options");
    if (!cursor_ || pattern_.size() > 255 || pattern_.find_first_of("/\\:") != std::string::npos)
        throw Error(ErrorCode::InvalidArgument, "invalid directory pattern");
    if (!limits_.batch || !limits_.maxEntries || limits_.maxEntries > 256 || limits_.snapshotBytes > 65535)
        throw Error(ErrorCode::InvalidArgument, "invalid directory limits");
    lookahead_.name.reserve(1023);
    if (!sort) return;
    if (limits_.snapshotBytes < sizeof(Record) * RecordsPerPage + NamePageBytes) quota();
    sorted_ = true;
}
DirectoryStream::~DirectoryStream() = default;
bool DirectoryStream::matches(const DirectoryEntry& entry) const {
    if (entry.name == "." || entry.name == "..") return false;
    if (entry.name.empty() || entry.name.size() > 1023 || entry.name.find_first_of("/\\") != std::string::npos)
        throw Error(ErrorCode::InvalidPath, "directory entry exceeds portable leaf-name limits");
    return entry.metadata.directory ? (options_ & 1) != 0 : directoryWildcardMatch(entry.name, pattern_);
}
bool DirectoryStream::prepare() {
    if (prepared_) return true;
    if (!sorted_) {
        prepared_ = true;
        if (ready()) return true;
        prepared_ = false; return false;
    }
    std::size_t work = limits_.batch;
    while (collecting_ && work--) {
        const auto result = cursor_->next(lookahead_);
        if (result == DirectoryCursorResult::Pending) continue;
        if (result == DirectoryCursorResult::End) {
            cursor_.reset();
            std::string{}.swap(lookahead_.name);
            std::string{}.swap(pattern_);
            collecting_ = false; buildHeap_ = count_ / 2; sortEnd_ = count_; break;
        }
        if (!matches(lookahead_)) continue;
        const auto nameBytes = lookahead_.name.size() + 1;
        if (count_ == limits_.maxEntries || nameBytes > NamePageBytes) quota();
        constexpr auto recordPageBytes = sizeof(Record) * RecordsPerPage;
        if (count_ == recordPageCount_ * RecordsPerPage) {
            if (recordPageCount_ == MaxPages || allocationBytes_ + recordPageBytes > limits_.snapshotBytes) quota();
            recordPages_[recordPageCount_].reset(new Record[RecordsPerPage]);
            ++recordPageCount_;
            allocationBytes_ += recordPageBytes;
        }
        if (nameFill_ + nameBytes > NamePageBytes) {
            if (namePageCount_ == MaxPages || allocationBytes_ + NamePageBytes > limits_.snapshotBytes) quota();
            namePages_[namePageCount_].reset(new char[NamePageBytes]);
            ++namePageCount_; nameFill_ = 0;
            allocationBytes_ += NamePageBytes;
        }
        const auto offset = (namePageCount_ - 1) * NamePageBytes + nameFill_;
        std::memcpy(namePages_[namePageCount_ - 1].get() + nameFill_, lookahead_.name.c_str(), nameBytes);
        nameFill_ += nameBytes;
        record(count_) = Record{lookahead_.metadata, static_cast<std::uint16_t>(offset)};
        ++count_;
    }
    if (collecting_) return false;
    // Each sift has at most log2(256) comparisons; no recursive or O(n) scratch.
    for (std::size_t step = 0; step < limits_.batch; ++step) {
        if (buildHeap_) sift(--buildHeap_, sortEnd_);
        else if (sortEnd_ > 1) { std::swap(record(0), record(--sortEnd_)); sift(0, sortEnd_); }
        else { prepared_ = true; end_ = count_ == 0; return true; }
    }
    return false;
}
void DirectoryStream::sift(std::size_t root, std::size_t end) {
    for (;;) {
        auto child = root * 2 + 1;
        if (child >= end) return;
        if (child + 1 < end && less(record(child), record(child + 1))) ++child;
        if (!less(record(root), record(child))) return;
        std::swap(record(root), record(child)); root = child;
    }
}
bool DirectoryStream::less(const Record& a, const Record& b) const {
    if (a.metadata.directory != b.metadata.directory) return a.metadata.directory;
    const auto mode = options_ & 126u;
    if (mode >= 8) {
        const bool date = mode == 8 || mode == 16;
        const auto known = date ? 2 : 1;
        const bool ak = a.metadata.known & known, bk = b.metadata.known & known;
        if (ak != bk) return ak;
        const auto av = date ? a.metadata.modified : a.metadata.size;
        const auto bv = date ? b.metadata.modified : b.metadata.size;
        if (ak && av != bv) return (mode == 16 || mode == 64) ? av > bv : av < bv;
    }
    const int order = compareNames(name(a), name(b));
    if (order) return mode == 4 ? order > 0 : order < 0;
    return std::strcmp(name(a), name(b)) < 0;
}
bool DirectoryStream::ready() {
    if (!prepared_) throw std::logic_error("directory published before preparation");
    if (sorted_) return true;
    if (hasNext_ || end_) return true;
    for (std::size_t i = 0; i < limits_.batch; ++i) {
        const auto result = cursor_->next(lookahead_);
        if (result == DirectoryCursorResult::Pending) continue;
        if (result == DirectoryCursorResult::End) { end_ = true; cursor_.reset(); return true; }
        if (matches(lookahead_)) { hasNext_ = true; return true; }
    }
    return false;
}
bool DirectoryStream::atEnd() const noexcept { return sorted_ ? position_ == count_ : end_; }
std::string_view DirectoryStream::nextName() const {
    if (atEnd()) throw Error(ErrorCode::IoError, "end of directory");
    if (sorted_) return name(const_cast<DirectoryStream*>(this)->record(position_));
    if (!hasNext_) throw std::logic_error("directory result is not ready");
    return lookahead_.name;
}
void DirectoryStream::commitRead() noexcept {
    current_ = sorted_ ? record(position_++).metadata : lookahead_.metadata;
    hasCurrent_ = true; hasNext_ = false;
}
double DirectoryStream::info(unsigned selector, bool single) const {
    if (!hasCurrent_) throw Error(ErrorCode::InvalidArgument, "read a directory entry before querying metadata");
    switch (selector) {
    case 1: return current_.directory ? 1 : 0;
    case 2: {
        const auto size = current_.directory || !(current_.known & 1) ? 0 : current_.size;
        if (size > (single ? 16777216ULL : 9007199254740992ULL))
            throw Error(ErrorCode::NumericOverflow, "directory size exceeds exact numeric range");
        return static_cast<double>(size);
    }
    case 3: return current_.flags;
    case 4: return current_.known;
    default: throw Error(ErrorCode::InvalidArgument, "invalid numeric DIRINFO selector");
    }
}
std::string DirectoryStream::infoText(unsigned selector) const {
    if (!hasCurrent_ || selector != 5) throw Error(ErrorCode::InvalidArgument, "invalid DIRINFO$ state or selector");
    if (!(current_.known & 2) || !current_.modified) return {};
    auto stamp = current_.modified;
    const auto s = stamp % 100; stamp /= 100; const auto m = stamp % 100; stamp /= 100;
    const auto h = stamp % 100; stamp /= 100; const auto d = stamp % 100; stamp /= 100;
    const auto month = stamp % 100; stamp /= 100;
    char text[32]; std::snprintf(text, sizeof text, "%04u-%02u-%02uT%02u:%02u:%02u",
        unsigned(stamp), unsigned(month), unsigned(d), unsigned(h), unsigned(m), unsigned(s)); return text;
}
} // namespace ql32::runtime
