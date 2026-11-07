#pragma once

#include <atomic>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "ql32/runtime/error.hpp"

namespace ql32::runtime {

struct FileEntry {
    std::string name;
    std::uintmax_t size = 0;
    bool isDirectory = false;
};

class TextStreamSink {
public:
    virtual ~TextStreamSink() = default;
    virtual void write(std::string_view chunk) = 0;
};

class TextStreamSource {
public:
    virtual ~TextStreamSource() = default;
    virtual void writeTo(TextStreamSink& sink) const = 0;
};

class StorageService {
public:
    virtual ~StorageService() = default;
    virtual std::string readText(const std::string& path) = 0;
    virtual void writeText(const std::string& path, const std::string& contents) = 0;
    // Adapters override this to stream chunks without assembling the complete
    // file in heap memory. The fallback keeps simple host/test adapters usable.
    virtual void writeTextStream(const std::string& path,
                                const TextStreamSource& source) {
        class Collector final : public TextStreamSink {
        public:
            void write(std::string_view chunk) override { contents.append(chunk); }
            std::string contents;
        } collector;
        source.writeTo(collector);
        writeText(path, collector.contents);
    }

    // Replace the destination only after the streamed temporary file is
    // complete. If installing it fails, attempt to restore the prior file.
    void writeTextStreamAtomic(const std::string& path,
                               const TextStreamSource& source) {
        static std::atomic<std::uint64_t> sequence{0};
        std::string parent;
        const auto separator = path.find_last_of('/');
        if (separator != std::string::npos) {
            parent = path.substr(0, separator + 1);
        } else {
            const auto drive = path.find(':');
            if (drive != std::string::npos) parent = path.substr(0, drive + 1);
        }
        std::string temporary;
        std::string backup;
        do {
            const std::string id = std::to_string(
                sequence.fetch_add(1, std::memory_order_relaxed) + 1);
            // Keep generated basenames short for filesystems such as SPIFFS,
            // which impose a small maximum path length.
            temporary = parent + ".q32-t" + id;
            backup = parent + ".q32-b" + id;
        } while (exists(temporary) || exists(backup));

        try {
            writeTextStream(temporary, source);
        } catch (...) {
            try { if (exists(temporary)) remove(temporary); } catch (...) {}
            throw;
        }
        const bool hadOriginal = exists(path);
        try {
            if (hadOriginal) rename(path, backup);
            rename(temporary, path);
        } catch (...) {
            if (hadOriginal) {
                try { rename(backup, path); } catch (...) {}
            }
            try { if (exists(temporary)) remove(temporary); } catch (...) {}
            throw;
        }
        if (hadOriginal) {
            try { remove(backup); } catch (...) {}
        }
    }
    virtual std::vector<FileEntry> list(const std::string& path) = 0;
    virtual bool exists(const std::string& path) = 0;
    virtual std::uintmax_t size(const std::string& path) = 0;
    virtual void rename(const std::string& path, const std::string& newPath) = 0;
    virtual void remove(const std::string& path) = 0;
    virtual void removeDirectory(const std::string&) {
        throw ServiceError(ServiceErrorCode::Unavailable, "storage: directory removal unavailable");
    }
    virtual void format(const std::string&) {
        throw ServiceError(ServiceErrorCode::Unavailable, "storage: format unavailable");
    }
};

} // namespace ql32::runtime
