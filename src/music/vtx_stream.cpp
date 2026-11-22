#include "ql32/music/vtx_stream.hpp"

#include "ql32/music/lh5.hpp"
#include "ql32/runtime/error.hpp"

#include <algorithm>
#include <cstdio>
#include <string_view>

namespace ql32::music {
namespace {
constexpr char CachePrefix[] = ".q32v-";
constexpr std::uint8_t CacheVersion = 1;
constexpr std::size_t KeyHeadBytes = 4096;
constexpr std::size_t KeyTailBytes = 256;

std::uint16_t le16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0] | (static_cast<unsigned>(p[1]) << 8));
}
std::uint32_t le32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
        (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}
void put32(std::uint8_t* p, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) p[i] = static_cast<std::uint8_t>(v >> (8 * i));
}

std::uint32_t fnv(std::uint32_t hash, const std::uint8_t* bytes, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) hash = (hash ^ bytes[i]) * 16777619u;
    return hash;
}

void readFully(runtime::StorageReadHandle& file, std::uint64_t offset,
               std::uint8_t* destination, std::size_t count, const char* what) {
    std::size_t done = 0;
    while (done < count) {
        const auto received = file.readAt(offset + done, destination + done, count - done);
        if (received == 0)
            throw runtime::ServiceError(runtime::ServiceErrorCode::IoError,
                                        std::string("music: short read of ") + what);
        done += received;
    }
}

// Identifies the source so a changed song file invalidates its cache.
std::uint32_t sourceKey(runtime::StorageReadHandle& file) {
    const std::uint64_t size = file.size();
    std::vector<std::uint8_t> buffer(KeyHeadBytes);
    const auto head = static_cast<std::size_t>(std::min<std::uint64_t>(size, KeyHeadBytes));
    readFully(file, 0, buffer.data(), head, "VTX header");
    std::uint32_t key = fnv(2166136261u, buffer.data(), head);
    const auto tail = static_cast<std::size_t>(std::min<std::uint64_t>(size, KeyTailBytes));
    readFully(file, size - tail, buffer.data(), tail, "VTX tail");
    return fnv(key, buffer.data(), tail);
}

std::string cacheName(const std::string& root, const std::string& path) {
    std::uint32_t hash = 2166136261u;
    for (char c : path) {
        const auto folded = static_cast<std::uint8_t>(c >= 'A' && c <= 'Z' ? c + 32 : c);
        hash = fnv(hash, &folded, 1);
    }
    char hex[9];
    std::snprintf(hex, sizeof hex, "%08x", static_cast<unsigned>(hash));
    std::string name = root;
    if (name.empty() || name.back() != '/') name += '/';
    return name + CachePrefix + hex;
}

struct SourceInfo {
    VtxMetadata metadata;
    std::uint32_t decodeBytes = 0;
    std::size_t frames = 0;
    std::uint64_t compressedOffset = 0;
    std::uint64_t compressedSize = 0;
};

