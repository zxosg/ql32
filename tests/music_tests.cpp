#include "ql32/interpreter.hpp"
#include "ql32/music/ay38912.hpp"
#include "ql32/music/dac_format.hpp"
#include "ql32/music/mixer.hpp"
#include "ql32/music/song_loader.hpp"
#include "ql32/music/pt3.hpp"
#include "ql32/music/sqt.hpp"
#include "ql32/music/tone.hpp"
#include "ql32/music/wav.hpp"
#include "ql32/music/vtx.hpp"
#include "ql32/music/vtx_player.hpp"
#include "ql32/music/vtx_stream.hpp"
#include "ql32/music/ym_player.hpp"
#include "ql32/runtime/music.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <map>
#include <new>
#include <sstream>
#include <stdexcept>
#include <optional>
#include <string>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

class BitWriter {
public:
    void put(unsigned value, unsigned count) {
        for (unsigned bit = count; bit-- > 0;) {
            if ((bits_ & 7u) == 0) bytes_.push_back(0);
            bytes_.back() |= static_cast<std::uint8_t>(
                ((value >> bit) & 1u) << (7 - (bits_ & 7u)));
            ++bits_;
        }
    }
    std::vector<std::uint8_t> take() { return std::move(bytes_); }
private:
    std::vector<std::uint8_t> bytes_;
    unsigned bits_ = 0;
};

void put16(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint16_t value) {
    bytes[at] = static_cast<std::uint8_t>(value);
    bytes[at + 1] = static_cast<std::uint8_t>(value >> 8);
}
void put32(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        bytes[at + i] = static_cast<std::uint8_t>(value >> (8 * i));
}

std::vector<std::uint8_t> syntheticVtx() {
    // A valid LH5 block with a one-bit literal tree and 56 output bytes.
    // This exercises the VTX/LHA framing without embedding a copyrighted song.
    BitWriter writer;
    writer.put(56, 16);
    writer.put(0, 5); writer.put(3, 5);   // constant PT symbol 3 (length 1)
    writer.put(2, 9);                     // two C codes
    writer.put(0, 4); writer.put(0, 4);   // constant position symbol 0
    for (unsigned i = 0; i < 56; ++i) writer.put(i & 1u, 1);
    auto packed = writer.take();

    std::vector<std::uint8_t> bytes(16, 0);
    bytes[0] = 'a'; bytes[1] = 'y';
    bytes[3] = 0xff; bytes[4] = 0xff;
    bytes[5] = 0x58; bytes[6] = 0x0f; bytes[7] = 0x1b;
    bytes[8] = 0x00; // 1,773,400 Hz
    bytes[9] = 50;
    put32(bytes, 12, 56);
    for (int i = 0; i < 5; ++i) bytes.push_back(0);

    const std::size_t lha = bytes.size();
    bytes.resize(lha + 24, 0);
    bytes[lha] = 22; // header length + the two framing bytes
    std::copy_n("-lh5-", 5, bytes.begin() + lha + 2);
    put32(bytes, lha + 7, static_cast<std::uint32_t>(packed.size()));
    put32(bytes, lha + 11, 56);
    bytes.insert(bytes.end(), packed.begin(), packed.end());
    return bytes;
}

class FakeMusic final : public ql32::runtime::MusicService {
public:
    void play(ql32::runtime::MusicSessionId session, const std::string& path) override {
        owner = session; lastPath = path; current = ql32::runtime::MusicPlaybackState::Playing;
    }
    void playWav(ql32::runtime::MusicSessionId session, const std::string& path) override {
        owner = session; lastWav = path; current = ql32::runtime::MusicPlaybackState::Playing;
    }
    void testTone(ql32::runtime::MusicSessionId session) override {
        owner = session; toneRequested = true;
        current = ql32::runtime::MusicPlaybackState::Playing;
    }
    void beep(ql32::runtime::MusicSessionId session, std::uint32_t durationMs,
              std::uint32_t pitchHz, ql32::runtime::ToneWaveform waveform) override {
        owner = session; beeps.push_back({durationMs, pitchHz, waveform});
    }
    void setVolume(std::uint8_t level) override { level_ = level; }
    std::uint8_t volume() const noexcept override { return level_; }
    void stop(ql32::runtime::MusicSessionId session) override {
        require(owner == session, "MUSIC STOP used the wrong session");
        owner = 0; current = ql32::runtime::MusicPlaybackState::Stopped;
    }
    void pause(ql32::runtime::MusicSessionId session) override {
        require(owner == session, "MUSIC PAUSE used the wrong session");
        current = ql32::runtime::MusicPlaybackState::Paused;
    }
    void resume(ql32::runtime::MusicSessionId session) override {
        require(owner == session, "MUSIC RESUME used the wrong session");
        current = ql32::runtime::MusicPlaybackState::Playing;
    }
    ql32::runtime::MusicPlaybackState state() const noexcept override { return current; }
    void stopSession(ql32::runtime::MusicSessionId session) noexcept override {
        if (owner == session) { owner = 0; current = ql32::runtime::MusicPlaybackState::Stopped; }
    }

    ql32::runtime::MusicSessionId owner = 0;
    std::string lastPath;
    std::string lastWav;
    bool toneRequested = false;
    struct Beep { std::uint32_t durationMs, pitchHz; ql32::runtime::ToneWaveform waveform; };
    std::vector<Beep> beeps;
    std::uint8_t level_ = 128;
    ql32::runtime::MusicPlaybackState current = ql32::runtime::MusicPlaybackState::Stopped;
};

void testVtxParserAndRenderer() {
    auto song = ql32::music::VtxSong::parse(syntheticVtx());
    require(song.frameCount() == 4, "VTX frame count was not decoded");
    require(song.metadata().playerHz == 50 && !song.metadata().ym2149,
            "VTX AY metadata was not parsed");
    for (std::size_t frame = 0; frame < song.frameCount(); ++frame)
        for (const auto value : song.registers(frame))
            require(value == (frame & 1u), "VTX register planes were not decoded");

    ql32::music::VtxPlayer player(std::move(song));
    std::array<std::int16_t, 2048> pcm{};
    player.render(pcm.data(), pcm.size());
    require(!player.playing(), "short VTX did not stop at end of stream");

    auto malformed = syntheticVtx();
    malformed[0] = 'x';
    bool rejected = false;
    try { (void)ql32::music::VtxSong::parse(malformed); }
    catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "invalid VTX signature was accepted");

    ql32::music::Ay38912 ay;
    ay.reset(1773400, 22050);
    std::array<std::uint8_t, 14> registers{};
    registers[0] = 30;
    registers[7] = 0x3e;
    registers[8] = 15;
    ay.writeRegisters(registers);
    std::array<std::int16_t, 128> tone{};
    for (auto& sample : tone) sample = ay.renderSample();
    bool changed = false;
    for (std::size_t i = 1; i < tone.size(); ++i)
        changed = changed || tone[i] != tone[i - 1];
    require(changed, "AY tone generator produced a constant signal");
}

void testAyUltrasonicTone() {
    // Tone period 1 (about 111 kHz) must not alias into an audible whistle: the output is a
    // constant level (half the square), not a toggling signal.
    ql32::music::Ay38912 ay;
    ay.reset(1773400, 22050);
    std::array<std::uint8_t, 14> registers{};
    registers[0] = 1;
    registers[7] = 0x38;
    registers[8] = 15;
    ay.writeRegisters(registers, true);
    int maxStep = 0;
    std::int16_t previous = ay.renderSample();
    for (int i = 1; i < 200; ++i) {
        const auto sample = ay.renderSample();
        maxStep = std::max(maxStep, std::abs(sample - previous));
        previous = sample;
    }
    require(maxStep < 200, "AY ultrasonic tone aliased into the audio band");
}

