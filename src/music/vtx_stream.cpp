#include "ql32/music/vtx_stream.hpp"

#include "ql32/music/lh5.hpp"
#include "ql32/runtime/error.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <new>
#include <string_view>

namespace ql32::music {
namespace {
constexpr char CachePrefix[] = ".q32v-";
constexpr std::uint8_t CacheVersion = 2;
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

// Byte-oriented LZ for cache windows: a control byte below 0x80 starts a run of
// (c + 1) literals; otherwise it is a match of (c & 0x7f) + 3 bytes at a distance
// of one byte (1..127) or two bytes (0x80 | high, low; up to 0x7fff).
void lzEncode(const std::uint8_t* in, std::size_t count, std::vector<std::uint8_t>& out) {
    out.clear();
    std::uint16_t table[1024];
    std::fill(std::begin(table), std::end(table), std::uint16_t{0xffff});
    const auto hashAt = [&](std::size_t i) {
        const std::uint32_t v = in[i] | (in[i + 1] << 8) | (static_cast<std::uint32_t>(in[i + 2]) << 16);
        return static_cast<std::size_t>((v * 2654435761u) >> 22);
    };
    std::size_t literals = 0;
    const auto flush = [&](std::size_t end) {
        while (literals < end) {
            const std::size_t run = std::min<std::size_t>(128, end - literals);
            out.push_back(static_cast<std::uint8_t>(run - 1));
            out.insert(out.end(), in + literals, in + literals + run);
            literals += run;
        }
    };
    std::size_t i = 0;
    while (i + 3 <= count) {
        const std::size_t h = hashAt(i);
        const std::uint16_t candidate = table[h];
        table[h] = static_cast<std::uint16_t>(i);
        if (candidate != 0xffff && i - candidate <= 0x7fff &&
            in[candidate] == in[i] && in[candidate + 1] == in[i + 1] &&
            in[candidate + 2] == in[i + 2]) {
            std::size_t length = 3;
            while (i + length < count && length < 130 && in[candidate + length] == in[i + length])
                ++length;
            flush(i);
            out.push_back(static_cast<std::uint8_t>(0x80 | (length - 3)));
            const std::size_t distance = i - candidate;
            if (distance < 128) {
                out.push_back(static_cast<std::uint8_t>(distance));
            } else {
                out.push_back(static_cast<std::uint8_t>(0x80 | (distance >> 8)));
                out.push_back(static_cast<std::uint8_t>(distance));
            }
            for (std::size_t k = 1; k < length && i + k + 3 <= count; ++k)
                table[hashAt(i + k)] = static_cast<std::uint16_t>(i + k);
            i += length;
            literals = i;
        } else {
            ++i;
        }
    }
    flush(count);
}

bool lzDecode(const std::uint8_t* in, std::size_t inLength, std::uint8_t* out,
              std::size_t outLength) noexcept {
    std::size_t ip = 0, op = 0;
    while (op < outLength) {
        if (ip >= inLength) return false;
        const unsigned control = in[ip++];
        if (control < 0x80) {
            const std::size_t run = control + 1;
            if (ip + run > inLength || op + run > outLength) return false;
            std::memcpy(out + op, in + ip, run);
            ip += run;
            op += run;
        } else {
            const std::size_t length = (control & 0x7f) + 3;
            if (ip >= inLength) return false;
            std::size_t distance = in[ip++];
            if (distance & 0x80) {
                if (ip >= inLength) return false;
                distance = ((distance & 0x7f) << 8) | in[ip++];
            }
            if (distance == 0 || distance > op || op + length > outLength) return false;
            for (std::size_t k = 0; k < length; ++k, ++op) out[op] = out[op - distance];
        }
    }
    return ip == inLength;
}

// Cuts the source byte stream into windows and writes them as cache blobs. Planar
// input arrives plane after plane; window-major input is frame-major.
class WindowEncoder {
public:
    WindowEncoder(runtime::TextStreamSink& sink, std::size_t frames, std::size_t planes,
                  bool windowMajor, std::size_t loopFrame)
        : sink_(sink), frames_(frames), planes_(planes), windowMajor_(windowMajor),
          loopWindow_(loopFrame < frames ? loopFrame / VtxPlaneFile::WindowFrames : kNone),
          position_(VtxPlaneFile::HeaderBytes) {
        layout_.start[0] = VtxPlaneFile::HeaderBytes;
        buffer_.reserve(windowMajor ? planes * VtxPlaneFile::WindowFrames : VtxPlaneFile::WindowFrames);
    }