SourceInfo readSourceHeader(runtime::StorageReadHandle& file) {
    const std::uint64_t size = file.size();
    if (size < 20 || size > VtxSong::MaximumFileBytes)
        throw std::runtime_error("VTX file size is invalid or exceeds 256 KiB");
    std::uint8_t header[16];
    readFully(file, 0, header, sizeof header, "VTX header");
    const std::string_view signature(reinterpret_cast<const char*>(header), 2);
    if (signature != "ay" && signature != "ym")
        throw std::runtime_error("file is not a VTX AY/YM stream");
    SourceInfo info;
    info.metadata.ym2149 = signature == "ym";
    info.metadata.stereoMode = header[2] & 7;
    info.metadata.loopFrame = le16(header + 3);
    info.metadata.chipClockHz = le32(header + 5);
    info.metadata.playerHz = header[9];
    info.decodeBytes = le32(header + 12);
    if (info.metadata.chipClockHz == 0 || info.metadata.playerHz == 0 ||
        info.decodeBytes < 14 || info.decodeBytes > VtxSong::MaximumRegisterBytes)
        throw std::runtime_error("VTX register stream header is invalid");

    // Skip the five NUL-terminated metadata strings.
    std::uint64_t offset = 16;
    std::uint8_t chunk[128];
    for (int field = 0; field < 5; ++field) {
        for (bool terminated = false; !terminated;) {
            if (offset >= size) throw std::runtime_error("unterminated VTX metadata string");
            const auto count = static_cast<std::size_t>(
                std::min<std::uint64_t>(sizeof chunk, size - offset));
            readFully(file, offset, chunk, count, "VTX metadata");
            const auto* end = std::find(chunk, chunk + count, std::uint8_t{0});
            terminated = end != chunk + count;
            offset += static_cast<std::uint64_t>(end - chunk) + (terminated ? 1 : 0);
        }
    }
    if (offset >= size) throw std::runtime_error("VTX file has no LHA member");

    info.compressedOffset = offset;
    info.compressedSize = size - offset;
    if (info.compressedSize >= 24) {
        std::uint8_t lha[24];
        readFully(file, offset, lha, sizeof lha, "VTX LHA header");
        if (std::string_view(reinterpret_cast<const char*>(lha + 2), 5) == "-lh5-") {
            const std::uint64_t headerSize = static_cast<std::uint64_t>(lha[0]) + 2;
            const std::uint32_t packed = le32(lha + 7);
            if (headerSize < 24 || headerSize > info.compressedSize ||
                le32(lha + 11) != info.decodeBytes ||
                packed > info.compressedSize - headerSize)
                throw std::runtime_error("VTX LHA member header is invalid");
            info.compressedOffset += headerSize;
            info.compressedSize = packed;
        }
    }
    info.frames = info.decodeBytes / 14;
    return info;
}

struct DecodeFailure {
    std::string message;
};

class TranscodeSource final : public runtime::TextStreamSource {
public:
    TranscodeSource(runtime::StorageReadHandle& file, const SourceInfo& info,
                    std::uint32_t srcSize, std::uint32_t key)
        : file_(file), info_(info), srcSize_(srcSize), key_(key) {}

    void writeTo(runtime::TextStreamSink& sink) const override {
        std::uint8_t header[VtxPlaneFile::HeaderBytes] = {'Q', '3', '2', 'V', CacheVersion};
        header[5] = info_.metadata.ym2149 ? 1 : 0;
        header[6] = info_.metadata.stereoMode;
        header[7] = info_.metadata.playerHz;
        put32(header + 8, static_cast<std::uint32_t>(info_.frames));
        put32(header + 12, info_.metadata.chipClockHz);
        header[16] = static_cast<std::uint8_t>(info_.metadata.loopFrame);
        header[17] = static_cast<std::uint8_t>(info_.metadata.loopFrame >> 8);
        put32(header + 20, srcSize_);
        put32(header + 24, key_);
        sink.write({reinterpret_cast<const char*>(header), sizeof header});

        std::uint64_t consumed = 0;
        const Lh5ByteReader reader = [&](std::uint8_t* destination, std::size_t capacity) {
            const auto count = static_cast<std::size_t>(
                std::min<std::uint64_t>(capacity, info_.compressedSize - consumed));
            if (count == 0) return std::size_t{0};
            readFully(file_, info_.compressedOffset + consumed, destination, count,
                      "VTX stream");
            consumed += count;
            return count;
        };
        // A trailing partial frame (some encoders write one) is dropped, as the
        // in-RAM decoder ignores it.
        std::size_t remaining = info_.frames * 14;
        const Lh5OutputSink output = [&](const std::uint8_t* bytes, std::size_t count) {
            count = std::min(count, remaining);
            if (count == 0) return;
            sink.write({reinterpret_cast<const char*>(bytes), count});
            remaining -= count;
        };
        const char* error = nullptr;
        if (!decodeLh5Stream(reader, info_.decodeBytes, output, &error))
            throw DecodeFailure{error ? error : "VTX LHA5 decode failed"};
    }

private:
    runtime::StorageReadHandle& file_;
    const SourceInfo& info_;
    std::uint32_t srcSize_;
    std::uint32_t key_;
};

