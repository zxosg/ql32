#include "compact_program.hpp"

#include <cstring>
#include <optional>
#include <sstream>

namespace ql32::detail {
namespace {

// Presence bits of the record header byte (byte 1 of each record).
constexpr std::uint8_t HasFlags = 1u << 0;
constexpr std::uint8_t HasName = 1u << 1;
constexpr std::uint8_t HasTarget = 1u << 2;
constexpr std::uint8_t HasExitTarget = 1u << 3;
constexpr std::uint8_t HasOperand = 1u << 4;
constexpr std::uint8_t HasExtras = 1u << 5;
constexpr std::uint8_t HasRankSeparator = 1u << 6;
constexpr std::uint8_t HasLiteral = 1u << 7;

void putVarint(SegmentedBytes& out, std::uint64_t value) {
    while (value >= 0x80) {
        out.push(static_cast<std::uint8_t>(value | 0x80));
        value >>= 7;
    }
    out.push(static_cast<std::uint8_t>(value));
}

std::uint64_t zigzag(long long value) {
    return (static_cast<std::uint64_t>(value) << 1) ^ static_cast<std::uint64_t>(value >> 63);
}
long long unzigzag(std::uint64_t value) {
    return static_cast<long long>(value >> 1) ^ -static_cast<long long>(value & 1);
}

class Reader {
public:
    Reader(const SegmentedBytes& bytes, std::size_t offset) : bytes_(bytes), offset_(offset) {}
    std::uint8_t byte() {
        if (offset_ >= bytes_.size()) throw std::out_of_range("compact program truncated");
        return bytes_.at(offset_++);
    }
    std::uint64_t varint() {
        std::uint64_t value = 0;
        for (int shift = 0; shift < 64; shift += 7) {
            const std::uint8_t b = byte();
            value |= static_cast<std::uint64_t>(b & 0x7F) << shift;
            if (!(b & 0x80)) return value;
        }
        throw std::out_of_range("compact program varint too long");
    }
    std::size_t offset() const { return offset_; }

private:
    const SegmentedBytes& bytes_;
    std::size_t offset_;
};

std::uint16_t packFlags(const InstructionFlags& f) {
    std::uint16_t bits = 0;
    int bit = 0;
    for (const bool value : {f.explicitNext, f.clearTarget, f.expectsValue, f.builtin,
                             f.channelArgument, f.sliceHasStart, f.sliceHasEnd, f.sliceSingle,
                             f.sourceJump, f.targetIsLineNumber, f.statementStart, f.textLiteral}) {
        if (value) bits |= static_cast<std::uint16_t>(1u << bit);
        ++bit;
    }
    return bits;
}

void unpackFlags(std::uint16_t bits, InstructionFlags& f) {
    auto at = [&](int bit) { return ((bits >> bit) & 1u) != 0; };
    f.explicitNext = at(0);
    f.clearTarget = at(1);
    f.expectsValue = at(2);
    f.builtin = at(3);
    f.channelArgument = at(4);
    f.sliceHasStart = at(5);
    f.sliceHasEnd = at(6);
    f.sliceSingle = at(7);
    f.sourceJump = at(8);
    f.targetIsLineNumber = at(9);
    f.statementStart = at(10);
    f.textLiteral = at(11);
}

bool hasExtras(const Instruction& instruction) {
    static const InstructionExtras* none = &Instruction{}.extra();
    return &instruction.extra() != none;
}

bool sameExtras(const InstructionExtras& a, const InstructionExtras& b, std::string* why) {
    auto fail = [&](const char* field) {
        if (why) *why = std::string("extras.") + field;
        return false;
    };
    auto same = [](const auto& x, const auto& y, auto equal) {
        if (x.size() != y.size()) return false;
        auto yi = y.begin();
        for (auto xi = x.begin(); xi != x.end(); ++xi, ++yi)
            if (!equal(*xi, *yi)) return false;
        return true;
    };
    const auto eqString = [](const std::string& x, const std::string& y) { return x == y; };
    if (!same(a.forItems, b.forItems, [](const ForItemSpec& x, const ForItemSpec& y) {
            return x.hasEnd == y.hasEnd && x.hasStep == y.hasStep; })) return fail("forItems");
    if (!same(a.parameters, b.parameters, eqString)) return fail("parameters");
    if (!same(a.inputVariables, b.inputVariables, eqString)) return fail("inputVariables");
    if (!same(a.localNames, b.localNames, eqString)) return fail("localNames");
    if (!same(a.arrayArgumentCandidates, b.arrayArgumentCandidates,
              [](const ArrayArgumentCandidate& x, const ArrayArgumentCandidate& y) {
                  return x.parameterIndex == y.parameterIndex && x.name == y.name &&
                         x.loadInstruction == y.loadInstruction; })) return fail("arrayArgumentCandidates");
    if (a.ownerDefinition != b.ownerDefinition) return fail("ownerDefinition");
    if (a.targetVariable != b.targetVariable) return fail("targetVariable");
    if (!same(a.selectItems, b.selectItems, [](const SelectItemSpec& x, const SelectItemSpec& y) {
            return x.hasEnd == y.hasEnd; })) return fail("selectItems");
    if (!same(a.jumpScopes, b.jumpScopes, [](std::size_t x, std::size_t y) { return x == y; }))
        return fail("jumpScopes");
    return true;
}

} // namespace

void SegmentedBytes::push(std::uint8_t value) {
    if (size_ % SegmentSize == 0) segments_.push_back(std::make_unique<Segment>());
    (*segments_.back())[size_ % SegmentSize] = value;
    ++size_;
}

void CompactProgram::appendRecord(const Instruction& in,
                                  std::unordered_map<const void*, std::size_t>& nameIds) {
    if (count_ % IndexStride == 0) {
        codeIndex_.push_back(static_cast<std::uint32_t>(code_.size()));
        positionIndex_.push_back(static_cast<std::uint32_t>(positions_.size()));
        writeCursor_.line = writeCursor_.column = writeCursor_.basicLine = 0;
    }
    const bool numericPush = in.op == Op::Push && !in.textLiteral;
    const std::uint16_t flags = packFlags(in);
    std::uint8_t mask = 0;
    if (flags) mask |= HasFlags;
    if (!in.name.empty()) mask |= HasName;
    if (!numericPush && in.target) mask |= HasTarget;
    if (!numericPush && in.exitTarget) mask |= HasExitTarget;
    if (in.operand_) mask |= HasOperand;
    if (hasExtras(in)) mask |= HasExtras;
    if (static_cast<int>(in.rank) || in.separator != PrintSeparator::None) mask |= HasRankSeparator;
    if (numericPush) mask |= HasLiteral;

    code_.push(static_cast<std::uint8_t>(in.op));
    code_.push(mask);
    if (mask & HasFlags) putVarint(code_, flags);
    if (mask & HasName) {
        auto found = nameIds.find(in.name.storageId());
        if (found == nameIds.end()) {
            found = nameIds.emplace(in.name.storageId(), names_.size()).first;
            names_.push_back(in.name);
        }
        putVarint(code_, found->second);
    }
    if (mask & HasTarget) putVarint(code_, in.target);
    if (mask & HasExitTarget) putVarint(code_, in.exitTarget);
    if (mask & HasOperand) putVarint(code_, in.operand_);
    if (mask & HasExtras) {
        putVarint(code_, extras_.size());
        extras_.push_back(in.extra());
    }
    if (mask & HasRankSeparator) {
        code_.push(static_cast<std::uint8_t>(static_cast<int>(in.rank)));
        code_.push(static_cast<std::uint8_t>(in.separator));
    }
    if (mask & HasLiteral) {
        const double value = in.literal();
        std::uint64_t bits;
        std::memcpy(&bits, &value, sizeof bits);
        const long long whole = static_cast<long long>(value);
        double back = 0;
        const bool small = value > -2147483648.0 && value < 2147483648.0;
        if (small) back = static_cast<double>(whole);
        std::uint64_t backBits;
        std::memcpy(&backBits, &back, sizeof backBits);
        if (small && backBits == bits) {
            code_.push(0);
            putVarint(code_, zigzag(whole));
        } else {
            code_.push(1);
            for (int i = 0; i < 8; ++i) code_.push(static_cast<std::uint8_t>(bits >> (8 * i)));
        }
    }

    putVarint(positions_, zigzag(static_cast<int>(in.line) - writeCursor_.line));
    putVarint(positions_, zigzag(static_cast<int>(in.column) - writeCursor_.column));
    putVarint(positions_, zigzag(static_cast<int>(in.basicLine) - writeCursor_.basicLine));
    writeCursor_.line = in.line;
    writeCursor_.column = in.column;
    writeCursor_.basicLine = in.basicLine;
    ++count_;
}

CompactProgram CompactProgram::encode(const InstructionSequence& source) {
    CompactProgram program;
    program.pool_ = source.textPool;
    std::unordered_map<const void*, std::size_t> nameIds;
    for (const Instruction& instruction : source) program.appendRecord(instruction, nameIds);
    return program;
}

void CompactProgram::readRecord(Cursor& cursor, Instruction* out) const {
    Reader code(code_, cursor.code);
    const Op op = static_cast<Op>(code.byte());
    const std::uint8_t mask = code.byte();
    Instruction scratch;
    Instruction& in = out ? *out : scratch;
    in.op = op;
    if (mask & HasFlags) unpackFlags(static_cast<std::uint16_t>(code.varint()), in);
    if (mask & HasName) {
        const std::size_t id = code.varint();
        if (id >= names_.size()) throw std::out_of_range("compact name index");
        if (out) in.name = names_[id];
    }
    if (mask & HasTarget) in.target = static_cast<std::uint32_t>(code.varint());
    if (mask & HasExitTarget) in.exitTarget = static_cast<std::uint32_t>(code.varint());
    if (mask & HasOperand) in.operand_ = static_cast<std::uint32_t>(code.varint());
    if (mask & HasExtras) {
        const std::size_t id = code.varint();
        if (id >= extras_.size()) throw std::out_of_range("compact extras index");
        if (out) in.mutableExtra() = extras_[id];
    }
    if (mask & HasRankSeparator) {
        in.rank = code.byte();
        in.separator = static_cast<PrintSeparator>(code.byte());
    }
    if (mask & HasLiteral) {
        if (code.byte() == 0) {
            in.setLiteral(static_cast<double>(unzigzag(code.varint())));
        } else {
            std::uint64_t bits = 0;
            for (int i = 0; i < 8; ++i) bits |= static_cast<std::uint64_t>(code.byte()) << (8 * i);
            double value;
            std::memcpy(&value, &bits, sizeof value);
            in.setLiteral(value);
        }
    }
    cursor.code = code.offset();

    Reader positions(positions_, cursor.position);
    cursor.line += unzigzag(positions.varint());
    cursor.column += unzigzag(positions.varint());
    cursor.basicLine += unzigzag(positions.varint());
    cursor.position = positions.offset();
    if (out) {
        in.line = cursor.line;
        in.column = cursor.column;
        in.basicLine = cursor.basicLine;
    }
}

Instruction CompactProgram::decode(std::size_t index) const {
    if (index >= count_) throw std::out_of_range("compact instruction index");
    const std::size_t slot = index / IndexStride;
    Cursor cursor;
    cursor.code = codeIndex_[slot];
    cursor.position = positionIndex_[slot];
    for (std::size_t skip = slot * IndexStride; skip < index; ++skip) readRecord(cursor, nullptr);
    Instruction result;
    readRecord(cursor, &result);
    return result;
}

CompactStats CompactProgram::stats() const {
    CompactStats s;
    s.instructions = count_;
    s.codeBytes = code_.size();
    s.positionBytes = positions_.size();
    s.indexBytes = (codeIndex_.size() + positionIndex_.size()) * sizeof(std::uint32_t);
    s.nameTableEntries = names_.size();
    s.extrasRecords = extras_.size();
    s.segments = code_.segmentCount() + positions_.segmentCount();
    return s;
}

bool instructionsEqual(const Instruction& a, const Instruction& b, std::string* why) {
    auto fail = [&](const char* field) {
        if (why) *why = field;
        return false;
    };
    if (a.op != b.op) return fail("op");
    if (packFlags(a) != packFlags(b)) return fail("flags");
    if (a.name.str() != b.name.str()) return fail("name");
    if (static_cast<int>(a.line) != static_cast<int>(b.line)) return fail("line");
    if (static_cast<int>(a.column) != static_cast<int>(b.column)) return fail("column");
    if (static_cast<int>(a.basicLine) != static_cast<int>(b.basicLine)) return fail("basicLine");
    if (a.target != b.target) return fail("target");
    if (a.exitTarget != b.exitTarget) return fail("exitTarget");
    if (a.operand_ != b.operand_) return fail("operand");
    if (static_cast<int>(a.rank) != static_cast<int>(b.rank)) return fail("rank");
    if (a.separator != b.separator) return fail("separator");
    return sameExtras(a.extra(), b.extra(), why);
}

bool verifyCompactRoundTrip(const InstructionSequence& code, std::string* why) {
    const CompactProgram program = CompactProgram::encode(code);
    if (program.size() != code.size()) {
        if (why) *why = "instruction count";
        return false;
    }
    for (std::size_t i = 0; i < code.size(); ++i) {
        std::string field;
        if (!instructionsEqual(code[i], program.decode(i), &field)) {
            if (why) *why = "instruction " + std::to_string(i) + ": " + field;
            return false;
        }
    }
    return true;
}

void compactDualRunCheck(const InstructionSequence& code, std::size_t pc) {
    struct Cache {
        const InstructionSequence* code = nullptr;
        std::optional<CompactProgram> program;
    };
    thread_local Cache cache;
    // EVAL appends and erases instructions in place, so a stale encoding is
    // refreshed once before a difference is reported.
    if (cache.code != &code || !cache.program || cache.program->size() != code.size()) {
        cache.code = &code;
        cache.program = CompactProgram::encode(code);
    }
    std::string why;
    if (instructionsEqual(code[pc], cache.program->decode(pc), &why)) return;
    cache.program = CompactProgram::encode(code);
    if (instructionsEqual(code[pc], cache.program->decode(pc), &why)) return;
    throw std::logic_error("compact dual-run mismatch at instruction " + std::to_string(pc) + ": " + why);
}

} // namespace ql32::detail
