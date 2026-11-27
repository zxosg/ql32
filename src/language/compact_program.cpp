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

SegmentedBytes::SegmentedBytes(const SegmentedBytes& other) { *this = other; }

SegmentedBytes& SegmentedBytes::operator=(const SegmentedBytes& other) {
    if (this == &other) return *this;
    segments_.clear();
    for (const auto& segment : other.segments_) segments_.push_back(std::make_unique<Segment>(*segment));
    size_ = other.size_;
    return *this;
}

void SegmentedBytes::truncate(std::size_t size) {
    if (size >= size_) return;
    segments_.resize((size + SegmentSize - 1) / SegmentSize);
    size_ = size;
}

void CompactProgram::appendRecord(const Instruction& in,
                                  std::unordered_map<const void*, std::size_t>* nameIds) {
    if (count_ % IndexStride == 0) {
        index_.push_back({static_cast<std::uint32_t>(code_.size()),
                          static_cast<std::uint32_t>(positions_.size()),
                          static_cast<std::uint32_t>(extras_.size())});
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
        std::size_t id = names_.size();
        if (nameIds) {
            const auto found = nameIds->find(in.name.storageId());
            if (found != nameIds->end()) id = found->second;
            else nameIds->emplace(in.name.storageId(), id);
        } else {
            for (std::size_t i = 0; i < names_.size(); ++i)
                if (names_[i].storageId() == in.name.storageId()) { id = i; break; }
        }
        if (id == names_.size()) names_.push_back(in.name);
        putVarint(code_, id);
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
    for (const Instruction& instruction : source) program.appendRecord(instruction, &nameIds);
    return program;
}

void CompactProgram::readRecord(Cursor& cursor, Instruction* out) const {
    Reader code(code_, cursor.code);
    const Op op = static_cast<Op>(code.byte());
    const std::uint8_t mask = code.byte();
    Instruction* in = out;
    if (in) {
        in->op = op;
        unpackFlags((mask & HasFlags) ? static_cast<std::uint16_t>(code.varint()) : 0, *in);
    } else if (mask & HasFlags) {
        code.varint();
    }
    std::size_t nameId = 0, extrasId = 0;
    if (mask & HasName) {
        nameId = code.varint();
        if (nameId >= names_.size()) throw std::out_of_range("compact name index");
    }
    const std::uint32_t target = (mask & HasTarget) ? static_cast<std::uint32_t>(code.varint()) : 0;
    const std::uint32_t exitTarget = (mask & HasExitTarget) ? static_cast<std::uint32_t>(code.varint()) : 0;
    const std::uint32_t operand = (mask & HasOperand) ? static_cast<std::uint32_t>(code.varint()) : 0;
    if (mask & HasExtras) {
        extrasId = code.varint();
        if (extrasId >= extras_.size()) throw std::out_of_range("compact extras index");
    }
    std::uint8_t rank = 0;
    PrintSeparator separator = PrintSeparator::None;
    if (mask & HasRankSeparator) {
        rank = code.byte();
        separator = static_cast<PrintSeparator>(code.byte());
    }
    bool literal = false;
    double literalValue = 0;
    if (mask & HasLiteral) {
        literal = true;
        if (code.byte() == 0) {
            literalValue = static_cast<double>(unzigzag(code.varint()));
        } else {
            std::uint64_t bits = 0;
            for (int i = 0; i < 8; ++i) bits |= static_cast<std::uint64_t>(code.byte()) << (8 * i);
            std::memcpy(&literalValue, &bits, sizeof literalValue);
        }
    }
    cursor.code = code.offset();

    Reader positions(positions_, cursor.position);
    cursor.line += unzigzag(positions.varint());
    cursor.column += unzigzag(positions.varint());
    cursor.basicLine += unzigzag(positions.varint());
    cursor.position = positions.offset();
    if (!in) return;
    if (mask & HasName) in->name = names_[nameId];
    else in->name = SharedText{};
    in->target = target;
    in->exitTarget = exitTarget;
    in->operand_ = operand;
    in->rank = rank;
    in->separator = separator;
    if (mask & HasExtras) in->extras.borrow(&extras_[extrasId]);
    else in->extras.clear();
    if (literal) in->setLiteral(literalValue);
    in->line = cursor.line;
    in->column = cursor.column;
    in->basicLine = cursor.basicLine;
}

CompactProgram::Cursor CompactProgram::seek(std::size_t index) const {
    const Checkpoint& checkpoint = index_[index / IndexStride];
    Cursor cursor;
    cursor.code = checkpoint.code;
    cursor.position = checkpoint.position;
    for (std::size_t skip = index / IndexStride * IndexStride; skip < index; ++skip)
        readRecord(cursor, nullptr);
    return cursor;
}

void CompactProgram::decodeInto(std::size_t index, Instruction& out, ReadHint& hint) const {
    if (index >= count_) throw std::out_of_range("compact instruction index");
    if (hint.next != index) {
        // Checkpoints reset the delta state, so a seek never needs earlier records.
        hint.cursor = seek(index);
    }
    readRecord(hint.cursor, &out);
    hint.next = index + 1;
    // The cursor must be reset at group boundaries exactly as the encoder did.
    if (hint.next % IndexStride == 0 && hint.next < count_) {
        hint.cursor.line = hint.cursor.column = hint.cursor.basicLine = 0;
    }
}

Instruction CompactProgram::decode(std::size_t index) const {
    Instruction result;
    ReadHint hint;
    decodeInto(index, result, hint);
    return result;
}

void CompactProgram::append(const Instruction& instruction) {
    appendRecord(instruction, nullptr);
}

void CompactProgram::truncate(std::size_t count) {
    if (count >= count_) return;
    Cursor cursor = count == 0 ? Cursor{} : seek(count);
    // seek() leaves the delta state of the last kept record, or zero when `count`
    // starts a new group (the next record is then written against a fresh base).
    if (count == 0) {
        code_.truncate(0);
        positions_.truncate(0);
        index_.clear();
        extras_.clear();
        writeCursor_ = Cursor{};
        count_ = 0;
        return;
    }
    const std::size_t groups = (count + IndexStride - 1) / IndexStride;
    std::size_t extrasKept = index_[(count - 1) / IndexStride].extras;
    {
        Cursor walk;
        const Checkpoint& group = index_[(count - 1) / IndexStride];
        walk.code = group.code;
        walk.position = group.position;
        for (std::size_t i = (count - 1) / IndexStride * IndexStride; i < count; ++i) {
            if (code_.at(walk.code + 1) & HasExtras) ++extrasKept;
            readRecord(walk, nullptr);
        }
    }
    code_.truncate(cursor.code);
    positions_.truncate(cursor.position);
    index_.resize(groups);
    while (extras_.size() > extrasKept) extras_.pop_back();
    writeCursor_ = cursor;
    count_ = count;
}

CompactStats CompactProgram::stats() const {
    CompactStats s;
    s.instructions = count_;
    s.codeBytes = code_.size();
    s.positionBytes = positions_.size();
    s.indexBytes = index_.size() * sizeof(Checkpoint);
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

const Instruction& CompactState::at(std::size_t index) {
    for (const Slot& slot : ring_)
        if (slot.index == index) return slot.instruction;
    Slot& slot = ring_[nextSlot_];
    nextSlot_ = (nextSlot_ + 1) % Slots;
    slot.index = CompactProgram::ReadHint::None;
    program.decodeInto(index, slot.instruction, hint_);
    slot.index = index;
    return slot.instruction;
}

void CompactState::invalidate() {
    for (Slot& slot : ring_) slot.index = CompactProgram::ReadHint::None;
    hint_ = CompactProgram::ReadHint{};
}

CompactHolder::CompactHolder() = default;
CompactHolder::CompactHolder(const CompactHolder& other)
    : state_(other.state_ ? std::make_unique<CompactState>(*other.state_) : nullptr) {}
CompactHolder& CompactHolder::operator=(const CompactHolder& other) {
    if (this != &other) state_ = other.state_ ? std::make_unique<CompactState>(*other.state_) : nullptr;
    return *this;
}
CompactHolder::CompactHolder(CompactHolder&&) noexcept = default;
CompactHolder& CompactHolder::operator=(CompactHolder&&) noexcept = default;
CompactHolder::~CompactHolder() = default;
void CompactHolder::reset() noexcept { state_.reset(); }
CompactState& CompactHolder::make() {
    state_ = std::make_unique<CompactState>();
    return *state_;
}

} // namespace ql32::detail

namespace ql32::detail {

std::size_t InstructionSequence::instructionCount() const {
    return compact.get() ? compact.get()->program.size() : size();
}

const Instruction& InstructionSequence::instructionAt(std::size_t index) const {
    if (!compact.get()) return (*this)[index];
    const Instruction& decoded = compact.get()->at(index);
#ifdef QL32_COMPACT_DUALRUN
    std::string why;
    if (index >= size() || !instructionsEqual((*this)[index], decoded, &why))
        throw std::logic_error("compact dual-run mismatch at instruction " + std::to_string(index) + ": " + why);
#endif
    return decoded;
}

void InstructionSequence::truncateInstructions(std::size_t count) {
    if (compact.get()) {
        compact.get()->program.truncate(count);
        compact.get()->invalidate();
    }
    if (count < size()) erase(begin() + static_cast<std::ptrdiff_t>(count), end());
}

void InstructionSequence::appendInstruction(Instruction instruction) {
    if (compact.get()) {
        compact.get()->program.append(instruction);
        compact.get()->invalidate();
#ifndef QL32_COMPACT_DUALRUN
        return;
#endif
    }
    push_back(std::move(instruction));
}

void InstructionSequence::appendInstructions(const InstructionSequence& other) {
    for (const Instruction& instruction : other) appendInstruction(instruction);
}

void InstructionSequence::makeCompact() {
    compact.make().program = CompactProgram::encode(*this);
#ifndef QL32_COMPACT_DUALRUN
    clear();
    shrink_to_fit();
#endif
}

} // namespace ql32::detail