std::shared_ptr<VtxPlaneFile> openCache(runtime::StorageService& storage,
                                        const std::string& cachePath,
                                        std::uint32_t srcSize, std::uint32_t key,
                                        VtxMetadata& metadata, std::size_t& frames) {
    try {
        if (!storage.exists(cachePath)) return nullptr;
        auto file = storage.openRead(cachePath);
        std::uint8_t header[VtxPlaneFile::HeaderBytes];
        if (file->size() < sizeof header) return nullptr;
        readFully(*file, 0, header, sizeof header, "VTX cache header");
        if (std::string_view(reinterpret_cast<const char*>(header), 4) != "Q32V" ||
            header[4] != CacheVersion || le32(header + 20) != srcSize ||
            le32(header + 24) != key)
            return nullptr;
        frames = le32(header + 8);
        if (frames == 0 || file->size() != sizeof header + frames * 14ull) return nullptr;
        metadata = {};
        metadata.ym2149 = header[5] != 0;
        metadata.stereoMode = header[6];
        metadata.playerHz = header[7];
        metadata.chipClockHz = le32(header + 12);
        metadata.loopFrame = le16(header + 16);
        if (metadata.chipClockHz == 0 || metadata.playerHz == 0) return nullptr;
        return std::make_shared<VtxPlaneFile>(std::move(file), frames, metadata.loopFrame);
    } catch (const std::exception&) {
        return nullptr;
    }
}

void evictOtherCaches(runtime::StorageService& storage, const std::string& root,
                      const std::string& keep) {
    try {
        std::string directory = root;
        if (directory.empty() || directory.back() != '/') directory += '/';
        for (const auto& entry : storage.list(directory)) {
            if (entry.isDirectory || entry.name.find(CachePrefix) == std::string::npos)
                continue;
            const auto slash = entry.name.find_last_of('/');
            const std::string name = directory +
                (slash == std::string::npos ? entry.name : entry.name.substr(slash + 1));
            if (name != keep) storage.remove(name);
        }
    } catch (const std::exception&) {
    }
}
} // namespace

VtxPlaneFile::VtxPlaneFile(std::unique_ptr<runtime::StorageReadHandle> file,
                           std::size_t frameCount, std::size_t loopFrame)
    : file_(std::move(file)), frameCount_(frameCount), loopFrame_(loopFrame) {
    for (std::size_t plane = 0; plane < current_.size(); ++plane) {
        current_[plane].resize(WindowFrames);
        next_[plane].resize(WindowFrames);
        if (!load(plane, 0, current_[plane], currentWindow_[plane]))
            throw runtime::ServiceError(runtime::ServiceErrorCode::IoError,
                                        "music: cannot read VTX cache");
    }
}

bool VtxPlaneFile::load(std::size_t plane, std::size_t start,
                        std::vector<std::uint8_t>& destination, Window& window) {
    const std::size_t end = std::min(frameCount_, start + WindowFrames);
    try {
        std::lock_guard lock(fileMutex_);
        readFully(*file_, HeaderBytes + plane * frameCount_ + start, destination.data(),
                  end - start, "VTX cache");
    } catch (const std::exception&) {
        return false;
    }
    window = {start, end - start};
    return true;
}

