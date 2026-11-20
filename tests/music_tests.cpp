#include "ql32/interpreter.hpp"
#include "ql32/music/ay38912.hpp"
#include "ql32/music/dac_format.hpp"
#include "ql32/music/tone.hpp"
#include "ql32/music/vtx.hpp"
#include "ql32/music/vtx_player.hpp"
#include "ql32/runtime/music.hpp"

#include <array>
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
        "MUSIC STOP\n", output);
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
} // namespace

int main() {
    try {
        testVtxParserAndRenderer();
        testMusicBasicCommands();
        testDacFormatAndTone();
        testWaveforms();
        testBeepAndVolumeCommands();
    } catch (const std::exception& error) {
        std::cerr << "music test failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