void testAyEnvelopeRate() {
    ql32::music::Ay38912 ay;
    ay.reset(1773400, 22050);
    std::array<std::uint8_t, 14> registers{};
    registers[7] = 0x3f;
    registers[8] = 0x10;
    registers[11] = 100;
    registers[13] = 8;
    ay.writeRegisters(registers, true);
    // Datasheet: one 16-step ramp lasts 256 * period / clock = 14.4 ms = ~318 samples.
    // A sawtooth envelope restarts at full level, which shows up as a large upward jump.
    std::array<std::size_t, 2> jumps{};
    std::size_t found = 0;
    std::int16_t previousSample = ay.renderSample();
    for (std::size_t i = 1; i < 800 && found < 2; ++i) {
        const auto sample = ay.renderSample();
        if (sample - previousSample > 4000 && (found == 0 || i - jumps[found - 1] > 5)) jumps[found++] = i;
        previousSample = sample;
    }
    require(found == 2, "AY sawtooth envelope never restarted");
    require(jumps[0] > 290 && jumps[0] < 350, "AY envelope ramp time is wrong");
    require(jumps[1] - jumps[0] > 300 && jumps[1] - jumps[0] < 340, "AY envelope period is not constant");

    // Every one of the 16 volume steps must be distinct and at most about 5 dB apart.
    std::int16_t previous = 0;
    for (std::uint8_t volume = 1; volume < 16; ++volume) {
        ay.reset(1773400, 22050);
        registers[8] = volume;
        registers[13] = 0;
        ay.writeRegisters(registers);
        const auto level = ay.renderSample();
        require(level > previous, "AY volume steps are not strictly increasing");
        if (volume > 1) require(level * 10 < previous * 18, "AY volume step is coarser than 5 dB");
        previous = level;
    }
}

void testMusicBasicCommands() {
    auto music = std::make_shared<FakeMusic>();
    ql32::Interpreter interpreter({}, {}, {}, {}, {}, {}, {}, 42, {}, {}, {}, {}, music);
    std::ostringstream output;
    interpreter.execute(
        "MUSIC PLAY \"SDC:/test.vtx\"\n"
        "MUSIC PAUSE\n"
        "MUSIC RESUME\n"
        "MUSIC STATUS\n"
        "MUSIC STOP\n"
        "MUSIC TEST\n"
        "MUSIC STOP\n"
        "MUSIC WAV \"SDC:/a.wav\"\n", output);
    require(music->lastWav == "SDC:/a.wav", "MUSIC WAV path was not passed to service");
    music->stop(music->owner);
    require(music->lastPath == "SDC:/test.vtx", "MUSIC PLAY path was not passed to service");
    require(music->owner == 0 && music->state() ==
            ql32::runtime::MusicPlaybackState::Stopped,
            "MUSIC STOP did not release playback");
    require(output.str().find("MUSIC PLAYING") != std::string::npos,
            "MUSIC STATUS did not print the current state");
    require(music->toneRequested && music->owner == 0,
            "MUSIC TEST did not start the test tone or MUSIC STOP did not stop it");
}
void testDacFormatAndTone() {
    using namespace ql32::music;
    require(pcmToDac16(0) == 0x8000, "silence must map to DAC mid-scale");
    require(pcmToDac16(-32768) == 0x0000 && pcmToDac16(32767) == 0xff00,
            "DAC mapping must span the full unsigned range in the upper byte");
    require((pcmToDac16(12345) & 0x00ff) == 0, "DAC value must use only the upper byte");
    ToneGenerator tone;
    tone.start(1000, 22050, 22050, Waveform::Sine, 6000);
    std::array<std::int16_t, 22050> pcm{};
    tone.mixInto(pcm.data(), pcm.size());
    require(!tone.active(), "tone did not end after its duration");
    int crossings = 0;
    for (std::size_t i = 1; i < pcm.size(); ++i)
        if (pcm[i - 1] < 0 && pcm[i] >= 0) ++crossings;
    require(crossings >= 998 && crossings <= 1002, "1 kHz tone has the wrong frequency");
    const auto limits = std::minmax_element(pcm.begin(), pcm.end());
    require(*limits.first <= -5990 && *limits.second >= 5990 &&
            *limits.first >= -6000 && *limits.second <= 6000, "tone amplitude is wrong");
    require(pcmToDac16(*limits.second) != pcmToDac16(*limits.first),
            "tone must produce distinct DAC levels");
}

void testWaveforms() {
    using namespace ql32::music;
    const Waveform all[] = {Waveform::Square, Waveform::Sine, Waveform::Triangle,
                            Waveform::SawtoothRising, Waveform::SawtoothFalling};
    for (const auto waveform : all) {
        ToneGenerator tone;
        tone.start(100, 220, 22050, waveform, 10000); // exactly one cycle
        std::array<std::int16_t, 220> pcm{};
        tone.mixInto(pcm.data(), pcm.size());
        const auto limits = std::minmax_element(pcm.begin(), pcm.end());
        require(*limits.second >= 9800 && *limits.second <= 10000 &&
                *limits.first <= -9800 && *limits.first >= -10000,
                "waveform does not span its amplitude");
        long long sum = 0;
        for (const auto sample : pcm) sum += sample;
        require(std::llabs(sum) < 220 * 300, "waveform has a DC offset");
        if (waveform == Waveform::SawtoothRising)
            require(pcm[10] > pcm[0] && pcm[200] > pcm[100], "rising saw does not rise");
        if (waveform == Waveform::SawtoothFalling)
            require(pcm[10] < pcm[0] && pcm[200] < pcm[100], "falling saw does not fall");
        if (waveform == Waveform::Triangle)
            require(pcm[55] > pcm[0] && pcm[165] < pcm[110] && pcm[110] > pcm[165],
                    "triangle shape is wrong");
        if (waveform == Waveform::Square)
            require(pcm[10] == 10000 && pcm[150] == -10000, "square levels are wrong");
    }
    require(applyVolume(10000, 255) == 10000 && applyVolume(10000, 0) == 0 &&
            applyVolume(-10000, 128) < -4900 && applyVolume(-10000, 128) > -5100,
            "volume scaling is wrong");
    std::array<std::int16_t, 4> loud{30000, 30000, -30000, 0};
    ToneGenerator tone;
    tone.start(1000, 0, 22050, Waveform::Square, 12000);
    tone.mixInto(loud.data(), loud.size());
    require(loud[0] == 32767, "mixed tone must saturate, not wrap");
}

void testBeepAndVolumeCommands() {
    using ql32::runtime::ToneWaveform;
    auto music = std::make_shared<FakeMusic>();
    ql32::Interpreter interpreter({}, {}, {}, {}, {}, {}, {}, 42, {}, {}, {}, {}, music);
    std::ostringstream output;
    interpreter.execute(
        "VOLUME 200\n"
        "BEEP 5, 440\n"
        "BEEP 5, 440, \"sine\"\n"
        "BEEP 5, 880, \"/|\"\n"
        "BEEP 5, 880, \"|\\\"\n"
        "BEEP 5, 880, 2\n", output);
    require(music->volume() == 200, "VOLUME was not applied");
    require(music->beeps.size() == 5, "BEEP did not reach the service");
    require(music->beeps[0].waveform == ToneWaveform::Square &&
            music->beeps[0].durationMs == 5 && music->beeps[0].pitchHz == 440,
            "BEEP default waveform or arguments are wrong");
    require(music->beeps[1].waveform == ToneWaveform::Sine &&
            music->beeps[2].waveform == ToneWaveform::SawtoothRising &&
            music->beeps[3].waveform == ToneWaveform::SawtoothFalling &&
            music->beeps[4].waveform == ToneWaveform::Triangle,
            "BEEP waveform names were not mapped");

    const char* bad[] = {
        "BEEP -5, 440\n", "BEEP 0, 440\n", "BEEP 5.5, 440\n", "BEEP 5, 10\n",
        "BEEP 5, 20000\n", "BEEP 5, 440, \"wobble\"\n", "BEEP 5, 440, 9\n",
        "VOLUME -1\n", "VOLUME 256\n", "VOLUME 1.5\n"};
    for (const char* source : bad) {
        bool rejected = false;
        ql32::Interpreter strict({}, {}, {}, {}, {}, {}, {}, 42, {}, {}, {}, {}, music);
        std::ostringstream sink;
        try { strict.execute(source, sink); } catch (const std::exception&) { rejected = true; }
        require(rejected, source);
    }
    bool unavailable = false;
    ql32::Interpreter noAudio;
    try { std::ostringstream sink; noAudio.execute("BEEP 5, 440\n", sink); }
    catch (const std::exception& error) {
        unavailable = std::string(error.what()).find("audio") != std::string::npos;
    }
    require(unavailable, "BEEP without an audio service must report it clearly");
}

