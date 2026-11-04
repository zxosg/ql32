#pragma once

#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#define FILE_READ "r"
#define FILE_WRITE "w"
#define FILE_APPEND "a"

namespace fs {

class FS;

class File {
public:
    File() = default;
    File(FS* filesystem, std::string path, bool directory = false);

    explicit operator bool() const { return valid_; }
    bool isDirectory() const { return directory_; }
    std::size_t size() const;
    int available() const;
    int read();
    std::size_t read(std::uint8_t* buffer, std::size_t length);
    std::size_t write(const std::uint8_t* buffer, std::size_t length);
    bool seek(std::uint32_t offset, int = 0);
    void flush() {}
    void close() { valid_ = false; }
    const char* name() const { return name_.c_str(); }
    const char* path() const { return path_.c_str(); }
    File openNextFile();

private:
    FS* filesystem_ = nullptr;
    std::string path_;
    std::string name_;
    std::size_t cursor_ = 0;
    std::size_t childCursor_ = 0;
    bool directory_ = false;
    bool valid_ = false;
};

class FS {
public:
    FS() { directories_.insert("/"); }

    File open(const char* path, const char* mode = FILE_READ) {
        if (!mounted_) return {};
        const std::string key(path);
        if (directories_.contains(key)) return File(this, key, true);
        auto found = files_.find(key);
        if (std::string(mode) == FILE_WRITE) {
            if (!directories_.contains(parent(key))) return {};
            auto& contents = files_[key];
            contents.clear();
            return File(this, key);
        }
        if (std::string(mode) == FILE_APPEND) {
            if (!directories_.contains(parent(key))) return {};
            files_.try_emplace(key);
            File file(this, key);
            file.seek(static_cast<std::uint32_t>(files_[key].size()));
            return file;
        }
        return found == files_.end() ? File{} : File(this, key);
    }

    bool isDirectory(const std::string& path) const {
        return directories_.contains(path);
    }
    bool exists(const char* path) const {
        const std::string key(path);
        return files_.contains(key) || directories_.contains(key);
    }
    bool rename(const char* from, const char* to) {
        const auto found = files_.find(from);
        if (found == files_.end() || exists(to) || !directories_.contains(parent(to))) return false;
        files_.emplace(to, std::move(found->second));
        files_.erase(found);
        return true;
    }
    bool remove(const char* path) { return files_.erase(path) != 0; }
    bool rmdir(const char* path) {
        const std::string key(path);
        if (!directories_.contains(key) || key == "/" || !children(key).empty()) return false;
        return directories_.erase(key) != 0;
    }
    void setMounted(bool mounted) { mounted_ = mounted; }
    std::string& contents(const std::string& path) { return files_[path]; }
    std::vector<std::string> children(const std::string& path) const {
        const std::string prefix = path == "/" ? "/" : path + "/";
        std::vector<std::string> result;
        for (const auto& dir : directories_)
            if (dir != path && dir.rfind(prefix, 0) == 0 &&
                dir.find('/', prefix.size()) == std::string::npos)
                result.push_back(dir);
        for (const auto& [file, _] : files_)
            if (file.rfind(prefix, 0) == 0 &&
                file.find('/', prefix.size()) == std::string::npos)
                result.push_back(file);
        std::sort(result.begin(), result.end());
        return result;
    }
    std::string basename(const std::string& path) const {
        const auto slash = path.find_last_of('/');
        return slash == std::string::npos ? path : path.substr(slash + 1);
    }

private:
    friend class File;
    static std::string parent(const std::string& path) {
        const auto slash = path.find_last_of('/');
        return slash == 0 ? "/" : path.substr(0, slash);
    }
    std::map<std::string, std::string> files_;
    std::set<std::string> directories_;
    bool mounted_ = true;
};

inline File::File(FS* filesystem, std::string path, bool directory)
    : filesystem_(filesystem), path_(std::move(path)),
      name_(filesystem->basename(path_)), directory_(directory), valid_(true) {}

inline std::size_t File::size() const {
    const auto found = filesystem_->files_.find(path_);
    return found == filesystem_->files_.end() ? 0 : found->second.size();
}

inline int File::available() const {
    return static_cast<int>(size() - cursor_);
}

inline int File::read() {
    const auto found = filesystem_->files_.find(path_);
    if (found == filesystem_->files_.end() || cursor_ >= found->second.size()) return -1;
    return static_cast<unsigned char>(found->second[cursor_++]);
}

inline std::size_t File::read(std::uint8_t* buffer, std::size_t length) {
    const auto found = filesystem_->files_.find(path_);
    if (found == filesystem_->files_.end()) return 0;
    const std::size_t count = std::min(length, found->second.size() - cursor_);
    std::copy_n(found->second.data() + cursor_, count,
                reinterpret_cast<char*>(buffer));
    cursor_ += count;
    return count;
}

inline std::size_t File::write(const std::uint8_t* buffer, std::size_t length) {
    auto& contents = filesystem_->files_[path_];
    if (cursor_ > contents.size()) contents.resize(cursor_, '\0');
    if (cursor_ + length > contents.size()) contents.resize(cursor_ + length);
    std::copy_n(reinterpret_cast<const char*>(buffer), length, contents.data() + cursor_);
    cursor_ += length;
    return length;
}

inline bool File::seek(std::uint32_t offset, int) {
    cursor_ = offset;
    return true;
}

inline File File::openNextFile() {
    const auto entries = filesystem_->children(path_);
    if (childCursor_ >= entries.size()) return {};
    const std::string child = entries[childCursor_++];
    return File(filesystem_, child, filesystem_->isDirectory(child));
}

} // namespace fs
