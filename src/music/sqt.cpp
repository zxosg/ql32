#include "ql32/music/sqt.hpp"

#include <stdexcept>

// SQ-Tracker module player written from the documented SQT data layout and
// the behaviour of S.V. Bulba's AY emulator player. Only the note-period
// table is shared numeric data.
namespace ql32::music {
namespace {
const std::uint16_t NoteTable[96] = {
    0x0D5D, 0x0C9C, 0x0BE7, 0x0B3C, 0x0A9B, 0x0A02, 0x0973, 0x08EB, 0x086B, 0x07F2, 0x0780, 0x0714,
    0x06AE, 0x064E, 0x05F4, 0x059E, 0x054F, 0x0501, 0x04B9, 0x0475, 0x0435, 0x03F9, 0x03C0, 0x038A,
    0x0357, 0x0327, 0x02FA, 0x02CF, 0x02A7, 0x0281, 0x025D, 0x023B, 0x021B, 0x01FC, 0x01E0, 0x01C5,
    0x01AC, 0x0194, 0x017D, 0x0168, 0x0153, 0x0140, 0x012E, 0x011D, 0x010D, 0x00FE, 0x00F0, 0x00E2,
    0x00D6, 0x00CA, 0x00BE, 0x00B4, 0x00AA, 0x00A0, 0x0097, 0x008F, 0x0087, 0x007F, 0x0078, 0x0071,
    0x006B, 0x0065, 0x005F, 0x005A, 0x0055, 0x0050, 0x004C, 0x0047, 0x0043, 0x0040, 0x003C, 0x0039,
    0x0035, 0x0032, 0x0030, 0x002D, 0x002A, 0x0028, 0x0026, 0x0024, 0x0022, 0x0020, 0x001E, 0x001C,
    0x001B, 0x0019, 0x0018, 0x0016, 0x0015, 0x0014, 0x0013, 0x0012, 0x0011, 0x0010, 0x000F, 0x000E,
};

std::uint16_t le16(const std::vector<std::uint8_t>& b, std::size_t o) {
    return o + 1 < b.size() ? static_cast<std::uint16_t>(b[o] | (b[o + 1] << 8)) : 0;
}
} // namespace

bool SqtModule::looksLikeSqt(const std::uint8_t* d, std::size_t length) noexcept {
    if (!d || length < 17 || length > MaximumBytes) return false;
    const auto w = [d](std::size_t o) { return static_cast<int>(d[o] | (d[o + 1] << 8)); };
    const int samples = w(2), ornaments = w(4), patterns = w(6), positions = w(8), loop = w(10);
    if (samples < 10 || ornaments <= samples || patterns < ornaments || positions <= patterns || loop < positions)
        return false;
    const int base = samples - 10;
    if (loop - base >= static_cast<int>(length)) return false;
    std::size_t p = static_cast<std::size_t>(positions - base);
    if (d[p] == 0) return false;
    int maxPattern = 0;
    while (d[p] != 0) {
        if (p + 7 >= length) return false;
        for (int i = 0; i < 3; ++i) {
            if (maxPattern < (d[p] & 0x7f)) maxPattern = d[p] & 0x7f;
            p += i < 2 ? 2 : 3;
        }
    }
    if (w(12) - patterns - 2 != maxPattern * 2) return false;
    const std::size_t samplesCount = static_cast<std::size_t>(ornaments - samples) / 2;
    const std::size_t ornamentsCount = static_cast<std::size_t>(patterns - ornaments) / 2;
    if (12 + 2 * (samplesCount + ornamentsCount) + 2 > length) return false;
    std::size_t o = 12;
    int previous = w(o);
    for (std::size_t i = 0; i < samplesCount; ++i) {
        o += 2;
        const int next = w(o);
        if (next - previous != 0x62) return false;
        previous = next;
    }
    for (std::size_t i = 0; i < ornamentsCount; ++i) {
        o += 2;
        const int next = w(o);
        if (next - previous != 0x22) return false;
        previous = next;
    }
    return true;
}

SqtModule SqtModule::parse(std::vector<std::uint8_t> bytes) {
    if (!looksLikeSqt(bytes.data(), bytes.size())) throw std::runtime_error("not an SQT module");
    SqtModule m;
    m.bytes_ = std::move(bytes);
    const std::size_t base = le16(m.bytes_, 2) - 10u;
    std::size_t p = le16(m.bytes_, 8) - base;
    unsigned maxPattern = 0;
    while (m.bytes_[p] != 0) {
        for (int i = 0; i < 3; ++i) {
            maxPattern = std::max<unsigned>(maxPattern, m.bytes_[p] & 0x7f);
            p += i < 2 ? 2 : 3;
        }
    }
    const std::size_t words = (le16(m.bytes_, 6) - base + maxPattern * 2) / 2;
    for (std::size_t i = 0; i < words; ++i) {
        const std::size_t o = 2 + i * 2;
        if (o + 1 >= m.bytes_.size()) break;
        const unsigned v = le16(m.bytes_, o);
        if (v < base) throw std::runtime_error("SQT pointer table is invalid");
        const unsigned r = v - static_cast<unsigned>(base);
        m.bytes_[o] = static_cast<std::uint8_t>(r & 255);
        m.bytes_[o + 1] = static_cast<std::uint8_t>(r >> 8);
    }
    return m;
}

SqtEngine::SqtEngine(const SqtModule& module) noexcept : m_(module) {
    positions_ = m_.positionsPointer();
}

void SqtEngine::startSample(Channel& c, unsigned n) noexcept {
    c.envelope = false;
    c.ornament = false;
    c.gliss = false;
    c.enabled = true;
    c.samplePtr = m_.word(n * 2 + m_.samplesPointer());
    c.pointInSample = c.samplePtr + 2;
    c.sampleTik = 32;
    c.mixNoise = true;
    c.mixTon = true;
}

void SqtEngine::startOrnament(Channel& c, unsigned n) noexcept {
    c.ornamentPtr = m_.word(n * 2 + m_.ornamentsPointer());
    c.pointInOrnament = c.ornamentPtr + 2;
    c.ornamentTik = 32;
    c.ornament = true;
}

// Parameter/effect command; a == 0 reproduces the original quirk of selecting an envelope.
void SqtEngine::command(Channel& c, std::size_t& ptr, int a) noexcept {
    ++ptr;
    if (c.b6) {
        c.address = ptr + 1;
        c.b6 = false;
    }
    const std::uint8_t arg = m_.at(ptr);
    switch (a - 1) {
    case 0: if (c.b4) c.volume = arg & 15; break;
    case 1: if (c.b4) c.volume = (c.volume + arg) & 15; break;
    case 2: if (c.b4) for (auto& x : ch_) x.volume = arg; break;
    case 3: if (c.b4) for (auto& x : ch_) x.volume = (x.volume + arg) & 15; break;
    case 4:
        if (c.b4) {
            delayCounter_ = arg & 31;
            if (delayCounter_ == 0) delayCounter_ = 32;
            delay_ = delayCounter_;
        }
        break;
    case 5:
        if (c.b4) {
            delayCounter_ = (delayCounter_ + arg) & 31;
            if (delayCounter_ == 0) delayCounter_ = 32;
            delay_ = delayCounter_;
        }
        break;
    case 6: c.sliding = 0; c.gliss = true; c.slideStep = static_cast<std::int16_t>(-arg); break;
    case 7: c.sliding = 0; c.gliss = true; c.slideStep = arg; break;
    default:
        c.envelope = true;
        pendingShape_ = (a - 1) & 15;
        envFine_ = arg;
        break;
    }
}

void SqtEngine::afterNote(Channel& c, std::size_t& ptr) noexcept {
    const unsigned val = m_.at(ptr);
    if (val <= 0x7f) {
        command(c, ptr, static_cast<int>(val));
    } else {
        if (((val >> 1) & 31) != 0) startSample(c, (val >> 1) & 31);
        if (val & 64) {
            unsigned t = m_.at(ptr + 1) >> 4;
            if (val & 1) t |= 16;
            if (t != 0) startOrnament(c, t);
            ++ptr;
            if ((m_.at(ptr) & 15) != 0) command(c, ptr, m_.at(ptr) & 15);
        }
    }
    ++ptr;
}

void SqtEngine::repeatLast(Channel& c, std::size_t& ptr) noexcept {
    ptr = c.ix27;
    c.b6 = false;
    if (m_.at(ptr) <= 0x7f) {
        ++ptr;
        afterNote(c, ptr);
    } else {
        startSample(c, m_.at(ptr) & 31);
    }
}

void SqtEngine::interpret(Channel& c) noexcept {
    std::size_t ptr = 0;
    if (c.ix21 != 0) {
        --c.ix21;
        if (c.b7) repeatLast(c, ptr);
        return;
    }
    ptr = c.address;
    c.b6 = true;
    c.b7 = false;
    const unsigned val = m_.at(ptr);
    if (val <= 0x5f) {
        c.note = static_cast<std::uint8_t>(val);
        c.ix27 = ptr;
        ++ptr;
        afterNote(c, ptr);
        if (c.b6) c.address = ptr;
    } else if (val <= 0x6e) {
        command(c, ptr, static_cast<int>(val) - 0x60);
    } else if (val <= 0x7f) {
        c.mixNoise = false;
        c.mixTon = false;
        c.enabled = false;
        if (val != 0x6f) command(c, ptr, static_cast<int>(val) - 0x6f);
        else c.address = ptr + 1;
    } else if (val <= 0xbf) {
        c.address = ptr + 1;
        if (val <= 0x9f) {
            c.note = static_cast<std::uint8_t>((val & 16) == 0 ? c.note + (val & 15) : c.note - (val & 15));
        } else {
            c.ix21 = static_cast<std::uint8_t>(val & 15);
            if ((val & 16) == 0) return;
            if (c.ix21 != 0) c.b7 = true;
        }
        repeatLast(c, ptr);
    } else {
        c.address = ptr + 1;
        c.ix27 = ptr;
        startSample(c, val & 31);
    }
}

void SqtEngine::registersFor(Channel& c, std::uint8_t& mixer) noexcept {
    mixer = static_cast<std::uint8_t>(mixer << 1);
    if (!c.enabled) {
        c.amplitude = 0;
        return;
    }
    const unsigned b0 = m_.at(c.pointInSample);
    const unsigned b1 = m_.at(c.pointInSample + 1);
    c.amplitude = static_cast<std::uint8_t>(b0 & 15);
    if (c.amplitude != 0) {
        c.amplitude = static_cast<std::uint8_t>(c.amplitude - c.volume);
        if (static_cast<std::int8_t>(c.amplitude) < 0) c.amplitude = 0;
    } else if (c.envelope) {
        c.amplitude = 16;
    }
    if (b1 & 32) {
        mixer |= 8;
        noise_ = static_cast<std::uint8_t>((b0 & 0xf0) >> 3);
        if (b1 & 0x80) ++noise_;
    }
    if (b1 & 64) mixer |= 1;
    std::uint8_t j = c.note;
    if (c.ornament) {
        j = static_cast<std::uint8_t>(j + m_.at(c.pointInOrnament));
        if (--c.ornamentTik == 0) {
            const bool noLoop = m_.at(c.ornamentPtr) == 32;
            c.ornamentTik = static_cast<std::int8_t>(m_.at(noLoop ? c.samplePtr + 1 : c.ornamentPtr + 1));
            c.pointInOrnament = c.ornamentPtr + 2 + m_.at(noLoop ? c.samplePtr : c.ornamentPtr);
        } else {
            ++c.pointInOrnament;
        }
    }
    j = static_cast<std::uint8_t>(j + c.transpose);
    if (j > 0x5f) j = 0x5f;
    const unsigned detune = ((b1 & 15) << 8) + m_.at(c.pointInSample + 2);
    c.ton = static_cast<std::uint16_t>((b1 & 16) == 0 ? NoteTable[j] - detune : NoteTable[j] + detune);
    if (--c.sampleTik == 0) {
        c.sampleTik = static_cast<std::int8_t>(m_.at(c.samplePtr + 1));
        if (m_.at(c.samplePtr) == 32) {
            c.enabled = false;
            c.ornament = false;
        }
        c.pointInSample = c.samplePtr + 2 + static_cast<std::size_t>(m_.at(c.samplePtr)) * 3;
    } else {
        c.pointInSample += 3;
    }
    if (c.gliss) {
        c.ton = static_cast<std::uint16_t>(c.ton + c.sliding);
        c.sliding = static_cast<std::int16_t>(c.sliding + c.slideStep);
    }
    c.ton &= 0xfff;
}

void SqtEngine::loadPosition(Channel& c, bool first) noexcept {
    if (m_.at(positions_) == 0) {
        positions_ = m_.loopPointer();
        ++loops_;
    }
    const std::uint8_t entry = m_.at(positions_);
    c.b4 = (entry & 0x80) != 0;
    const std::size_t pattern = m_.word(static_cast<std::uint8_t>(entry * 2) + m_.patternsPointer());
    if (first) {
        lines_ = m_.at(pattern);
        c.address = pattern + 1;
    } else {
        c.address = pattern + 1;
    }
    ++positions_;
    const std::uint8_t vt = m_.at(positions_);
    c.volume = vt & 15;
    c.transpose = static_cast<std::int8_t>((vt >> 4) < 9 ? (vt >> 4) : -((vt >> 4) - 9) - 1);
    ++positions_;
    c.ix21 = 0;
}

std::array<std::uint8_t, 14> SqtEngine::tick(bool& shapeWritten) noexcept {
    pendingShape_ = -1;
    if (--delayCounter_ == 0) {
        delayCounter_ = delay_;
        if (--lines_ == 0) {
            loadPosition(ch_[2], true);
            loadPosition(ch_[1], false);
            loadPosition(ch_[0], false);
            delay_ = m_.at(positions_);
            delayCounter_ = delay_;
            ++positions_;
        }
        interpret(ch_[2]);
        interpret(ch_[1]);
        interpret(ch_[0]);
    }
    std::uint8_t mixer = 0;
    registersFor(ch_[2], mixer);
    registersFor(ch_[1], mixer);
    registersFor(ch_[0], mixer);
    mixer = static_cast<std::uint8_t>((-(mixer + 1)) & 0x3f);
    for (unsigned i = 0; i < 3; ++i) {
        if (!ch_[i].mixNoise) mixer |= static_cast<std::uint8_t>(8 << i);
        if (!ch_[i].mixTon) mixer |= static_cast<std::uint8_t>(1 << i);
    }
    std::array<std::uint8_t, 14> r{};
    for (unsigned i = 0; i < 3; ++i) {
        r[i * 2] = static_cast<std::uint8_t>(ch_[i].ton & 0xff);
        r[i * 2 + 1] = static_cast<std::uint8_t>((ch_[i].ton >> 8) & 0x0f);
        r[8 + i] = ch_[i].amplitude;
    }
    r[6] = noise_;
    r[7] = mixer;
    r[11] = envFine_;
    shapeWritten = pendingShape_ >= 0;
    r[13] = shapeWritten ? static_cast<std::uint8_t>(pendingShape_) : 0;
    return r;
}

SqtPlayer::SqtPlayer(SqtModule module, std::uint32_t sampleRate)
    : module_(std::move(module)), engine_(module_), sampleRate_(sampleRate ? sampleRate : DefaultSampleRate) {
    ay_.reset(ChipClockHz, sampleRate_);
}

void SqtPlayer::render(std::int16_t* samples, std::size_t count) noexcept {
    if (!samples) return;
    for (std::size_t i = 0; i < count; ++i) {
        if (!primed_ || accumulator_ >= sampleRate_) {
            if (primed_) accumulator_ -= sampleRate_;
            primed_ = true;
            bool shape = false;
            auto regs = engine_.tick(shape);
            if (shape) lastShape_ = regs[13];
            else regs[13] = lastShape_;
            ay_.writeRegisters(regs, shape);
        }
        samples[i] = ay_.renderSample();
        accumulator_ += PlayerHz;
    }
}

} // namespace ql32::music