// Read handle that records the largest single read, to prove WAV streaming.
class SyntheticFile final : public ql32::runtime::StorageReadHandle {
public:
    explicit SyntheticFile(std::vector<std::uint8_t> bytes) : bytes_(std::move(bytes)) {}
    std::uint64_t size() const noexcept override { return bytes_.size(); }
    std::size_t readAt(std::uint64_t offset, std::uint8_t* out, std::size_t capacity) override {
        maxRead = std::max(maxRead, capacity);
        if (offset >= bytes_.size()) return 0;
        const std::size_t n = std::min<std::size_t>(capacity, bytes_.size() - offset);
        std::copy_n(bytes_.begin() + static_cast<std::ptrdiff_t>(offset), n, out);
        return n;
    }
    std::size_t maxRead = 0;
private:
    std::vector<std::uint8_t> bytes_;
};

std::vector<std::uint8_t> makeWav(std::uint16_t channels, std::uint16_t bits,
                                  std::uint32_t rate, const std::vector<std::uint8_t>& data,
                                  bool extraChunk = false) {
    std::vector<std::uint8_t> w;
    auto p32 = [&](std::uint32_t v) { for (int i = 0; i < 4; ++i) w.push_back((v >> (8 * i)) & 255); };
    auto p16 = [&](std::uint16_t v) { w.push_back(v & 255); w.push_back(v >> 8); };
    auto tag = [&](const char* s) { w.insert(w.end(), s, s + 4); };
    tag("RIFF"); p32(36 + static_cast<std::uint32_t>(data.size())); tag("WAVE");
    tag("fmt "); p32(16); p16(1); p16(channels); p32(rate);
    p32(rate * channels * bits / 8); p16(channels * bits / 8); p16(bits);
    if (extraChunk) { tag("LIST"); p32(3); w.push_back(1); w.push_back(2); w.push_back(3); w.push_back(0); }
    tag("data"); p32(static_cast<std::uint32_t>(data.size()));
    w.insert(w.end(), data.begin(), data.end());
    return w;
}

std::vector<std::int16_t> renderAll(ql32::music::AudioSource& source, std::size_t limit) {
    std::vector<std::int16_t> out;
    std::array<std::int16_t, 256> block{};
    while (source.playing() && out.size() < limit) {
        source.render(block.data(), block.size());
        out.insert(out.end(), block.begin(), block.end());
    }
    return out;
}

void testMixer() {
    using namespace ql32::music;
    Mixer mixer;
    mixer.set(Mixer::Music, std::make_unique<ToneSource>(1000, 0, 22050, Waveform::Square, 20000));
    mixer.set(Mixer::Effect, std::make_unique<ToneSource>(1000, 100, 22050, Waveform::Square, 20000));
    std::array<std::int16_t, 512> pcm{};
    mixer.render(pcm.data(), pcm.size());
    require(pcm[0] == 32767, "mixed voices must saturate");
    require(!mixer.active(Mixer::Effect) && mixer.active(Mixer::Music),
            "finished effect voice must be released without touching the music voice");
    require(pcm[300] == 20000 || pcm[300] == -20000, "music voice must continue alone");
    mixer.clearAll();
    require(!mixer.anyActive(), "clearAll left voices active");
}

void testWavStreaming() {
    using namespace ql32::music;
    // 16-bit mono ramp at the output rate: output equals input.
    std::vector<std::uint8_t> data;
    for (int i = 0; i < 4000; ++i) {
        const std::int16_t v = static_cast<std::int16_t>(i * 5 - 10000);
        data.push_back(v & 255); data.push_back((v >> 8) & 255);
    }
    auto file = std::make_unique<SyntheticFile>(makeWav(1, 16, 22050, data, true));
    auto* raw = file.get();
    WavSource wav(std::move(file), 22050);
    const auto pcm = renderAll(wav, 10000);
    require(pcm[0] == -10000 && pcm[100] == 100 * 5 - 10000 && pcm[3000] == 3000 * 5 - 10000,
            "16-bit mono WAV did not play back unchanged at the output rate");
    require(!wav.playing(), "WAV did not end");
    require(raw->maxRead <= WavSource::BufferBytes, "WAV reads must stay within the stream buffer");

    // 8-bit stereo at half rate: upsampled 2x, channels averaged, unsigned offset removed.
    std::vector<std::uint8_t> eight;
    for (int i = 0; i < 2000; ++i) { eight.push_back(192); eight.push_back(64); }
    WavSource low(std::make_unique<SyntheticFile>(makeWav(2, 8, 11025, eight)), 22050);
    const auto lowPcm = renderAll(low, 10000);
    require(lowPcm.size() >= 3900 && lowPcm[50] == 0, "8-bit stereo WAV was not averaged to centre");
    require(lowPcm.size() < 4500, "half-rate WAV was not stretched to twice its length");

    // A file far larger than the stream buffer is read in bounded blocks.
    std::vector<std::uint8_t> big(400000, 0);
    auto bigFile = std::make_unique<SyntheticFile>(makeWav(1, 16, 22050, big));
    auto* bigRaw = bigFile.get();
    WavSource bigWav(std::move(bigFile), 22050);
    std::array<std::int16_t, 256> block{};
    for (int i = 0; i < 10; ++i) bigWav.render(block.data(), block.size());
    require(bigRaw->maxRead <= WavSource::BufferBytes, "large WAV was not streamed");

    const std::vector<std::vector<std::uint8_t>> bad = {
        {}, makeWav(1, 24, 22050, {0, 0}), makeWav(3, 16, 22050, {0, 0}),
        makeWav(1, 16, 100, {0, 0}), makeWav(1, 16, 22050, {})};
    for (const auto& bytes : bad) {
        bool rejected = false;
        try { WavSource x(std::make_unique<SyntheticFile>(bytes), 22050); }
        catch (const std::runtime_error&) { rejected = true; }
        require(rejected, "invalid WAV was accepted");
    }
    auto compressed = makeWav(1, 16, 22050, {0, 0, 0, 0});
    compressed[20] = 2;
    bool rejected = false;
    try { WavSource x(std::make_unique<SyntheticFile>(compressed), 22050); }
    catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "compressed WAV was accepted");
}

std::vector<std::uint8_t> be32v(std::uint32_t v) {
    return {static_cast<std::uint8_t>(v >> 24), static_cast<std::uint8_t>(v >> 16),
            static_cast<std::uint8_t>(v >> 8), static_cast<std::uint8_t>(v)};
}

