#pragma once
#include "ql32/runtime/storage.hpp"
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <sys/stat.h>
#include <limits.h>

namespace ql32::platform::posix {
// POSIX/VFS primitive cursor, including a bounded virtual SPIFFS hierarchy.
// One call performs at most one readdir/stat; folder deduplication uses repeated
// passes rather than a directory-sized set. These passes may return Pending.
class DirectoryCursor final : public runtime::DirectoryCursor {
public:
    DirectoryCursor(std::string path, std::string confinedRoot = {},
                    bool completeFlags = false, std::string virtualPrefix = {}, bool flat = false)
        : path_(std::move(path)), root_(std::move(confinedRoot)), prefix_(std::move(virtualPrefix)),
          completeFlags_(completeFlags), flat_(flat) {
        if (path_.size() > 1023 || prefix_.size() > 1023)
            throw runtime::Error(runtime::ErrorCode::InvalidPath, "directory path too long");
        full_.reserve(2048); candidate_.reserve(1023); lastFolder_.reserve(1023);
        open();
    }
    ~DirectoryCursor() override { if (directory_) ::closedir(directory_); }
    runtime::DirectoryCursorResult next(runtime::DirectoryEntry& result) override {
        using Result = runtime::DirectoryCursorResult;
        if (!directory_) return Result::End;
        errno = 0;
        dirent* entry = ::readdir(directory_);
        if (!entry) {
            if (errno) fail("directory read failed");
            ::closedir(directory_); directory_ = nullptr;
            if (flat_ && !candidate_.empty()) {
                result.name = candidate_; result.metadata = {}; result.metadata.directory = true;
                lastFolder_ = candidate_; candidate_.clear(); filesPass_ = false;
                open(); return Result::Entry;
            }
            if (flat_ && !prefix_.empty() && !seenPrefix_)
                throw runtime::Error(runtime::ErrorCode::NotFound, "virtual directory not found");
            return Result::End;
        }
        std::string_view raw(entry->d_name);
        if (raw == "." || raw == "..") return Result::Pending;
        if (raw.size() > 1023) throw runtime::Error(runtime::ErrorCode::InvalidPath, "directory filename too long");
        auto leaf = raw;
        if (flat_) {
            while (!leaf.empty() && leaf.front() == '/') leaf.remove_prefix(1);
            if (leaf.substr(0, prefix_.size()) != prefix_) return Result::Pending;
            leaf.remove_prefix(prefix_.size());
            if (leaf.empty()) return Result::Pending;
            seenPrefix_ = true;
            const auto slash = leaf.find('/');
            if (slash != std::string_view::npos) {
                const auto folder = leaf.substr(0, slash);
                if (!folder.empty() && folder > std::string_view(lastFolder_) &&
                    (candidate_.empty() || folder < std::string_view(candidate_))) candidate_.assign(folder.data(), folder.size());
                return Result::Pending;
            }
            if (!filesPass_) return Result::Pending;
        }
        full_ = path_; full_ += '/'; full_.append(raw.data(), raw.size());
        if (!root_.empty()) {
            char resolved[PATH_MAX];
            if (!::realpath(full_.c_str(), resolved)) fail("cannot resolve directory entry");
            const std::string_view target(resolved);
            if (target != root_ && (target.size() <= root_.size() || target.substr(0, root_.size()) != root_ ||
                (root_ != "/" && target[root_.size()] != '/')))
                throw runtime::Error(runtime::ErrorCode::InvalidPath, "directory link escapes storage root");
        }
        struct stat status{};
        if (::stat(full_.c_str(), &status)) fail("cannot stat directory entry");
        if (!S_ISREG(status.st_mode) && !S_ISDIR(status.st_mode))
            throw runtime::Error(runtime::ErrorCode::UnsupportedOperation, "unsupported directory entry type");
        result.name.assign(leaf.data(), leaf.size()); result.metadata = {};
        auto& metadata = result.metadata;
        metadata.directory = S_ISDIR(status.st_mode);
        metadata.size = metadata.directory ? 0 : static_cast<std::uint64_t>(status.st_size);
        metadata.known = 1;
        if (!leaf.empty() && leaf.front() == '.') metadata.flags |= 1;
        if (!(status.st_mode & (S_IWUSR | S_IWGRP | S_IWOTH))) metadata.flags |= 2;
        if (completeFlags_) metadata.known |= 4;
        std::tm stamp{};
        if (status.st_mtime > 0 && ::localtime_r(&status.st_mtime, &stamp) && stamp.tm_year >= -1899 && stamp.tm_year <= 8099) {
            metadata.modified = (((((static_cast<std::uint64_t>(stamp.tm_year + 1900) * 100 + stamp.tm_mon + 1) * 100 + stamp.tm_mday) * 100 + stamp.tm_hour) * 100 + stamp.tm_min) * 100 + stamp.tm_sec);
            metadata.known |= 2;
        }
        return Result::Entry;
    }
private:
    void open() { directory_ = ::opendir(path_.c_str()); if (!directory_) fail("cannot open directory"); }
    [[noreturn]] static void fail(const char* detail) {
        const auto code = errno == ENOENT ? runtime::ErrorCode::NotFound :
            errno == ENOTDIR ? runtime::ErrorCode::InvalidPath :
            errno == EACCES ? runtime::ErrorCode::PermissionDenied : runtime::ErrorCode::IoError;
        throw runtime::Error(code, detail);
    }
    DIR* directory_ = nullptr;
    std::string path_, root_, prefix_, full_, candidate_, lastFolder_;
    bool completeFlags_, flat_, filesPass_ = true, seenPrefix_ = false;
};
} // namespace ql32::platform::posix