    void feed(const std::uint8_t* bytes, std::size_t count) {
        constexpr std::size_t W = VtxPlaneFile::WindowFrames;
        for (std::size_t n = 0; n < count; ++n) {
            if (windowMajor_) {
                buffer_.push_back(bytes[n]);
                const std::size_t length = std::min(W, frames_ - window_ * W);
                if (buffer_.size() != length * planes_) continue;
                std::vector<std::uint8_t> planar(buffer_.size());
                for (std::size_t f = 0; f < length; ++f)
                    for (std::size_t p = 0; p < planes_; ++p)
                        planar[p * length + f] = buffer_[f * planes_ + p];
                if (window_ == loopWindow_) layout_.loop[0] = position_;
                writeBlob(planar.data(), planar.size());
                buffer_.clear();
                ++window_;
            } else {
                if (plane_ >= planes_) return;
                if (buffer_.empty() && window_ == 0) layout_.start[plane_] = position_;
                buffer_.push_back(bytes[n]);
                const std::size_t length = std::min(W, frames_ - window_ * W);
                if (buffer_.size() != length) continue;
                if (window_ == loopWindow_) layout_.loop[plane_] = position_;
                writeBlob(buffer_.data(), length);
                buffer_.clear();
                if (++window_ * W >= frames_) {
                    window_ = 0;
                    ++plane_;
                }
            }
        }
    }

    void emit(const void* bytes, std::size_t count) {
        sink_.write({static_cast<const char*>(bytes), count});
        position_ += static_cast<std::uint32_t>(count);
    }
    void beginDrums() { layout_.drumStart = position_; }
    void finish() {
        std::vector<std::uint8_t> trailer;
        const std::size_t streams = windowMajor_ ? 1 : planes_;
        trailer.resize(streams * 8 + 4);
        for (std::size_t s = 0; s < streams; ++s) {
            put32(trailer.data() + s * 8, layout_.start[s]);
            put32(trailer.data() + s * 8 + 4, layout_.loop[s]);
        }
        put32(trailer.data() + streams * 8, layout_.drumStart);
        emit(trailer.data(), trailer.size());
    }

private:
    static constexpr std::size_t kNone = static_cast<std::size_t>(-1);
    void writeBlob(const std::uint8_t* raw, std::size_t length) {
        lzEncode(raw, length, packed_);
        std::uint8_t code[2];
        const bool compressed = packed_.size() < length;
        const std::size_t value = compressed ? packed_.size() : 0x8000;
        code[0] = static_cast<std::uint8_t>(value);
        code[1] = static_cast<std::uint8_t>(value >> 8);
        emit(code, 2);
        if (compressed) emit(packed_.data(), packed_.size());
        else emit(raw, length);
    }