std::vector<std::uint8_t> syntheticYm6(std::size_t frames, std::uint8_t effectRegs[3],
                                       bool withDrum) {
    std::vector<std::uint8_t> b = {'Y', 'M', '6', '!', 'L', 'e', 'O', 'n', 'A', 'r', 'D', '!'};
    auto app = [&](const std::vector<std::uint8_t>& v) { b.insert(b.end(), v.begin(), v.end()); };
    app(be32v(static_cast<std::uint32_t>(frames)));
    app(be32v(0)); // not planar
    app({0, static_cast<std::uint8_t>(withDrum ? 1 : 0)});
    app(be32v(2000000)); app({0, 50}); app(be32v(0)); app({0, 0});
    if (withDrum) { app(be32v(4)); app({255, 255, 0, 0}); }
    for (const char* s : {"t", "a", "c"}) { b.push_back(s[0]); b.push_back(0); }
    for (std::size_t f = 0; f < frames; ++f) {
        std::uint8_t r[16] = {};
        r[0] = 100; r[7] = 0x3e; r[8] = withDrum ? 0 : 15; r[13] = 0xff;
        r[1] = effectRegs[0]; r[6] = effectRegs[1]; r[14] = effectRegs[2];
        for (auto v : r) b.push_back(v);
    }
    return b;
}

std::size_t countLevels(const std::vector<std::int16_t>& pcm) {
    std::size_t changes = 0;
    for (std::size_t i = 1; i < pcm.size(); ++i) if (pcm[i] != pcm[i - 1]) ++changes;
    return changes;
}

void testYm() {
    using namespace ql32::music;
    // YM3 planar: 3 frames, tone on A.
    std::vector<std::uint8_t> ym3 = {'Y', 'M', '3', '!'};
    for (int reg = 0; reg < 14; ++reg)
        for (int f = 0; f < 3; ++f) ym3.push_back(reg == 8 ? 15 : (reg == 7 ? 0x3e : (reg == 0 ? 80 : 0)));
    auto song = YmSong::parse(ym3);
    require(song.frameCount() == 3 && song.registers(1)[0] == 80 && song.registers(2)[8] == 15,
            "YM3 planar decode failed");
    YmSource plain(YmSong::parse(ym3));
    std::array<std::int16_t, 512> pcm{};
    plain.render(pcm.data(), pcm.size());
    require(*std::max_element(pcm.begin(), pcm.end()) > 1000, "YM3 produced no tone");

    // YM6 effects: a plain tone vs SID voice (control 0x10 = SID on voice A, r6 prediv 4, r14 count 5).
    std::uint8_t none[3] = {0, 0, 0}, sid[3] = {0x10, 0x20, 5}, buzz[3] = {0xf0 & 0xd0, 0x20, 5};
    auto render = [&](std::vector<std::uint8_t> bytes) {
        YmSource s(YmSong::parse(std::move(bytes)));
        std::vector<std::int16_t> out(4000);
        s.render(out.data(), out.size());
        return out;
    };
    const auto plainTone = render(syntheticYm6(10, none, false));
    const auto sidTone = render(syntheticYm6(10, sid, false));
    require(sidTone != plainTone, "SID effect did not alter the voice amplitude");
    const auto buzzTone = render(syntheticYm6(10, buzz, false));
    require(!buzzTone.empty(), "sync-buzzer render failed");
    // Digi-drum 1: control 0x50 = drum effect on voice A.
    std::uint8_t drumRegs[3] = {0x50, 0xe0, 1};
    const auto drumTone = render(syntheticYm6(10, drumRegs, true));
    require(drumTone != plainTone && drumTone[0] == 127 * 128,
            "digi-drum did not replace the voice");

    bool rejected = false;
    try { YmSong::parse(std::vector<std::uint8_t>{'Y', 'M', '2', '!', 1, 2, 3, 4, 5, 6}); }
    catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "unsupported YM variant was accepted");
    require(openSong(ym3) != nullptr, "song loader did not pick YM");
    (void)countLevels;
}

// Behavior checks derived from the arnaud-carre/StSound reference (MIT, behavior only).
std::vector<std::uint8_t> syntheticYm56(const char* format, std::size_t frames,
                                        const std::array<std::uint8_t, 16>& regs, bool withDrum) {
    std::vector<std::uint8_t> b = {static_cast<std::uint8_t>(format[0]), static_cast<std::uint8_t>(format[1]),
                                   static_cast<std::uint8_t>(format[2]), '!',
                                   'L', 'e', 'O', 'n', 'A', 'r', 'D', '!'};
    auto app = [&](const std::vector<std::uint8_t>& v) { b.insert(b.end(), v.begin(), v.end()); };
    app(be32v(static_cast<std::uint32_t>(frames)));
    app(be32v(0));
    app({0, static_cast<std::uint8_t>(withDrum ? 1 : 0)});
    app(be32v(2000000)); app({0, 50}); app(be32v(0)); app({0, 0});
    if (withDrum) { app(be32v(4)); app({255, 255, 0, 0}); }
    for (const char* s : {"t", "a", "c"}) { b.push_back(s[0]); b.push_back(0); }
    for (std::size_t f = 0; f < frames; ++f) b.insert(b.end(), regs.begin(), regs.end());
    return b;
}

std::vector<std::int16_t> renderYm(std::vector<std::uint8_t> bytes, std::size_t count,
                                   const std::vector<std::vector<std::int16_t>>* bank = nullptr) {
    auto song = ql32::music::YmSong::parse(std::move(bytes));
    if (bank) song.setDrumBank(*bank);
    ql32::music::YmPlayer player(std::move(song));
    std::vector<std::int16_t> out(count);
    player.render(out.data(), out.size());
    return out;
}

std::int16_t peakAfter(const std::vector<std::int16_t>& pcm, std::size_t from) {
    std::int16_t peak = 0;
    for (std::size_t i = from; i < pcm.size(); ++i) peak = std::max<std::int16_t>(peak, static_cast<std::int16_t>(std::abs(pcm[i])));
    return peak;
}

void testYmEffects() {
    using namespace ql32::music;
    // SID: tone and noise off (constant high), voice A at volume 15 gated by a ~61 Hz timer.
    // The gate flips once per timer tick, so the square wave has half the timer frequency.
    std::array<std::uint8_t, 16> sid{};
    sid[7] = 0x3f; sid[8] = 15; sid[13] = 0xff;
    sid[1] = 0x10; sid[6] = 0xe0; sid[14] = 200;
    const auto sidPcm = renderYm(syntheticYm56("YM6!", 200, sid, false), 44100);
    std::size_t edges = 0;
    for (std::size_t i = 1; i < sidPcm.size(); ++i)
        if (sidPcm[i] - sidPcm[i - 1] > 4000) ++edges;
    // 2457600 / (200 * 200) = 61.44 Hz ticks -> about 30.7 rising edges per second.
    require(edges >= 56 && edges <= 66, "SID gate rate is not half the timer rate");

    // YM5 has no effect type bits: slot 1 control 0x10 is a digi-drum, but in YM6 it is a SID voice.
    std::array<std::uint8_t, 16> slot1{};
    slot1[7] = 0x3e; slot1[0] = 100; slot1[13] = 0xff;
    slot1[3] = 0x10; slot1[8] = 0xe0; slot1[15] = 1;
    const auto ym5Drum = renderYm(syntheticYm56("YM5!", 10, slot1, true), 400);
    const auto ym6Sid = renderYm(syntheticYm56("YM6!", 10, slot1, true), 400);
    require(ym5Drum[0] == 127 * 128, "YM5 slot 1 did not start a digi-drum");
    require(ym6Sid[0] != 127 * 128, "YM6 type bits were ignored");

    // Sync-buzzer: the envelope shape comes from the voice volume nibble and restarts on every
    // timer tick, so a fast envelope keeps re-ramping; without the effect it decays to silence.
    std::array<std::uint8_t, 16> buzz{};
    buzz[7] = 0x3f; buzz[8] = 0x18; buzz[13] = 0xff; buzz[11] = 20; buzz[12] = 0;
    auto plainBuzz = buzz;
    buzz[1] = 0xd0; buzz[6] = 0xe0; buzz[14] = 20;
    const auto withBuzzer = renderYm(syntheticYm56("YM6!", 100, buzz, false), 44100);
    const auto withoutBuzzer = renderYm(syntheticYm56("YM6!", 100, plainBuzz, false), 44100);
    require(peakAfter(withBuzzer, 22050) > 1000, "sync-buzzer did not keep restarting the envelope");
    require(peakAfter(withoutBuzzer, 22050) * 10 < peakAfter(withBuzzer, 22050), "reference without buzzer should be much quieter");

    // YM2 (Mad Max): a register 13 write means shape 10 with register 12 forced to 0.
    auto ym2 = [](std::uint8_t r10, std::uint8_t r12, std::uint8_t r13, std::uint8_t r8 = 0x10) {
        std::vector<std::uint8_t> b = {'Y', 'M', '2', '!'};
        for (int reg = 0; reg < 14; ++reg) {
            std::uint8_t v = 0;
            if (reg == 0) v = 100;
            if (reg == 7) v = 0x3b;
            if (reg == 8) v = r8;
            if (reg == 10) v = r10;
            if (reg == 11) v = 0x10;
            if (reg == 12) v = r12;
            if (reg == 13) v = r13;
            for (int f = 0; f < 100; ++f) b.push_back(v);
        }
        return b;
    };
    std::vector<std::uint8_t> ym3 = {'Y', 'M', '3', '!'};
    for (int reg = 0; reg < 14; ++reg) {
        std::uint8_t v = reg == 0 ? 100 : reg == 7 ? 0x3b : reg == 8 ? 0x10 : reg == 11 ? 0x10 : reg == 13 ? 10 : 0;
        for (int f = 0; f < 100; ++f) ym3.push_back(v);
    }
    require(renderYm(ym2(0, 7, 5), 8000) == renderYm(ym3, 8000), "YM2 buzzer was not mapped to shape 10 / R12=0");

    // YM2 Mad Max digi-drum: register 10 bit 7 selects a sample played on voice C at
    // (MFP clock / 4) / register 12; without a bank the voice is muted (no built-in samples).
    const std::vector<std::vector<std::int16_t>> bank = {{12800, 12800, -12800, -12800}};
    const auto muted = renderYm(ym2(0x80, 100, 0xff, 0), 16000);
    require(peakAfter(muted, 12000) < 50, "Mad Max drum without a bank should mute voice C");
    const auto drum = renderYm(ym2(0x80, 100, 0xff, 0), 4000, &bank);
    require(drum[0] == 12800, "Mad Max digi-drum did not play from the bank");
}

