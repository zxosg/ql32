#pragma once

#include "ql32/music/mixer.hpp"
#include "ql32/runtime/storage.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>

namespace ql32::music {

// Streams an uncompressed PCM WAV file (8-bit unsigned or 16-bit signed,
// mono or stereo, 4000..48000 Hz) from storage in small blocks, resampling
// linearly to the mixer rate. The whole file is never held in memory.
class WavSource final : public AudioSource {
public:
    static constexpr std::size_t BufferBytes = 2048;

    WavSource(std::unique_ptr<runtime::StorageReadHandle> file, std::uint32_t outputRate)
        : file_(std::move(file)) {
        parseHeader();
        step_ = (static_cast<std::uint64_t>(sampleRate_) << 16) / outputRate;
        prev_ = nextFrame();
        cur_ = nextFrame();
    }

    void render(std::int16_t* samples, std::size_t count) noexcept override {
        for (std::size_t i = 0; i < count; ++i) {
            if (!active_) { samples[i] = 0; continue; }
            const std::int32_t frac = static_cast<std::int32_t>(phase_ & 0xffff);
            samples[i] = static_cast<std::int16_t>(
                prev_ + (((static_cast<std::int32_t>(cur_) - prev_) * frac) >> 16));
            phase_ += step_;
            while (phase_ >= 0x10000 && active_) {
                phase_ -= 0x10000;
                prev_ = cur_;
                cur_ = nextFrame();
            }
        }
    }
    bool playing() const noexcept override { return active_; }
    std::uint32_t sampleRate() const noexcept { return sampleRate_; }
    std::uint16_t channels() const noexcept { return channels_; }
    std::uint16_t bitsPerSample() const noexcept { return bits_; }

private:
    static std::uint32_t le32(const std::uint8_t* p) noexcept {
        return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
    }
    static std::uint16_t le16(const std::uint8_t* p) noexcept {
        return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
    }

    void parseHeader() {
        std::array<std::uint8_t, 12> riff{};
        if (file_->readAt(0, riff.data(), riff.size()) != riff.size() ||
            std::string_view(reinterpret_cast<const char*>(riff.data()), 4) != "RIFF" ||
            std::string_view(reinterpret_cast<const char*>(riff.data() + 8), 4) != "WAVE")
            throw std::runtime_error("music: not a RIFF/WAVE file");
        std::uint64_t offset = 12;
        const std::uint64_t size = file_->size();
        bool haveFormat = false;
        while (offset + 8 <= size) {
            std::array<std::uint8_t, 8> chunk{};
            if (file_->readAt(offset, chunk.data(), chunk.size()) != chunk.size()) break;
            const std::string_view id(reinterpret_cast<const char*>(chunk.data()), 4);
            const std::uint64_t length = le32(chunk.data() + 4);
            if (id == "fmt ") {
                std::array<std::uint8_t, 16> fmt{};
                if (length < fmt.size() ||
                    file_->readAt(offset + 8, fmt.data(), fmt.size()) != fmt.size())
                    throw std::runtime_error("music: WAV fmt chunk is truncated");
                const std::uint16_t format = le16(fmt.data());
                channels_ = le16(fmt.data() + 2);
                sampleRate_ = le32(fmt.data() + 4);
                bits_ = le16(fmt.data() + 14);
                if (format != 1)
                    throw std::runtime_error("music: only uncompressed PCM WAV is supported");
                if (channels_ < 1 || channels_ > 2 || (bits_ != 8 && bits_ != 16))
                    throw std::runtime_error("music: WAV must be 8/16-bit mono or stereo");
                if (sampleRate_ < 4000 || sampleRate_ > 48000)
                    throw std::runtime_error("music: WAV sample rate must be 4000-48000 Hz");
                haveFormat = true;
            } else if (id == "data") {
                if (!haveFormat) throw std::runtime_error("music: WAV data precedes fmt");
                dataOffset_ = offset + 8;
                dataEnd_ = dataOffset_ + length;
                if (dataEnd_ > size) dataEnd_ = size;
                position_ = dataOffset_;
                if (dataEnd_ <= dataOffset_)
                    throw std::runtime_error("music: WAV contains no audio data");
                return;
            }
            offset += 8 + length + (length & 1);
        }
        throw std::runtime_error("music: WAV has no data chunk");
    }

    std::int16_t nextFrame() noexcept {
        const std::size_t frameBytes = static_cast<std::size_t>(channels_) * (bits_ / 8);
        if (bufferPos_ + frameBytes > bufferLen_) {
            bufferPos_ = 0;
            bufferLen_ = 0;
            if (position_ < dataEnd_) {
                std::uint64_t want = dataEnd_ - position_;
                if (want > buffer_.size()) want = buffer_.size();
                want -= want % frameBytes;
                bufferLen_ = want ? file_->readAt(position_, buffer_.data(),
                                                   static_cast<std::size_t>(want)) : 0;
                bufferLen_ -= bufferLen_ % frameBytes;
                position_ += bufferLen_;
            }
            if (bufferLen_ == 0) { active_ = false; return 0; }
        }
        const std::uint8_t* p = buffer_.data() + bufferPos_;
        bufferPos_ += frameBytes;
        std::int32_t sum = 0;
        for (std::uint16_t c = 0; c < channels_; ++c) {
            sum += bits_ == 16 ? static_cast<std::int16_t>(le16(p + c * 2))
                               : (static_cast<std::int32_t>(p[c]) - 128) * 256;
        }
        return static_cast<std::int16_t>(sum / channels_);
    }

    std::unique_ptr<runtime::StorageReadHandle> file_;
    std::array<std::uint8_t, BufferBytes> buffer_{};
    std::size_t bufferPos_ = 0;
    std::size_t bufferLen_ = 0;
    std::uint64_t dataOffset_ = 0;
    std::uint64_t dataEnd_ = 0;
    std::uint64_t position_ = 0;
    std::uint32_t sampleRate_ = 0;
    std::uint32_t step_ = 0;
    std::uint32_t phase_ = 0;
    std::uint16_t channels_ = 0;
    std::uint16_t bits_ = 0;
    std::int16_t prev_ = 0;
    std::int16_t cur_ = 0;
    bool active_ = true;
};

} // namespace ql32::music
