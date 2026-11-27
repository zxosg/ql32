#pragma once

// Compact, byte-oriented encoding of a compiled program (compact-bytecode plan,
// Phases 1-2). InstructionSequence::makeCompact() replaces the instruction
// deque with this form; the VM reads it through instructionAt(), which decodes
// into a small ring of Instruction slots.
#include "bytecode.hpp"

#include <array>
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace ql32::detail {

// Fixed 1 KB segments: no allocation larger than one segment, segments never move.
class SegmentedBytes {
public:
    static constexpr std::size_t SegmentSize = 1024;
    SegmentedBytes() = default;
    SegmentedBytes(const SegmentedBytes& other);
    SegmentedBytes& operator=(const SegmentedBytes& other);
    SegmentedBytes(SegmentedBytes&&) noexcept = default;
    SegmentedBytes& operator=(SegmentedBytes&&) noexcept = default;

    void push(std::uint8_t value);
    std::uint8_t at(std::size_t offset) const { return (*segments_[offset / SegmentSize])[offset % SegmentSize]; }
    std::size_t size() const noexcept { return size_; }
    std::size_t segmentCount() const noexcept { return segments_.size(); }
    void truncate(std::size_t size);

private:
    using Segment = std::array<std::uint8_t, SegmentSize>;
    std::vector<std::unique_ptr<Segment>> segments_;
    std::size_t size_ = 0;
};

struct CompactStats {
    std::size_t instructions = 0;
    std::size_t codeBytes = 0;
    std::size_t positionBytes = 0;
    std::size_t indexBytes = 0;
    std::size_t nameTableEntries = 0;
    std::size_t extrasRecords = 0;
    std::size_t segments = 0;
};

class CompactProgram {
public:
    // One jump-index entry per this many instructions.
    static constexpr std::size_t IndexStride = 8;

    // Read position: byte offsets plus the running delta-coded source position.
    struct Cursor {
        std::size_t code = 0;
        std::size_t position = 0;
        long long line = 0, column = 0, basicLine = 0;
    };
    // Lets sequential decoding continue where the previous decode stopped.
    struct ReadHint {
        static constexpr std::size_t None = static_cast<std::size_t>(-1);
        std::size_t next = None;
        Cursor cursor;
    };

    static CompactProgram encode(const InstructionSequence& code);

    std::size_t size() const noexcept { return count_; }
    // Appends one instruction (runtime EVAL).
    void append(const Instruction& instruction);
    // Drops every instruction from `count` on, including their extras records.
    void truncate(std::size_t count);
    // Throws std::out_of_range past the end. Extras are borrowed from this program.
    Instruction decode(std::size_t index) const;
    void decodeInto(std::size_t index, Instruction& out, ReadHint& hint) const;
    CompactStats stats() const;

private:
    struct Checkpoint {
        std::uint32_t code = 0;
        std::uint32_t position = 0;
        std::uint32_t extras = 0;
    };
    void appendRecord(const Instruction& instruction, std::unordered_map<const void*, std::size_t>* nameIds);
    void readRecord(Cursor& cursor, Instruction* out) const;
    Cursor seek(std::size_t index) const;

    SegmentedBytes code_;
    SegmentedBytes positions_;
    std::vector<Checkpoint> index_;
    std::vector<SharedText> names_;
    std::deque<InstructionExtras> extras_;  // stable addresses: decoded instructions borrow them
    std::shared_ptr<TextPool> pool_;
    std::size_t count_ = 0;
    Cursor writeCursor_;
};

class CompactState {
public:
    CompactState() = default;
    CompactState(const CompactState& other) : program(other.program) {}
    CompactState& operator=(const CompactState& other) {
        program = other.program;
        invalidate();
        return *this;
    }

    const Instruction& at(std::size_t index);
    void invalidate();

    CompactProgram program;

private:
    static constexpr std::size_t Slots = 8;
    struct Slot {
        std::size_t index = CompactProgram::ReadHint::None;
        Instruction instruction;
    };
    std::array<Slot, Slots> ring_;
    std::size_t nextSlot_ = 0;
    CompactProgram::ReadHint hint_;
};

// Field-by-field comparison; on mismatch `why` names the first differing field.
bool instructionsEqual(const Instruction& a, const Instruction& b, std::string* why = nullptr);

// Encodes `code` and checks every instruction round-trips. Returns false and
// fills `why` (with the instruction index) on the first difference.
bool verifyCompactRoundTrip(const InstructionSequence& code, std::string* why = nullptr);

} // namespace ql32::detail