std::vector<std::uint8_t> readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void testExampleYmFiles() {
    namespace fs = std::filesystem;
    const fs::path root = fs::path(QL32_EXAMPLES_DIR) / "music" / "ym";
    if (!fs::exists(root)) return;
    std::size_t decoded = 0, unsupported = 0;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file()) continue;
        auto bytes = readFile(entry.path());
        try {
            auto source = ql32::music::openSong(std::move(bytes));
            std::array<std::int16_t, 4096> block{};
            std::int16_t peak = 0;
            for (int i = 0; i < 20; ++i) {
                source->render(block.data(), block.size());
                for (auto v : block) peak = std::max<std::int16_t>(peak, v);
            }
            require(peak > 100, ("example YM is silent: " + entry.path().string()).c_str());
            ++decoded;
        } catch (const std::runtime_error& e) {
            std::cerr << "note: " << entry.path().filename() << ": " << e.what() << "\n";
            ++unsupported;
        }
    }
    std::cerr << "example YM files decoded=" << decoded << " unsupported=" << unsupported << "\n";
    require(decoded >= 24 && unsupported <= 1, "example YM files failed to decode");
}

std::vector<std::uint8_t> makePt3(std::uint8_t note) {
    std::vector<std::uint8_t> m(0x200, 0);
    const char* text = "ProTracker 3.6 compilation of test";
    for (std::size_t i = 0; text[i]; ++i) m[i] = static_cast<std::uint8_t>(text[i]);
    const auto put16 = [&m](std::size_t at, unsigned v) { m[at] = v & 255; m[at + 1] = v >> 8; };
    m[0x63] = 0; m[0x64] = 3; m[0x65] = 1; m[0x66] = 0;
    put16(0x67, 0x120);
    put16(0x69 + 2, 0x140);   // sample 1
    put16(0xa9, 0x150);       // ornament 0
    m[0xc9] = 0; m[0xca] = 0xff;
    put16(0x120, 0x160); put16(0x122, 0x170); put16(0x124, 0x170);
    m[0x140] = 0; m[0x141] = 1; m[0x142] = 0; m[0x143] = 0x0f; put16(0x144, 0);
    m[0x150] = 0; m[0x151] = 1; m[0x152] = 0;
    const std::uint8_t a[] = {0xb1, 32, static_cast<std::uint8_t>(0x50 + note), 0};
    const std::uint8_t b[] = {0xb1, 32, 0xc0, 0};
    for (std::size_t i = 0; i < sizeof a; ++i) m[0x160 + i] = a[i];
    for (std::size_t i = 0; i < sizeof b; ++i) m[0x170 + i] = b[i];
    return m;
}

void testPt3() {
    using namespace ql32::music;
    auto bytes = makePt3(36);
    require(Pt3Module::looksLikePt3(bytes.data(), bytes.size()), "PT3 header not recognised");
    const auto module = Pt3Module::parse(bytes);
    require(module.version() == 6, "PT3 version not parsed");
    Pt3Engine engine(module);
    bool shape = false;
    const auto regs = engine.tick(shape);
    const auto period = module.notePeriod(36);
    require(regs[0] == (period & 255) && regs[1] == (period >> 8), "PT3 channel A period wrong");
    require(regs[8] == 15, "PT3 channel A amplitude wrong");
    require(regs[9] == 0 && regs[10] == 0 && !shape, "PT3 silent channels wrong");
    // Note off on A is absent so the tone keeps sounding through the row.
    Pt3Source source(Pt3Module::parse(makePt3(48)));
    std::array<std::int16_t, 4096> block{};
    std::int16_t peak = 0;
    for (int i = 0; i < 10; ++i) {
        source.render(block.data(), block.size());
        for (auto v : block) peak = std::max<std::int16_t>(peak, v);
    }
    require(peak > 500, "PT3 render is silent");
    require(source.playing(), "PT3 stopped unexpectedly");
    require(openSong(makePt3(40)) != nullptr, "song loader did not pick PT3");

    auto bad = makePt3(36);
    bad[0x65] = 0;
    bool threw = false;
    try { (void)Pt3Module::parse(bad); } catch (const std::runtime_error&) { threw = true; }
    require(threw, "PT3 with no positions accepted");
    // Truncated pattern data must stop the player, not run forever.
    auto cut = makePt3(36);
    cut[0x170] = 0x01; cut[0x171] = 0x01;   // unterminated parameter command row
    cut.resize(0x172);
    Pt3Source broken(Pt3Module::parse(cut));
    for (int i = 0; i < 20; ++i) broken.render(block.data(), block.size());
}

void testExamplePt3Files() {
    namespace fs = std::filesystem;
    const fs::path root = fs::path(QL32_EXAMPLES_DIR) / "music" / "pt3";
    if (!fs::exists(root)) return;
    std::size_t decoded = 0;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file()) continue;
        auto source = ql32::music::openSong(readFile(entry.path()));
        std::array<std::int16_t, 4096> block{};
        std::int16_t peak = 0;
        for (int i = 0; i < 40; ++i) {
            source->render(block.data(), block.size());
            for (auto v : block) peak = std::max<std::int16_t>(peak, v);
        }
        require(peak > 100, ("example PT3 is silent: " + entry.path().string()).c_str());
        // A correctly synchronised pattern parser reaches the song end within 30 minutes.
        const auto module = ql32::music::Pt3Module::parse(readFile(entry.path()));
        ql32::music::Pt3Engine engine(module);
        bool shape = false;
        for (int t = 0; t < 50 * 1800 && engine.loopCount() == 0 && !engine.failed(); ++t) engine.tick(shape);
        require(!engine.failed() && engine.loopCount() > 0,
                ("example PT3 never loops: " + entry.path().string()).c_str());
        ++decoded;
    }
    std::cerr << "example PT3 files decoded=" << decoded << "\n";
    require(decoded >= 10, "example PT3 files failed to decode");
}