std::array<std::uint8_t, 14> VtxPlaneFile::registers(std::size_t frame) {
    std::array<std::uint8_t, 14> result{};
    if (failed_ || frame >= frameCount_) return result;
    lastFrame_.store(frame, std::memory_order_relaxed);
    for (std::size_t plane = 0; plane < result.size(); ++plane) {
        std::unique_lock lock(stateMutex_);
        auto& window = currentWindow_[plane];
        if (frame < window.start || frame >= window.start + window.length) {
            auto& ahead = nextWindow_[plane];
            if (ahead.length != 0 && frame >= ahead.start &&
                frame < ahead.start + ahead.length) {
                current_[plane].swap(next_[plane]);
                window = ahead;
                ahead = {};
            } else {
                // The read-ahead has not caught up (a seek, or a slow storage
                // read): read synchronously so playback stays correct.
                lock.unlock();
                Window loaded;
                std::vector<std::uint8_t>& spare = scratch_;
                spare.resize(WindowFrames);
                if (!load(plane, frame, spare, loaded)) {
                    failed_ = true;
                    return result;
                }
                lock.lock();
                current_[plane].swap(spare);
                window = loaded;
            }
        }
        result[plane] = current_[plane][frame - window.start];
    }
    return result;
}

void VtxPlaneFile::run() noexcept {
    if (failed_) return;
    for (std::size_t plane = 0; plane < current_.size(); ++plane) {
        std::size_t start;
        {
            std::lock_guard lock(stateMutex_);
            const auto& window = currentWindow_[plane];
            const std::size_t end = window.start + window.length;
            // Read ahead once the current window is half consumed, wrapping to
            // the loop point after the final window.
            const std::size_t position = lastFrame_.load(std::memory_order_relaxed);
            if (position >= window.start && position < window.start + window.length / 2)
                continue;
            start = end < frameCount_ ? end : loopFrame_;
            if (start >= frameCount_) continue;
            if (nextWindow_[plane].length != 0 && nextWindow_[plane].start == start) continue;
        }
        Window loaded;
        std::vector<std::uint8_t> buffer(WindowFrames);
        if (!load(plane, start, buffer, loaded)) continue;
        std::lock_guard lock(stateMutex_);
        next_[plane].swap(buffer);
        nextWindow_[plane] = loaded;
    }
}

VtxSong openVtxStreamed(runtime::StorageService& storage, const std::string& path,
                        const std::vector<std::string>& cacheRoots, VtxStreamInfo* info) {
    if (cacheRoots.empty())
        throw runtime::ServiceError(runtime::ServiceErrorCode::Unavailable,
                                    "music: no VTX cache location configured");
    auto source = storage.openRead(path);
    const SourceInfo header = readSourceHeader(*source);
    const auto srcSize = static_cast<std::uint32_t>(source->size());
    const std::uint32_t key = sourceKey(*source);

    VtxMetadata metadata;
    std::size_t frames = 0;
    const auto finish = [&](std::shared_ptr<VtxPlaneFile> planes, const std::string& cache,
                            bool hit) {
        if (info) {
            info->cachePath = cache;
            info->cacheHit = hit;
        }
        return VtxSong::fromPlaneFile(std::move(metadata), std::move(planes), frames);
    };
    for (const auto& root : cacheRoots) {
        const auto cache = cacheName(root, path);
        if (auto planes = openCache(storage, cache, srcSize, key, metadata, frames))
            return finish(std::move(planes), cache, true);
    }

    std::string lastError = "no writable location";
    for (const auto& root : cacheRoots) {
        const auto cache = cacheName(root, path);
        const TranscodeSource transcode(*source, header, srcSize, key);
        for (int attempt = 0; attempt < 2; ++attempt) {
            try {
                storage.writeTextStreamAtomic(cache, transcode);
                if (auto planes = openCache(storage, cache, srcSize, key, metadata, frames))
                    return finish(std::move(planes), cache, false);
                lastError = "cache verification failed on " + root;
            } catch (const DecodeFailure& failure) {
                throw std::runtime_error(failure.message);
            } catch (const std::exception& error) {
                lastError = error.what();
            }
            // Out of space: drop caches of other songs once and retry.
            if (attempt == 0) evictOtherCaches(storage, root, cache);
            else break;
        }
    }
    throw runtime::ServiceError(runtime::ServiceErrorCode::IoError,
                                "music: cannot write VTX cache: " + lastError);
}

} // namespace ql32::music
