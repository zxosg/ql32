#include "ql32/music/pt3.hpp"

#include <cstring>
#include <stdexcept>

// Independent implementation of the ProTracker 3 module format as documented
// by the Vortex Tracker II / PT3 player behaviour. Only numeric tables
// (pt3_tables.cpp) are shared with other players; see docs for provenance.
namespace ql32::music {

bool Pt3Module::looksLikePt3(const std::uint8_t* data, std::size_t size) noexcept {
    return size >= 0x100 &&
           (std::memcmp(data, "ProTracker 3.", 13) == 0 || std::memcmp(data, "Vortex Tracker II", 17) == 0);
}

Pt3Module Pt3Module::parse(std::vector<std::uint8_t> bytes) {
    if (!looksLikePt3(bytes.data(), bytes.size())) throw std::runtime_error("not a PT3 module");
    if (bytes.size() > MaximumBytes) throw std::runtime_error("PT3 module exceeds 64 KiB");
    Pt3Module m;
    m.bytes_ = std::move(bytes);
    const std::uint8_t v = m.bytes_[13];
    m.version_ = (std::memcmp(m.bytes_.data(), "ProTracker 3.", 13) == 0 && v >= '0' && v <= '9') ? v - '0' : 6;
    if (m.positionCount() == 0 || m.loopPosition() >= m.positionCount())
        throw std::runtime_error("PT3 module has an invalid position list");
    if (0xc9 + m.positionCount() > m.size() || m.patternsPointer() + 6 > m.size())
        throw std::runtime_error("PT3 module is truncated");
    return m;
}

std::uint16_t Pt3Module::notePeriod(unsigned note) const noexcept {
    if (note > 95) note = 95;
    const bool old = version_ <= 3;
    switch (toneTable()) {
    case 0: return old ? pt3_tables::NotePt33[note] : pt3_tables::NotePt34[note];
    case 1: return pt3_tables::NoteSt[note];
    case 2: return old ? pt3_tables::NoteAsm33[note] : pt3_tables::NoteAsm34[note];
    default: return old ? pt3_tables::NoteReal33[note] : pt3_tables::NoteReal34[note];
    }
}

Pt3Engine::Pt3Engine(const Pt3Module& module) noexcept : module_(module) {
    delay_ = module_.initialDelay();
    delayCounter_ = 1;
    loadPosition();
    for (auto& c : ch_) {
        selectOrnament(c, 0);
        selectSample(c, 1);
        c.volume = 15;
        c.skipCounter = 1;
        c.notesToSkip = 1;
    }
}

void Pt3Engine::loadPosition() noexcept {
    const std::size_t pattern = static_cast<std::size_t>(module_.positionPattern(position_)) * 2;
    const std::size_t table = module_.patternsPointer() + pattern;
    for (unsigned i = 0; i < 3; ++i) ch_[i].address = module_.word(table + i * 2);
}

void Pt3Engine::selectSample(Channel& c, unsigned n) noexcept {
    const std::size_t p = module_.samplePointer(n);
    c.sampleLoop = module_.at(p);
    c.sampleLength = module_.at(p + 1);
    c.samplePtr = p + 2;
}

void Pt3Engine::selectOrnament(Channel& c, unsigned n) noexcept {
    const std::size_t p = module_.ornamentPointer(n);
    c.ornamentLoop = module_.at(p);
    c.ornamentLength = module_.at(p + 1);
    c.ornamentPtr = p + 2;
}

void Pt3Engine::interpret(Channel& c) noexcept {
    const unsigned previousNote = c.note;
    const int previousSliding = c.tonSliding;
    std::array<unsigned, 10> order{};
    unsigned counter = 0;
    bool quit = false;
    const auto resetNote = [&c] {
        c.samplePos = 0;
        c.amplitudeSliding = 0;
        c.noiseSliding = 0;
        c.envelopeSliding = 0;
        c.ornamentPos = 0;
        c.tonSlideCount = 0;
        c.tonSliding = 0;
        c.tonAccumulator = 0;
        c.currentOnOff = 0;
    };
    const auto startEnvelope = [this, &c](unsigned shape) {
        pendingShape_ = static_cast<int>(shape);
        envBase_ = static_cast<std::uint16_t>((module_.at(c.address + 1) << 8) | module_.at(c.address + 2));
        c.address += 2;
        c.envelopeEnabled = true;
        envSlide_ = 0;
        curEnvDelay_ = 0;
    };
    do {
        if (c.address >= module_.size()) {
            failed_ = true;
            return;
        }
        const unsigned v = module_.at(c.address);
        if (v >= 0xf0) {
            selectOrnament(c, v - 0xf0);
            ++c.address;
            selectSample(c, module_.at(c.address) / 2);
            c.envelopeEnabled = false;
            c.ornamentPos = 0;
        } else if (v >= 0xd1 && v <= 0xef) {
            selectSample(c, v - 0xd0);
        } else if (v == 0xd0) {
            quit = true;
        } else if (v >= 0xc1) {
            c.volume = v - 0xc0;
        } else if (v == 0xc0) {
            resetNote();
            c.enabled = false;
            quit = true;
        } else if (v >= 0xb2) {
            startEnvelope(v - 0xb1);
            c.ornamentPos = 0;
        } else if (v == 0xb1) {
            ++c.address;
            c.notesToSkip = module_.at(c.address);
        } else if (v == 0xb0) {
            c.envelopeEnabled = false;
            c.ornamentPos = 0;
        } else if (v >= 0x50) {
            c.note = v - 0x50;
            resetNote();
            c.enabled = true;
            quit = true;
        } else if (v >= 0x40) {
            selectOrnament(c, v - 0x40);
            c.ornamentPos = 0;
        } else if (v >= 0x20) {
            noiseBase_ = v - 0x20;
        } else if (v >= 0x10) {
            if (v == 0x10) {
                c.envelopeEnabled = false;
            } else {
                startEnvelope(v - 0x10);
            }
            ++c.address;
            selectSample(c, module_.at(c.address) / 2);
            c.ornamentPos = 0;
        } else if (v == 1 || v == 2 || v == 3 || v == 4 || v == 5 || v == 8 || v == 9) {
            if (counter < order.size()) order[counter++] = v;
        }
        ++c.address;
    } while (!quit);

    while (counter > 0) {
        switch (order[--counter]) {
        case 1:
            c.tonSlideDelay = module_.at(c.address);
            c.tonSlideCount = c.tonSlideDelay;
            c.tonSlideStep = static_cast<std::int16_t>(module_.word(c.address + 1));
            c.address += 3;
            c.simpleGliss = true;
            c.currentOnOff = 0;
            if (c.tonSlideCount == 0 && module_.version() >= 7) c.tonSlideCount = 1;
            break;
        case 2: {
            c.simpleGliss = false;
            c.currentOnOff = 0;
            c.tonSlideDelay = module_.at(c.address);
            c.tonSlideCount = c.tonSlideDelay;
            c.address += 3;
            const int raw = static_cast<std::int16_t>(module_.word(c.address));
            c.tonSlideStep = raw < 0 ? -raw : raw;
            c.address += 2;
            c.tonDelta = static_cast<int>(module_.notePeriod(c.note)) - static_cast<int>(module_.notePeriod(previousNote));
            c.slideToNote = c.note;
            c.note = previousNote;
            if (module_.version() >= 6) c.tonSliding = previousSliding;
            if (c.tonDelta - c.tonSliding < 0) c.tonSlideStep = -c.tonSlideStep;
            break;
        }
        case 3:
            c.samplePos = module_.at(c.address++);
            break;
        case 4:
            c.ornamentPos = module_.at(c.address++);
            break;
        case 5:
            c.onOffDelay = module_.at(c.address++);
            c.offOnDelay = module_.at(c.address++);
            c.currentOnOff = c.onOffDelay;
            c.tonSlideCount = 0;
            c.tonSliding = 0;
            break;
        case 8:
            envDelay_ = module_.at(c.address);
            curEnvDelay_ = envDelay_;
            envSlideAdd_ = static_cast<std::int16_t>(module_.word(c.address + 1));
            c.address += 3;
            break;
        case 9:
            delay_ = module_.at(c.address++);
            break;
        default: break;
        }
    }
    c.skipCounter = c.notesToSkip;
}

void Pt3Engine::changeRegisters(Channel& c, std::uint8_t& addToEnv, std::uint8_t& mixer) noexcept {
    if (c.enabled) {
        const std::size_t entry = c.samplePtr + static_cast<std::size_t>(c.samplePos) * 4;
        const unsigned b0 = module_.at(entry);
        const unsigned b1 = module_.at(entry + 1);
        int tone = module_.word(entry + 2) + c.tonAccumulator;
        if (b1 & 0x40) c.tonAccumulator = tone;
        int j = static_cast<int>(c.note) + static_cast<std::int8_t>(module_.at(c.ornamentPtr + c.ornamentPos));
        if (j < 0) j = 0;
        if (j > 95) j = 95;
        tone = (tone + c.tonSliding + module_.notePeriod(static_cast<unsigned>(j))) & 0xfff;
        c.tone = tone;
        if (c.tonSlideCount > 0 && --c.tonSlideCount == 0) {
            c.tonSliding += c.tonSlideStep;
            c.tonSlideCount = c.tonSlideDelay;
            if (!c.simpleGliss &&
                ((c.tonSlideStep < 0 && c.tonSliding <= c.tonDelta) ||
                 (c.tonSlideStep >= 0 && c.tonSliding >= c.tonDelta))) {
                c.note = c.slideToNote;
                c.tonSlideCount = 0;
                c.tonSliding = 0;
            }
        }
        int amp = static_cast<int>(b1 & 15);
        if (b0 & 0x80) {
            if (b0 & 0x40) {
                if (c.amplitudeSliding < 15) ++c.amplitudeSliding;
            } else if (c.amplitudeSliding > -15) {
                --c.amplitudeSliding;
            }
        }
        amp += c.amplitudeSliding;
        if (amp < 0) amp = 0;
        if (amp > 15) amp = 15;
        const auto* table = module_.version() <= 4 ? pt3_tables::VolumeOld : pt3_tables::VolumeNew;
        amp = table[(c.volume & 15) * 16 + amp];
        if (!(b0 & 1) && c.envelopeEnabled) amp |= 16;
        c.amplitude = amp;
        if (b1 & 0x80) {
            std::uint8_t e = static_cast<std::uint8_t>(((b0 & 0x20) ? ((b0 >> 1) | 0xf0) : ((b0 >> 1) & 0x0f)) + c.envelopeSliding);
            if (b1 & 0x20) c.envelopeSliding = e;
            addToEnv = static_cast<std::uint8_t>(addToEnv + e);
        } else {
            addToNoise_ = static_cast<std::uint8_t>((b0 >> 1) + c.noiseSliding);
            if (b1 & 0x20) c.noiseSliding = static_cast<std::uint8_t>(addToNoise_);
        }
        mixer = static_cast<std::uint8_t>(((b1 >> 1) & 0x48) | mixer);
        if (++c.samplePos >= c.sampleLength) c.samplePos = c.sampleLoop;
        if (++c.ornamentPos >= c.ornamentLength) c.ornamentPos = c.ornamentLoop;
    } else {
        c.amplitude = 0;
    }
    mixer >>= 1;
    if (c.currentOnOff > 0 && --c.currentOnOff == 0) {
        c.enabled = !c.enabled;
        c.currentOnOff = c.enabled ? c.onOffDelay : c.offOnDelay;
    }
}

std::array<std::uint8_t, 14> Pt3Engine::tick(bool& shapeWritten) noexcept {
    pendingShape_ = -1;
    if (--delayCounter_ == 0) {
        if (--ch_[0].skipCounter == 0) {
            if (module_.at(ch_[0].address) == 0) {
                if (++position_ >= module_.positionCount()) {
                    position_ = module_.loopPosition();
                    ++loops_;
                }
                loadPosition();
                noiseBase_ = 0;
            }
            interpret(ch_[0]);
        }
        if (--ch_[1].skipCounter == 0) interpret(ch_[1]);
        if (--ch_[2].skipCounter == 0) interpret(ch_[2]);
        delayCounter_ = delay_ ? delay_ : 256;
    }
    std::uint8_t addToEnv = 0, mixer = 0;
    for (auto& c : ch_) changeRegisters(c, addToEnv, mixer);
    std::array<std::uint8_t, 14> r{};
    for (unsigned i = 0; i < 3; ++i) {
        r[i * 2] = static_cast<std::uint8_t>(ch_[i].tone & 0xff);
        r[i * 2 + 1] = static_cast<std::uint8_t>((ch_[i].tone >> 8) & 0x0f);
        r[8 + i] = static_cast<std::uint8_t>(ch_[i].amplitude);
    }
    r[6] = static_cast<std::uint8_t>((noiseBase_ + addToNoise_) & 31);
    r[7] = mixer;
    const std::uint16_t env = static_cast<std::uint16_t>(envBase_ + static_cast<std::int8_t>(addToEnv) + envSlide_);
    r[11] = static_cast<std::uint8_t>(env & 0xff);
    r[12] = static_cast<std::uint8_t>(env >> 8);
    shapeWritten = pendingShape_ >= 0;
    r[13] = shapeWritten ? static_cast<std::uint8_t>(pendingShape_) : 0;
    if (curEnvDelay_ > 0 && --curEnvDelay_ == 0) {
        curEnvDelay_ = envDelay_;
        envSlide_ += envSlideAdd_;
    }
    return r;
}

Pt3Player::Pt3Player(Pt3Module module, std::uint32_t sampleRate)
    : module_(std::move(module)), engine_(module_), sampleRate_(sampleRate ? sampleRate : DefaultSampleRate) {
    ay_.reset(ChipClockHz, sampleRate_);
}

void Pt3Player::render(std::int16_t* samples, std::size_t count) noexcept {
    if (!samples) return;
    for (std::size_t i = 0; i < count; ++i) {
        if (!playing_) {
            samples[i] = 0;
            continue;
        }
        if (!primed_ || accumulator_ >= sampleRate_) {
            if (primed_) accumulator_ -= sampleRate_;
            primed_ = true;
            bool shape = false;
            auto regs = engine_.tick(shape);
            if (engine_.failed()) {
                playing_ = false;
                samples[i] = 0;
                continue;
            }
            if (!shape) regs[13] = lastShape_;
            else lastShape_ = regs[13];
            ay_.writeRegisters(regs, shape);
        }
        samples[i] = ay_.renderSample();
        accumulator_ += PlayerHz;
    }
}

} // namespace ql32::music