    runtime::TextStreamSink& sink_;
    std::size_t frames_;
    std::size_t planes_;
    bool windowMajor_;
    std::size_t loopWindow_;
    std::uint32_t position_;
    VtxPlaneFile::Layout layout_;
    std::vector<std::uint8_t> buffer_;
    std::vector<std::uint8_t> packed_;
    std::size_t plane_ = 0;
    std::size_t window_ = 0;
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
        WindowEncoder encoder(sink, info_.frames, 14, false, info_.metadata.loopFrame);

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
            encoder.feed(bytes, count);
            remaining -= count;
        };
        const char* error = nullptr;
        if (!decodeLh5Stream(reader, info_.decodeBytes, output, &error))
            throw DecodeFailure{error ? error : "VTX LHA5 decode failed"};
        if (remaining != 0) throw DecodeFailure{"VTX register stream is truncated"};
        encoder.beginDrums();
        encoder.finish();
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
        if (frames == 0) return nullptr;
        metadata = {};
        metadata.ym2149 = header[5] != 0;
        metadata.stereoMode = header[6];
        metadata.playerHz = header[7];
        metadata.chipClockHz = le32(header + 12);
        metadata.loopFrame = le16(header + 16);
        if (metadata.chipClockHz == 0 || metadata.playerHz == 0) return nullptr;
        const auto layout = VtxPlaneFile::readLayout(*file, 14, false);
        return std::make_shared<VtxPlaneFile>(std::move(file), frames, metadata.loopFrame, 14,
                                              false, layout);
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

VtxPlaneFile::Layout VtxPlaneFile::readLayout(runtime::StorageReadHandle& file,
                                              std::size_t planeCount, bool windowMajor) {
    const std::size_t streams = windowMajor ? 1 : planeCount;
    const std::uint64_t size = file.size();
    const std::uint64_t trailerBytes = streams * 8 + 4;
    if (planeCount == 0 || planeCount > MaximumPlanes || size < HeaderBytes + trailerBytes)
        throw runtime::ServiceError(runtime::ServiceErrorCode::IoError, "music: bad cache");
    std::uint8_t trailer[MaximumPlanes * 8 + 4];
    readFully(file, size - trailerBytes, trailer, static_cast<std::size_t>(trailerBytes),
              "cache trailer");
    Layout layout;
    layout.trailerStart = static_cast<std::uint32_t>(size - trailerBytes);
    for (std::size_t s = 0; s < streams; ++s) {
        layout.start[s] = le32(trailer + s * 8);
        layout.loop[s] = le32(trailer + s * 8 + 4);
        if (layout.start[s] < HeaderBytes || layout.start[s] >= layout.trailerStart ||
            layout.loop[s] >= layout.trailerStart)
            throw runtime::ServiceError(runtime::ServiceErrorCode::IoError, "music: bad cache");
    }
    layout.drumStart = le32(trailer + streams * 8);
    if (layout.drumStart < HeaderBytes || layout.drumStart > layout.trailerStart)
        throw runtime::ServiceError(runtime::ServiceErrorCode::IoError, "music: bad cache");
    return layout;
}

VtxPlaneFile::VtxPlaneFile(std::unique_ptr<runtime::StorageReadHandle> file,
                           std::size_t frameCount, std::size_t loopFrame,
                           std::size_t planeCount, bool windowMajor, const Layout& layout)
    : file_(std::move(file)), frameCount_(frameCount), loopFrame_(loopFrame),
      planeCount_(std::min(planeCount, MaximumPlanes)), windowMajor_(windowMajor),
      layout_(layout) {
    for (std::size_t s = 0; s < known_.size(); ++s) known_[s] = {0, layout_.start[s]};
    for (std::size_t plane = 0; plane < planeCount_; ++plane) {
        current_[plane].resize(WindowFrames);
        next_[plane].resize(WindowFrames);
        if (!load(plane, 0, current_[plane], currentWindow_[plane]))
            throw runtime::ServiceError(runtime::ServiceErrorCode::IoError,
                                        "music: cannot read music cache");
    }
}

// Decodes one window of a stream. known_ remembers where the next blob of each
// stream starts, so sequential playback never scans; a jump scans blob headers
// forward from the stream start, the loop window or the last position.
void VtxPlaneFile::readWindow(std::size_t stream, std::size_t window, std::size_t rawLength,
                              std::size_t rawBytesPerFrame, std::uint8_t* destination) {
    const auto blobLength = [&](std::size_t index, std::uint32_t offset, std::uint16_t& code) {
        std::uint8_t header[2];
        readFully(*file_, offset, header, 2, "cache window");
        code = static_cast<std::uint16_t>(header[0] | (header[1] << 8));
        const std::size_t length = std::min(WindowFrames, frameCount_ - index * WindowFrames);
        return (code & 0x8000) ? length * rawBytesPerFrame : static_cast<std::size_t>(code);
    };
    Known at = known_[stream];
    if (at.window != window) {
        Known best{0, layout_.start[stream]};
        const std::size_t loopWindow = loopFrame_ < frameCount_ ? loopFrame_ / WindowFrames : 0;
        if (loopFrame_ < frameCount_ && loopWindow <= window)
            best = {loopWindow, layout_.loop[stream]};
        if (at.window < window && at.window >= best.window) best = at;
        for (; best.window < window; ++best.window) {
            std::uint16_t code;
            best.offset += 2 + static_cast<std::uint32_t>(blobLength(best.window, best.offset, code));
        }
        at = best;
    }
    std::uint16_t code;
    const std::size_t payload = blobLength(window, at.offset, code);
    if (code & 0x8000) {
        readFully(*file_, at.offset + 2, destination, rawLength, "cache window");
    } else {
        if (payload == 0 || payload > rawLength + rawLength / 2 + 16 ||
            at.offset + 2ull + payload > layout_.drumStart)
            throw runtime::ServiceError(runtime::ServiceErrorCode::IoError, "music: bad cache");
        blob_.resize(payload);
        readFully(*file_, at.offset + 2, blob_.data(), payload, "cache window");
        if (!lzDecode(blob_.data(), payload, destination, rawLength))
            throw runtime::ServiceError(runtime::ServiceErrorCode::IoError, "music: bad cache");
    }
    known_[stream] = {window + 1, static_cast<std::uint32_t>(at.offset + 2 + payload)};
}

bool VtxPlaneFile::load(std::size_t plane, std::size_t start,
                        std::vector<std::uint8_t>& destination, Window& window) {
    const std::size_t index = start / WindowFrames;
    const std::size_t length = std::min(WindowFrames, frameCount_ - index * WindowFrames);
    try {
        std::lock_guard lock(fileMutex_);
        destination.resize(WindowFrames);
        if (!windowMajor_) {
            readWindow(plane, index, length, 1, destination.data());
        } else {
            if (wholeWindowIndex_ != index) {
                wholeWindowIndex_ = static_cast<std::size_t>(-1);
                wholeWindow_.resize(length * planeCount_);
                readWindow(0, index, length * planeCount_, planeCount_, wholeWindow_.data());
                wholeWindowIndex_ = index;
            }
            std::memcpy(destination.data(), wholeWindow_.data() + plane * length, length);
        }
    } catch (const std::exception&) {
        return false;
    }
    window = {index * WindowFrames, length};
    return true;
}

std::array<std::uint8_t, 14> VtxPlaneFile::registers(std::size_t frame) {
    const auto all = registers16(frame);
    std::array<std::uint8_t, 14> result{};
    std::copy_n(all.begin(), result.size(), result.begin());
    return result;
}

std::array<std::uint8_t, 16> VtxPlaneFile::registers16(std::size_t frame) {
    std::array<std::uint8_t, 16> result{};
    if (failed_ || frame >= frameCount_) return result;
    lastFrame_.store(frame, std::memory_order_relaxed);
    for (std::size_t plane = 0; plane < planeCount_; ++plane) {
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
    for (std::size_t plane = 0; plane < planeCount_; ++plane) {
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
            start -= start % WindowFrames;
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
            } catch (const std::bad_alloc&) {
                throw runtime::ServiceError(runtime::ServiceErrorCode::OutOfMemory,
                                            "music: not enough memory (heap fragmented)");
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

namespace ql32::music {
namespace {
constexpr std::uint8_t YmCacheVersion = 2;
constexpr std::size_t YmDrumSampleBudget = 12288;

std::uint32_t beu32(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) | p[3];
}

struct YmStop {};

struct YmInput {
    enum class Mode { Raw, Stored, Lh5 };
    Mode mode = Mode::Raw;
    std::uint64_t offset = 0;
    std::uint64_t size = 0;       // bytes stored in the file for the member
    std::uint32_t unpacked = 0;   // bytes the member unpacks to

    YmMetadata metadata;
    std::vector<std::vector<std::int16_t>> drums;
    std::size_t frames = 0;
    std::size_t planes = 14;
    bool interleaved = false;
    bool effects = false;
    std::uint64_t prefix = 0;     // unpacked bytes before the register data

    void unpack(runtime::StorageReadHandle& file, const Lh5OutputSink& sink) const {
        if (mode != Mode::Lh5) {
            std::uint8_t buffer[512];
            for (std::uint64_t done = 0; done < size;) {
                const auto count = static_cast<std::size_t>(
                    std::min<std::uint64_t>(sizeof buffer, size - done));
                readFully(file, offset + done, buffer, count, "YM stream");
                sink(buffer, count);
                done += count;
            }
            return;
        }
        std::uint64_t consumed = 0;
        const Lh5ByteReader reader = [&](std::uint8_t* destination, std::size_t capacity) {
            const auto count = static_cast<std::size_t>(
                std::min<std::uint64_t>(capacity, size - consumed));
            if (count == 0) return std::size_t{0};
            readFully(file, offset + consumed, destination, count, "YM stream");
            consumed += count;
            return count;
        };
        const char* error = nullptr;
        if (!decodeLh5Stream(reader, unpacked, sink, &error))
            throw DecodeFailure{error ? error : "YM LHA5 decode failed"};
    }
};

// Byte-wise reader of the YM header, digi-drum table and text fields.
class YmPrefixParser {
public:
    explicit YmPrefixParser(YmInput& source) : y_(source) {}

    void feed(const std::uint8_t* bytes, std::size_t count) {
        for (std::size_t i = 0; i < count; ++i) byte(bytes[i]);
    }
    std::uint32_t loopTail() const { return beu32(tail_); }

private:
    void byte(std::uint8_t b) {
        ++pos_;
        for (int i = 0; i < 3; ++i) tail_[i] = tail_[i + 1];
        tail_[3] = b;
        switch (phase_) {
        case Phase::Header:
            header_[have_++] = b;
            if (have_ == 4) startFormat();
            else if (have_ == 34) parseHeader();
            break;
        case Phase::Skip:
            if (--skip_ == 0) afterExtra();
            break;
        case Phase::DrumLength:
            lengthBytes_[lengthHave_++] = b;
            if (lengthHave_ == 4) startDrum();
            break;
        case Phase::DrumData:
            if (keep_) {
                const int v = b;
                current_.push_back(static_cast<std::int16_t>(fourBit_ ? ((v & 15) - 8) * 2048
                    : signed_ ? static_cast<std::int8_t>(v) * 128 : (v - 128) * 128));
            }
            if (--drumLeft_ == 0) finishDrum();
            break;
        case Phase::Text:
            if (b == 0 && ++terminators_ == 3) finishYm5();
            break;
        case Phase::Payload:
            break;
        }
    }

    void startFormat() {
        auto& m = y_.metadata;
        m.format.assign(reinterpret_cast<const char*>(header_), 4);
        if (m.format == "YM5!" || m.format == "YM6!") return;
        if (m.format != "YM2!" && m.format != "YM3!" && m.format != "YM3b")
            throw std::runtime_error("unsupported YM variant (supported: YM2!, YM3!, YM3b, YM5!, YM6!)");
        const bool loopTail = m.format == "YM3b";
        const std::uint64_t payload = y_.unpacked - 4ull - (loopTail ? 4 : 0);
        if (y_.unpacked < (loopTail ? 8u : 4u) || payload == 0 || payload % 14 != 0)
            throw std::runtime_error("YM3 register data size is invalid");
        y_.frames = static_cast<std::size_t>(payload / 14);
        y_.planes = 14;
        y_.interleaved = false;
        y_.prefix = 4;
        m.chipClockHz = 2000000;
        m.frameRateHz = 50;
        phase_ = Phase::Payload;
        if (!loopTail) throw YmStop{};
    }

    void parseHeader() {
        auto& m = y_.metadata;
        if (std::string_view(reinterpret_cast<const char*>(header_ + 4), 8) != "LeOnArD!")
            throw std::runtime_error("YM5/YM6 header is invalid");
        frames_ = beu32(header_ + 12);
        const std::uint32_t attributes = beu32(header_ + 16);
        drums_ = static_cast<unsigned>((header_[20] << 8) | header_[21]);
        m.chipClockHz = beu32(header_ + 22);
        m.frameRateHz = static_cast<std::uint16_t>((header_[26] << 8) | header_[27]);
        m.loopFrame = beu32(header_ + 28);
        skip_ = static_cast<std::uint32_t>((header_[32] << 8) | header_[33]);
        if (frames_ == 0 || m.chipClockHz == 0 || m.frameRateHz == 0)
            throw std::runtime_error("YM5/YM6 header values are invalid");
        y_.interleaved = (attributes & 1) == 0;
        signed_ = (attributes & 2) != 0;
        fourBit_ = (attributes & 4) != 0;
        if (skip_ != 0) phase_ = Phase::Skip;
        else afterExtra();
    }

    void afterExtra() { phase_ = drums_ ? Phase::DrumLength : Phase::Text; }

    void startDrum() {
        lengthHave_ = 0;
        drumLeft_ = beu32(lengthBytes_);
        current_.clear();
        keep_ = drumTotal_ + drumLeft_ <= YmDrumSampleBudget;
        if (keep_) {
            current_.reserve(drumLeft_);
            drumTotal_ += drumLeft_;
        }
        if (drumLeft_ == 0) finishDrum();
        else phase_ = Phase::DrumData;
    }

    void finishDrum() {
        y_.drums.push_back(std::move(current_));
        current_ = {};
        phase_ = ++drumIndex_ == drums_ ? Phase::Text : Phase::DrumLength;
    }

    void finishYm5() {
        y_.prefix = pos_;
        if (y_.prefix >= y_.unpacked)
            throw std::runtime_error("YM register data is truncated");
        const std::uint64_t body = y_.unpacked - y_.prefix;
        // Some files carry only 14 registers per frame or lose their tail.
        y_.planes = body + 16 >= static_cast<std::uint64_t>(frames_) * 16 ? 16 : 14;
        const std::uint64_t usable = body / y_.planes;
        if (usable == 0) throw std::runtime_error("YM register data is truncated");
        y_.frames = static_cast<std::size_t>(std::min<std::uint64_t>(usable, frames_));
        y_.effects = y_.planes == 16;
        throw YmStop{};
    }

    enum class Phase { Header, Skip, DrumLength, DrumData, Text, Payload };
    YmInput& y_;
    Phase phase_ = Phase::Header;
    std::uint64_t pos_ = 0;
    std::uint8_t header_[34] = {};
    std::size_t have_ = 0;
    std::uint32_t frames_ = 0;
    unsigned drums_ = 0;
    unsigned drumIndex_ = 0;
    std::uint32_t skip_ = 0;
    std::uint8_t lengthBytes_[4] = {};
    std::size_t lengthHave_ = 0;
    std::uint32_t drumLeft_ = 0;
    std::vector<std::int16_t> current_;
    std::size_t drumTotal_ = 0;
    bool keep_ = false;
    bool signed_ = false;
    bool fourBit_ = false;
    int terminators_ = 0;
    std::uint8_t tail_[4] = {};
};

YmInput readYmSource(runtime::StorageReadHandle& file) {
    const std::uint64_t size = file.size();
    if (size < 8 || size > YmSong::MaximumPackedBytes)
        throw std::runtime_error("YM file size is invalid or exceeds 256 KiB");
    std::uint8_t head[32] = {};
    readFully(file, 0, head, static_cast<std::size_t>(std::min<std::uint64_t>(size, sizeof head)),
              "YM header");
    YmInput y;
    const std::string_view method(reinterpret_cast<const char*>(head + 2), 5);
    if (size >= 22 && (method == "-lh5-" || method == "-lh0-")) {
        const std::uint64_t headerSize = static_cast<std::uint64_t>(head[0]) + 2;
        const std::uint32_t packed = le32(head + 7);
        y.unpacked = le32(head + 11);
        if (head[20] != 0 || headerSize < 24 || headerSize > size)
            throw std::runtime_error("unsupported LHA header (level 0 required)");
        if (packed > size - headerSize || y.unpacked < 8 ||
            y.unpacked > YmSong::MaximumUnpackedBytes)
            throw std::runtime_error("YM LHA member sizes are invalid or too large");
        y.offset = headerSize;
        y.size = packed;
        if (method == "-lh0-") {
            if (packed != y.unpacked)
                throw std::runtime_error("YM LHA stored member is inconsistent");
            y.mode = YmInput::Mode::Stored;
        } else {
            y.mode = YmInput::Mode::Lh5;
        }
    } else {
        if (head[0] != 'Y' || head[1] != 'M') throw std::runtime_error("file is not a YM song");
        y.size = size;
        y.unpacked = static_cast<std::uint32_t>(size);
    }

    YmPrefixParser parser(y);
    bool stopped = false;
    try {
        y.unpack(file, [&](const std::uint8_t* bytes, std::size_t count) {
            parser.feed(bytes, count);
        });
    } catch (const YmStop&) {
        stopped = true;
    }
    if (!stopped) {   // YM3b: the loop frame is the last four bytes
        y.metadata.loopFrame = parser.loopTail();
    }
    if (y.metadata.format.empty() || y.frames == 0)
        throw std::runtime_error("YM song has no register data");
    return y;
}

class YmTranscodeSource final : public runtime::TextStreamSource {
public:
    YmTranscodeSource(runtime::StorageReadHandle& file, const YmInput& y,
                      std::uint32_t srcSize, std::uint32_t key)
        : file_(file), y_(y), srcSize_(srcSize), key_(key) {}

    void writeTo(runtime::TextStreamSink& sink) const override {
        std::uint8_t header[VtxPlaneFile::HeaderBytes] = {'Q', '3', '2', 'Y', YmCacheVersion};
        header[5] = static_cast<std::uint8_t>((y_.interleaved ? 1 : 0) | (y_.effects ? 2 : 0));
        header[6] = static_cast<std::uint8_t>(y_.planes);
        header[7] = static_cast<std::uint8_t>(formatCode());
        put32(header + 8, static_cast<std::uint32_t>(y_.frames));
        put32(header + 12, y_.metadata.chipClockHz);
        put32(header + 16, y_.metadata.loopFrame);
        put32(header + 20, srcSize_);
        put32(header + 24, key_);
        header[28] = static_cast<std::uint8_t>(y_.metadata.frameRateHz);
        header[29] = static_cast<std::uint8_t>(y_.metadata.frameRateHz >> 8);
        header[30] = static_cast<std::uint8_t>(y_.drums.size());
        header[31] = static_cast<std::uint8_t>(y_.drums.size() >> 8);
        sink.write({reinterpret_cast<const char*>(header), sizeof header});

        WindowEncoder encoder(sink, y_.frames, y_.planes, y_.interleaved, y_.metadata.loopFrame);
        std::uint64_t pos = 0;
        std::uint64_t remaining = static_cast<std::uint64_t>(y_.frames) * y_.planes;
        y_.unpack(file_, [&](const std::uint8_t* bytes, std::size_t count) {
            if (pos < y_.prefix) {
                const auto skip = static_cast<std::size_t>(
                    std::min<std::uint64_t>(count, y_.prefix - pos));
                pos += skip;
                bytes += skip;
                count -= skip;
            }
            count = static_cast<std::size_t>(std::min<std::uint64_t>(count, remaining));
            if (count == 0) return;
            encoder.feed(bytes, count);
            remaining -= count;
        });
        if (remaining != 0) throw DecodeFailure{"YM register data is truncated"};
        encoder.beginDrums();

        for (const auto& drum : y_.drums) {
            std::uint8_t length[4];
            put32(length, static_cast<std::uint32_t>(drum.size()));
            encoder.emit(length, sizeof length);
            std::uint8_t block[256];
            for (std::size_t i = 0; i < drum.size();) {
                std::size_t n = 0;
                for (; n + 2 <= sizeof block && i < drum.size(); ++i, n += 2) {
                    const auto v = static_cast<std::uint16_t>(drum[i]);
                    block[n] = static_cast<std::uint8_t>(v);
                    block[n + 1] = static_cast<std::uint8_t>(v >> 8);
                }
                encoder.emit(block, n);
            }
        }
        encoder.finish();
    }

private:
    char formatCode() const {
        const auto& f = y_.metadata.format;
        return f == "YM3b" ? 'b' : f[2];
    }
    runtime::StorageReadHandle& file_;
    const YmInput& y_;
    std::uint32_t srcSize_;
    std::uint32_t key_;
};

struct YmCacheContents {
    YmMetadata metadata;
    std::vector<std::vector<std::int16_t>> drums;
    std::size_t frames = 0;
    bool effects = false;
};

std::shared_ptr<VtxPlaneFile> openYmCache(runtime::StorageService& storage,
                                          const std::string& cachePath, std::uint32_t srcSize,
                                          std::uint32_t key, YmCacheContents& out) {
    try {
        if (!storage.exists(cachePath)) return nullptr;
        auto file = storage.openRead(cachePath);
        std::uint8_t header[VtxPlaneFile::HeaderBytes];
        if (file->size() < sizeof header) return nullptr;
        readFully(*file, 0, header, sizeof header, "YM cache header");
        if (std::string_view(reinterpret_cast<const char*>(header), 4) != "Q32Y" ||
            header[4] != YmCacheVersion || le32(header + 20) != srcSize ||
            le32(header + 24) != key)
            return nullptr;
        const std::size_t planes = header[6];
        const std::size_t frames = le32(header + 8);
        if (frames == 0 || planes < 14 || planes > VtxPlaneFile::MaximumPlanes) return nullptr;
        const bool windowMajor = (header[5] & 1) != 0;
        const auto layout = VtxPlaneFile::readLayout(*file, planes, windowMajor);
        std::uint64_t cursor = layout.drumStart;
        const std::uint64_t size = layout.trailerStart;

        YmCacheContents contents;
        contents.frames = frames;
        contents.effects = (header[5] & 2) != 0;
        auto& m = contents.metadata;
        switch (header[7]) {
        case '2': m.format = "YM2!"; break;
        case '3': m.format = "YM3!"; break;
        case 'b': m.format = "YM3b"; break;
        case '5': m.format = "YM5!"; break;
        case '6': m.format = "YM6!"; break;
        default: return nullptr;
        }
        m.chipClockHz = le32(header + 12);
        m.loopFrame = le32(header + 16);
        m.frameRateHz = le16(header + 28);
        if (m.chipClockHz == 0 || m.frameRateHz == 0) return nullptr;
        const std::size_t drumCount = le16(header + 30);
        std::size_t samples = 0;
        for (std::size_t d = 0; d < drumCount; ++d) {
            std::uint8_t length[4];
            if (cursor + 4 > size) return nullptr;
            readFully(*file, cursor, length, 4, "YM cache drum");
            cursor += 4;
            const std::size_t count = le32(length);
            samples += count;
            if (samples > YmDrumSampleBudget || cursor + count * 2ull > size) return nullptr;
            std::vector<std::uint8_t> raw(count * 2);
            if (count) readFully(*file, cursor, raw.data(), raw.size(), "YM cache drum");
            cursor += raw.size();
            std::vector<std::int16_t> pcm(count);
            for (std::size_t i = 0; i < count; ++i)
                pcm[i] = static_cast<std::int16_t>(le16(raw.data() + i * 2));
            contents.drums.push_back(std::move(pcm));
        }
        if (cursor != size) return nullptr;
        auto planesFile = std::make_shared<VtxPlaneFile>(
            std::move(file), frames, m.loopFrame, planes, windowMajor, layout);
        out = std::move(contents);
        return planesFile;
    } catch (const std::exception&) {
        return nullptr;
    }
}
} // namespace

YmSong openYmStreamed(runtime::StorageService& storage, const std::string& path,
                      const std::vector<std::string>& cacheRoots, VtxStreamInfo* info) {
    if (cacheRoots.empty())
        throw runtime::ServiceError(runtime::ServiceErrorCode::Unavailable,
                                    "music: no music cache location configured");
    auto source = storage.openRead(path);
    const auto srcSize = static_cast<std::uint32_t>(source->size());
    const std::uint32_t key = sourceKey(*source);

    YmCacheContents contents;
    const auto finish = [&](std::shared_ptr<VtxPlaneFile> planes, const std::string& cache,
                            bool hit) {
        if (info) {
            info->cachePath = cache;
            info->cacheHit = hit;
        }
        return YmSong::fromPlaneFile(std::move(contents.metadata), std::move(contents.drums),
                                     std::move(planes), contents.frames, contents.effects);
    };
    for (const auto& root : cacheRoots) {
        const auto cache = cacheName(root, path);
        if (auto planes = openYmCache(storage, cache, srcSize, key, contents))
            return finish(std::move(planes), cache, true);
    }

    const YmInput parsed = readYmSource(*source);
    std::string lastError = "no writable location";
    for (const auto& root : cacheRoots) {
        const auto cache = cacheName(root, path);
        const YmTranscodeSource transcode(*source, parsed, srcSize, key);
        for (int attempt = 0; attempt < 2; ++attempt) {
            try {
                storage.writeTextStreamAtomic(cache, transcode);
                if (auto planes = openYmCache(storage, cache, srcSize, key, contents))
                    return finish(std::move(planes), cache, false);
                lastError = "cache verification failed on " + root;
            } catch (const DecodeFailure& failure) {
                throw std::runtime_error(failure.message);
            } catch (const std::bad_alloc&) {
                throw runtime::ServiceError(runtime::ServiceErrorCode::OutOfMemory,
                                            "music: not enough memory (heap fragmented)");
            } catch (const std::exception& error) {
                lastError = error.what();
            }
            if (attempt == 0) evictOtherCaches(storage, root, cache);
        }
    }
    throw runtime::ServiceError(runtime::ServiceErrorCode::IoError,
                                "music: cannot write YM cache: " + lastError);
}

} // namespace ql32::music
