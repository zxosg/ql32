#include "ql32/interpreter.hpp"
#include "ql32/music/ay38912.hpp"
#include "ql32/music/dac_format.hpp"
#include "ql32/music/mixer.hpp"
#include "ql32/music/song_loader.hpp"
#include "ql32/music/tone.hpp"
#include "ql32/music/wav.hpp"
#include "ql32/music/vtx.hpp"
#include "ql32/music/vtx_player.hpp"
#include "ql32/runtime/music.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <stdexcept>
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
    require(drumTone != plainTone && drumTone[0] == 127 * 128 / 3,
            "digi-drum did not replace the voice");

    bool rejected = false;
    try { YmSong::parse(std::vector<std::uint8_t>{'Y', 'M', '2', '!', 1, 2, 3, 4, 5, 6}); }
    catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "unsupported YM variant was accepted");
    require(openSong(ym3) != nullptr, "song loader did not pick YM");
    (void)countLevels;
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
} // namespace

int main() {
    try {
        testVtxParserAndRenderer();
        testMusicBasicCommands();
        testDacFormatAndTone();
        testWaveforms();
        testMixer();
        testWavStreaming();
        testYm();
        testExampleYmFiles();
        testBeepAndVolumeCommands();
    } catch (const std::exception& error) {
        std::cerr << "music test failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