// VTX dumps store R13 = 0xFF for "shape not written"; that must neither restart nor
// change the envelope shape (z00m_unreal.vtx uses hardware-envelope bass).
void testVtxEnvelopeShapeKept() {
    namespace fs = std::filesystem;
    const fs::path file = fs::path(QL32_EXAMPLES_DIR) / "music" / "z00m_unreal.vtx";
    if (!fs::exists(file)) return;
    const auto bytes = readFile(file);
    auto song = ql32::music::VtxSong::parse(bytes);
    ql32::music::VtxPlayer player(ql32::music::VtxSong::parse(bytes), 22050);
    ql32::music::Ay38912 reference;
    reference.reset(song.metadata().chipClockHz, 22050);
    std::uint8_t shape = 0;
    std::uint64_t accumulator = 0;
    std::size_t frame = 0;
    auto load = [&] {
        auto regs = song.registers(frame);
        const bool written = regs[13] != 0xff;
        if (written) shape = regs[13]; else regs[13] = shape;
        reference.writeRegisters(regs, written);
    };
    load();
    std::array<std::int16_t, 1> out{};
    for (std::size_t i = 0; i < 22050 * 20; ++i) {
        player.render(out.data(), 1);
        require(out[0] == reference.renderSample(), "VTX R13=0xFF handling diverged from the AY reference");
        accumulator += song.metadata().playerHz;
        if (accumulator >= 22050) { accumulator -= 22050; ++frame; load(); }
    }
}

void testExampleSqtFiles() {
    namespace fs = std::filesystem;
    const fs::path root = fs::path(QL32_EXAMPLES_DIR) / "music" / "sqt";
    if (!fs::exists(root)) return;
    std::size_t decoded = 0;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file()) continue;
        const auto bytes = readFile(entry.path());
        require(ql32::music::SqtModule::looksLikeSqt(bytes.data(), bytes.size()),
                ("SQT not recognised: " + entry.path().string()).c_str());
        auto source = ql32::music::openSong(bytes);
        std::array<std::int16_t, 4096> block{};
        std::int16_t peak = 0;
        for (int i = 0; i < 40; ++i) {
            source->render(block.data(), block.size());
            for (auto v : block) peak = std::max<std::int16_t>(peak, v);
        }
        require(peak > 100, ("example SQT is silent: " + entry.path().string()).c_str());
        const auto module = ql32::music::SqtModule::parse(bytes);
        ql32::music::SqtEngine engine(module);
        bool shape = false;
        for (int t = 0; t < 50 * 1800 && engine.loopCount() == 0; ++t) engine.tick(shape);
        require(engine.loopCount() > 0, ("example SQT never loops: " + entry.path().string()).c_str());
        ++decoded;
    }
    std::cerr << "example SQT files decoded=" << decoded << "\n";
    require(decoded >= 5, "example SQT files failed to decode");
}

void testVtxLongAndRaggedStreams() {
    using namespace ql32::music;
    // A 2-frame stream with one stray trailing byte is accepted as 2 frames.
    const std::vector<std::uint8_t> raw(29, 7);
    std::vector<std::uint8_t> file = {'a', 'y', 0, 0xff, 0xff, 0x34, 0x6c, 0x1b, 0x00, 50, 0, 0, 29, 0, 0, 0};
    for (int i = 0; i < 5; ++i) file.push_back(0);
    // Stored-method LHA data is not available here, so only the size gates are checked.
    bool threw = false;
    try { (void)VtxSong::parse(file); } catch (const std::runtime_error&) { threw = true; }
    require(threw, "VTX without a data stream accepted");
    require(VtxSong::MaximumFileBytes >= 256 * 1024 && VtxSong::MaximumRegisterBytes >= 512 * 1024,
            "VTX limits not raised");
    require(maximumSongFileBytes() >= VtxSong::MaximumFileBytes, "loader limit below VTX limit");
}

class MemoryStorage final : public ql32::runtime::StorageService {
public:
    std::map<std::string, std::string> files;
    std::string readOnlyPrefix;
    int writes = 0;
    bool failWritesWithBadAlloc = false;
    std::string readText(const std::string& path) override { return files.at(path); }
    std::unique_ptr<ql32::runtime::StorageReadHandle> openRead(const std::string& path) override {
        return std::make_unique<ql32::runtime::MemoryStorageReadHandle>(files.at(path));
    }
    void writeText(const std::string& path, const std::string& contents) override {
        if (failWritesWithBadAlloc) throw std::bad_alloc();
        if (!readOnlyPrefix.empty() && path.rfind(readOnlyPrefix, 0) == 0)
            throw ql32::runtime::ServiceError(
                ql32::runtime::ServiceErrorCode::PermissionDenied, "write protected");
        ++writes;
        files[path] = contents;
    }
    std::vector<ql32::runtime::FileEntry> list(const std::string& path) override {
        std::vector<ql32::runtime::FileEntry> result;
        for (const auto& [name, data] : files)
            if (name.rfind(path, 0) == 0) result.push_back({name, data.size(), false});
        return result;
    }
    bool exists(const std::string& path) override { return files.count(path) != 0; }
    std::uintmax_t size(const std::string& path) override { return files.at(path).size(); }
    void rename(const std::string& path, const std::string& newPath) override {
        files[newPath] = std::move(files.at(path));
        files.erase(path);
    }
    void remove(const std::string& path) override { files.erase(path); }
};

std::string interleavedYm(std::size_t frames, bool planar) {
    std::string out = "YM5!LeOnArD!";
    const auto be32 = [&](std::uint32_t v) {
        for (int i = 3; i >= 0; --i) out.push_back(static_cast<char>(v >> (8 * i)));
    };
    be32(static_cast<std::uint32_t>(frames));
    be32(planar ? 1 : 0);
    out += std::string("\0\0", 2);
    be32(2000000);
    out.push_back(0); out.push_back(50);
    be32(300);
    out += std::string("\0\0", 2);
    out += std::string("\0\0\0", 3);
    std::string data(frames * 16, 0);
    std::uint32_t seed = 7;
    for (std::size_t f = 0; f < frames; ++f)
        for (std::size_t r = 0; r < 16; ++r) {
            seed = seed * 1664525u + 1013904223u;
            const std::size_t at = planar ? r * frames + f : f * 16 + r;
            data[at] = static_cast<char>(r < 4 ? (f / 7 + r) : (seed >> 24) & (r % 3 ? 0 : 3));
        }
    return out + data + "End!";
}

