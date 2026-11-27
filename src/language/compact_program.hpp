#pragma once

// Compact, byte-oriented encoding of a compiled program (compact-bytecode plan,
// Phase 1). The VM still executes the Instruction form; this encoder/decoder
// exists so the new form can be proven equal instruction by instruction
// (dual-run) before any phase makes it authoritative.
#include "bytecode.hpp"

#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace ql32::detail {

// Fixed 1 KB segments: no allocation larger than one segment, segments never move.
class SegmentedBytes {
public:
    static constexpr std::size_t SegmentSize = 1024;
    void push(std::uint8_t value);
    std::uint8_t at(std::size_t offset) const { return (*segments_[offset / SegmentSize])[offset % SegmentSize]; }
    std::size_t size() const noexcept { return size_; }
    std::size_t segmentCount() const noexcept { return segments_.size(); }

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

    static CompactProgram encode(const InstructionSequence& code);

    std::size_t size() const noexcept { return count_; }
    // Throws std::out_of_range past the end.
    Instruction decode(std::size_t index) const;
    CompactStats stats() const;

private:
    struct Cursor {
        std::size_t code = 0;
        std::size_t position = 0;
        long long line = 0, column = 0, basicLine = 0;
    };
    void appendRecord(const Instruction& instruction, std::unordered_map<const void*, std::size_t>& nameIds);
    void readRecord(Cursor& cursor, Instruction* out) const;

    SegmentedBytes code_;
    SegmentedBytes positions_;
    std::vector<std::uint32_t> codeIndex_;
    std::vector<std::uint32_t> positionIndex_;
    std::vector<SharedText> names_;
    std::vector<InstructionExtras> extras_;
    std::shared_ptr<TextPool> pool_;
    std::size_t count_ = 0;
    Cursor writeCursor_;
};

// Field-by-field comparison; on mismatch `why` names the first differing field.
bool instructionsEqual(const Instruction& a, const Instruction& b, std::string* why = nullptr);

// Encodes `code` and checks every instruction round-trips. Returns false and
// fills `why` (with the instruction index) on the first difference.
bool verifyCompactRoundTrip(const InstructionSequence& code, std::string* why = nullptr);

// Dual-run hook: called by the VM before it executes `code[pc]` when built with
// QL32_COMPACT_DUALRUN. Throws std::logic_error if the compact form differs.
void compactDualRunCheck(const InstructionSequence& code, std::size_t pc);

} // namespace ql32::detail