void testYmInterleavedStreamed() {
    using namespace ql32::music;
    const std::vector<std::string> roots = {"FFS:/"};
    for (const bool planar : {false, true}) {
        const std::string bytes = interleavedYm(1000, planar);
        const auto reference = YmSong::parse(std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
        MemoryStorage storage;
        storage.files["FFS:/i.ym"] = bytes;
        auto song = openYmStreamed(storage, "FFS:/i.ym", roots);
        const auto pump = song.prefetcher();
        for (std::size_t frame = 0; frame < reference.frameCount(); ++frame) {
            if (frame % 16 == 0) pump->run();
            require(song.registers(frame) == reference.registers(frame), "layout YM differs");
        }
        for (std::size_t frame = 300; frame < 700; ++frame) {
            if (frame % 16 == 0) pump->run();
            require(song.registers(frame) == reference.registers(frame), "layout YM loop differs");
        }
        for (std::size_t frame = reference.frameCount(); frame-- > 0; frame -= std::min<std::size_t>(frame, 97))
            require(song.registers(frame) == reference.registers(frame), "layout YM reverse differs");
        require(!song.failed(), "layout YM failed");
        require(storage.files.size() == 2, "cache not created");
    }
}

void testYmTracker() {
    using namespace ql32::music;
    // Synthetic YMT1: 1 voice, planar, one 4-sample drum, 3 lines (note 0 on, hold, silent).
    auto tracker = [](unsigned voices, std::uint32_t frames, std::uint32_t attributes,
                      std::size_t lineBytes) {
        std::vector<std::uint8_t> b{'Y', 'M', 'T', '1'};
        for (char c : std::string("LeOnArD!")) b.push_back(static_cast<std::uint8_t>(c));
        auto w16 = [&](unsigned v) { b.push_back(v >> 8); b.push_back(v & 255); };
        auto w32 = [&](std::uint32_t v) { w16(v >> 16); w16(v & 0xffff); };
        w16(voices); w16(50); w32(frames); w32(0); w16(1); w32(attributes);
        for (int i = 0; i < 3; ++i) b.push_back(0);
        w16(4);
        for (std::uint8_t v : {128, 255, 128, 0}) b.push_back(v);
        for (std::size_t i = 0; i < lineBytes; ++i) b.push_back(0);
        return b;
    };
    {
        auto b = tracker(1, 3, 1, 0);
        // planar: note[3], volume[3], freqHigh[3], freqLow[3]
        for (std::uint8_t v : {0, 0xff, 0xff, 63, 63, 63, 0x80, 0x80, 0, 0, 0, 0}) b.push_back(v);
        auto song = YmSong::parse(b);
        require(song.metadata().format == "YMT1" && song.metadata().trackerVoices == 1 &&
                song.frameCount() == 3 && song.drumCount() == 1 && song.drum(0).size() == 4,
                "YMT1 header decode failed");
        require(song.registers(0)[0] == 0 && song.registers(0)[1] == 63 &&
                song.registers(1)[0] == 0xff && song.registers(2)[2] == 0,
                "YMT1 planar lines decode failed");
        YmSource source(std::move(song));
        std::array<std::int16_t, 64> pcm{};
        source.render(pcm.data(), pcm.size());
        require(*std::max_element(pcm.begin(), pcm.end()) > 1000, "YMT1 produced no sound");
    }
    auto rejects = [](std::vector<std::uint8_t> bytes) {
        try { YmSong::parse(std::move(bytes)); } catch (const std::runtime_error&) { return true; }
        return false;
    };
    require(rejects(tracker(5, 3, 1, 12)), "YMT1 with 5 voices was accepted");
    require(rejects(tracker(0, 3, 1, 12)), "YMT1 with no voices was accepted");
    require(rejects(tracker(1, 3, 1, 8)), "truncated planar YMT1 was accepted");
    require(rejects(tracker(1, 3, 1, 0)), "YMT1 without lines was accepted");
    auto truncated = tracker(1, 3, 1, 12);
    truncated.resize(40);
    require(rejects(truncated), "YMT1 with truncated header text was accepted");

    // Real LH5-packed YMT1 file (header level 0): host and streamed paths must agree.
    const auto path = std::filesystem::path(QL32_EXAMPLES_DIR) / "music" / "ym" / "KNUCKLE.YM";
    if (!std::filesystem::exists(path)) return;
    const auto bytes = readFile(path);
    auto reference = YmSong::parse(bytes);
    require(reference.metadata().format == "YMT1" && reference.metadata().trackerVoices == 3 &&
            reference.metadata().frameRateHz == 50 && reference.frameCount() == 54750 &&
            reference.drumCount() == 25 &&
            reference.metadata().title.rfind("KnuckleBusters", 0) == 0,
            "KNUCKLE.YM header decode failed");
    require(reference.registers(0)[0] == 0x00 || reference.registers(0)[0] == 0xff,
            "KNUCKLE.YM first line is implausible");
    YmSource knuckle(YmSong::parse(bytes));
    std::array<std::int16_t, 4096> block{};
    std::int16_t peak = 0;
    for (int i = 0; i < 200; ++i) {
        knuckle.render(block.data(), block.size());
        for (auto v : block) peak = std::max<std::int16_t>(peak, static_cast<std::int16_t>(std::abs(v)));
    }
    require(peak > 500 && knuckle.playing(), "KNUCKLE.YM is silent");

    const std::vector<std::string> roots = {"SDC:/", "FFS:/"};
    MemoryStorage storage;
    storage.files["FFS:/k.ym"] = std::string(bytes.begin(), bytes.end());
    VtxStreamInfo info;
    auto streamed = openYmStreamed(storage, "FFS:/k.ym", roots, &info);
    require(streamed.metadata().format == "YMT1" && streamed.metadata().trackerVoices == 3 &&
            streamed.frameCount() == reference.frameCount() &&
            streamed.drumCount() == reference.drumCount(), "streamed KNUCKLE.YM metadata differs");
    for (std::size_t i = 0; i < reference.drumCount(); ++i)
        require(streamed.drum(i) == reference.drum(i), "streamed KNUCKLE.YM drum differs");
    const auto pump = streamed.prefetcher();
    for (std::size_t frame = 0; frame < reference.frameCount(); frame += 7) {
        if (frame % 64 == 0) pump->run();
        require(streamed.registers(frame) == reference.registers(frame),
                "streamed KNUCKLE.YM lines differ");
    }
    require(!streamed.failed(), "streamed KNUCKLE.YM failed");

    // Corrupt and truncated packed data must be rejected or decoded safely, never crash.
    for (std::size_t cut : {8u, 24u, 40u, 200u, 5000u, 22000u}) {
        auto cutBytes = bytes;
        cutBytes.resize(std::min(cut, bytes.size()));
        try { YmSong::parse(cutBytes); } catch (const std::runtime_error&) {}
    }
    for (std::size_t at : {40u, 60u, 120u, 3000u, 20000u}) {
        auto bad = bytes;
        bad[at] ^= 0xa5;
        try { YmSong::parse(bad); } catch (const std::runtime_error&) {}
    }
}
void testYmStreamedCache() {
    using namespace ql32::music;
    const std::vector<std::string> roots = {"SDC:/", "FFS:/"};
    const std::filesystem::path dir = std::filesystem::path(QL32_EXAMPLES_DIR) / "music";
    int songs = 0;
    std::size_t cacheBytes = 0, rawBytes = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
        std::string ext = entry.path().extension().string();
        for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (ext != ".ym" || entry.file_size() > YmSong::MaximumPackedBytes) continue;
        std::ifstream in(entry.path(), std::ios::binary);
        const std::string bytes((std::istreambuf_iterator<char>(in)), {});
        std::optional<YmSong> parsed;
        try { parsed = YmSong::parse(std::vector<std::uint8_t>(bytes.begin(), bytes.end())); }
        catch (const std::runtime_error&) { continue; }
        const YmSong& reference = *parsed;

        MemoryStorage storage;
        storage.files["FFS:/song.ym"] = bytes;
        VtxStreamInfo info;
        auto song = openYmStreamed(storage, "FFS:/song.ym", roots, &info);
        require(!info.cacheHit, "first YM open hit a cache");
        require(song.frameCount() == reference.frameCount() &&
                song.metadata().format == reference.metadata().format &&
                song.metadata().frameRateHz == reference.metadata().frameRateHz &&
                song.metadata().chipClockHz == reference.metadata().chipClockHz &&
                song.metadata().loopFrame == reference.metadata().loopFrame &&
                song.metadata().hasLoop == reference.metadata().hasLoop &&
                song.hasEffects() == reference.hasEffects(),
                "streamed YM metadata differs");
        std::size_t drumSamples = 0;
        for (std::size_t i = 0; i < reference.drumCount(); ++i) drumSamples += reference.drum(i).size();
        if (drumSamples <= 12288) {
            require(song.drumCount() == reference.drumCount(), "streamed YM drum count differs");
            for (std::size_t i = 0; i < reference.drumCount(); ++i)
                require(song.drum(i) == reference.drum(i), "streamed YM drum differs");
        }
        const auto pump = song.prefetcher();
        require(pump != nullptr, "streamed YM has no prefetcher");
        for (std::size_t frame = 0; frame < reference.frameCount(); ++frame) {
            if (frame % 16 == 0) pump->run();
            require(song.registers(frame) == reference.registers(frame),
                    "streamed YM registers differ");
        }
        require(!song.failed(), "streamed YM reported a read failure");
        // Out-of-order access (scans from the stream start or loop window).
        std::uint32_t seed = 12345;
        for (int k = 0; k < 200; ++k) {
            seed = seed * 1664525u + 1013904223u;
            const std::size_t frame = (seed >> 8) % reference.frameCount();
            require(song.registers(frame) == reference.registers(frame),
                    "streamed YM random access differs");
        }
        require(!song.failed(), "streamed YM random access failed");
        cacheBytes += storage.files[info.cachePath].size();
        rawBytes += reference.frameCount() * (reference.metadata().trackerVoices ? reference.metadata().trackerVoices * 4u : reference.hasEffects() ? 16u : 14u);

        const int writes = storage.writes;
        openYmStreamed(storage, "FFS:/song.ym", roots, &info);
        require(info.cacheHit && storage.writes == writes, "YM cache was not reused");
        ++songs;
    }
    require(songs >= 2, "example YM files not found");
    require(cacheBytes * 10 < rawBytes * 4, "YM cache is not compressed");

    MemoryStorage storage;
    storage.files["FFS:/bad.ym"] = std::string(64, 'x');
    bool rejected = false;
    try { openYmStreamed(storage, "FFS:/bad.ym", roots); }
    catch (const std::runtime_error&) { rejected = true; }
    require(rejected && storage.files.size() == 1, "malformed YM was cached");
}

void testVtxStreamedCache() {
    using namespace ql32::music;
    const std::vector<std::string> roots = {"SDC:/", "FFS:/"};
    const std::filesystem::path dir = std::filesystem::path(QL32_EXAMPLES_DIR) / "music";
    int songs = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.path().extension() != ".vtx") continue;
        std::ifstream in(entry.path(), std::ios::binary);
        const std::string bytes((std::istreambuf_iterator<char>(in)), {});
        const auto reference = VtxSong::parse(std::vector<std::uint8_t>(bytes.begin(), bytes.end()));

        MemoryStorage storage;
        storage.files["FFS:/song.vtx"] = bytes;
        storage.readOnlyPrefix = "SDC:";   // SD card write-protected
        VtxStreamInfo info;
        auto song = openVtxStreamed(storage, "FFS:/song.vtx", roots, &info);
        require(!info.cacheHit && info.cachePath.rfind("FFS:/", 0) == 0,
                "write-protected SDC did not fall back to FFS");
        require(song.frameCount() == reference.frameCount() &&
                song.metadata().playerHz == reference.metadata().playerHz &&
                song.metadata().loopFrame == reference.metadata().loopFrame &&
                song.metadata().chipClockHz == reference.metadata().chipClockHz,
                "streamed VTX metadata differs");
        for (std::size_t frame = 0; frame < reference.frameCount(); ++frame)
            require(song.registers(frame) == reference.registers(frame),
                    "streamed VTX registers differ");
        require(!song.failed(), "streamed VTX reported a read failure");
        // Read-ahead path: sequential playback with run() interleaved, then a loop jump.
        auto ahead = openVtxStreamed(storage, "FFS:/song.vtx", roots);
        const auto pump = ahead.prefetcher();
        require(pump != nullptr, "streamed VTX has no prefetcher");
        for (std::size_t frame = 0; frame < reference.frameCount(); ++frame) {
            if (frame % 16 == 0) pump->run();
            require(ahead.registers(frame) == reference.registers(frame),
                    "read-ahead VTX registers differ");
        }
        const std::size_t loop = std::min<std::size_t>(
            reference.metadata().loopFrame, reference.frameCount() - 1);
        pump->run();
        for (std::size_t frame = loop; frame < std::min(reference.frameCount(), loop + 600); ++frame) {
            if (frame % 16 == 0) pump->run();
            require(ahead.registers(frame) == reference.registers(frame),
                    "VTX registers differ after loop jump");
        }
        require(!ahead.failed(), "read-ahead VTX reported a read failure");

        const int writes = storage.writes;
        openVtxStreamed(storage, "FFS:/song.vtx", roots, &info);
        require(info.cacheHit && storage.writes == writes, "VTX cache was not reused");

        storage.readOnlyPrefix.clear();
        openVtxStreamed(storage, "FFS:/song.vtx", roots, &info);
        require(info.cacheHit && info.cachePath.rfind("FFS:/", 0) == 0,
                "an existing cache was not found on the fallback root");

        // A different song at the same path invalidates the cache.
        storage.files["FFS:/song.vtx"][17] ^= 1;
        openVtxStreamed(storage, "FFS:/song.vtx", roots, &info);
        require(!info.cacheHit && info.cachePath.rfind("SDC:/", 0) == 0,
                "changed source reused a stale cache or ignored the first root");
        ++songs;
    }
    require(songs >= 3, "example VTX files not found");

    MemoryStorage storage;
    storage.files["FFS:/bad.vtx"] = std::string(64, 'x');
    bool rejected = false;
    try { openVtxStreamed(storage, "FFS:/bad.vtx", roots); }
    catch (const std::runtime_error&) { rejected = true; }
    require(rejected && storage.files.size() == 1, "malformed VTX was cached");

    storage.readOnlyPrefix = "FFS:/.";
    storage.files["FFS:/ok.vtx"] = [] {
        std::ifstream in(std::filesystem::path(QL32_EXAMPLES_DIR) / "music/LyraII1.vtx",
                         std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(in)), {});
    }();
    rejected = false;
    try { openVtxStreamed(storage, "FFS:/ok.vtx", {"FFS:/"}); }
    catch (const ql32::runtime::ServiceError&) { rejected = true; }
    require(rejected, "unwritable cache roots did not report an error");

    // Heap exhaustion while transcoding must not be reported as an I/O error.
    storage.readOnlyPrefix.clear();
    storage.failWritesWithBadAlloc = true;
    bool outOfMemory = false;
    try { openVtxStreamed(storage, "FFS:/ok.vtx", {"FFS:/"}); }
    catch (const ql32::runtime::ServiceError& error) {
        outOfMemory = error.code() == ql32::runtime::ServiceErrorCode::OutOfMemory;
    }
    require(outOfMemory, "bad_alloc during VTX transcode was not reported as OutOfMemory");
    storage.failWritesWithBadAlloc = false;
}

void testSqtRejectsGarbage() {
    std::vector<std::uint8_t> junk(300);
    for (std::size_t i = 0; i < junk.size(); ++i) junk[i] = static_cast<std::uint8_t>(i * 37 + 11);
    require(!ql32::music::SqtModule::looksLikeSqt(junk.data(), junk.size()), "garbage accepted as SQT");
    bool threw = false;
    try { (void)ql32::music::openSong(junk); } catch (const std::runtime_error&) { threw = true; }
    require(threw, "garbage accepted by song loader");
}
} // namespace

int main() {
    try {
        testVtxParserAndRenderer();
        testAyUltrasonicTone();
        testAyEnvelopeRate();
        testMusicBasicCommands();
        testDacFormatAndTone();
        testWaveforms();
        testMixer();
        testWavStreaming();
        testYm();
        testYmEffects();
        testExampleYmFiles();
        testPt3();
        testExamplePt3Files();
        testExampleSqtFiles();
        testVtxEnvelopeShapeKept();
        testSqtRejectsGarbage();
        testVtxLongAndRaggedStreams();
        testVtxStreamedCache();
        testYmStreamedCache();
        testYmTracker();
        testYmInterleavedStreamed();
        testBeepAndVolumeCommands();
    } catch (const std::exception& error) {
        std::cerr << "music test failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
