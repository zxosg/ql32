// Compact bytecode encoder/decoder round-trip tests (compact-bytecode plan, Phase 1).
// Usage: ql32_compact_program_tests [source-root]  (walks examples/ and tests/ for *.bas)
#include "language/compact_program.hpp"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

using namespace ql32::detail;

namespace {

void require(bool ok, const std::string& message) {
    if (!ok) {
        std::cerr << "compact program test failed: " << message << '\n';
        std::exit(1);
    }
}

void requireRoundTrip(const InstructionSequence& code, const std::string& label) {
    std::string why;
    require(verifyCompactRoundTrip(code, &why), label + ": " + why);
}

void testLiteralsSurviveExactly() {
    auto code = compileSource("PRINT 1\n");
    for (const double value : {0.0, -0.0, 1.0, -1.0, 2147483647.0, 2147483648.0, -2147483648.0,
                               -2147483649.0, 1e300, -1e-300, 0.1, 1.0 / 3.0,
                               std::numeric_limits<double>::infinity(),
                               std::numeric_limits<double>::quiet_NaN()}) {
        Instruction push{Op::Push, value};
        code.push_back(push);
    }
    requireRoundTrip(code, "literals");
}

void testRoundTripAcrossIndexStridesAndSegments() {
    std::ostringstream source;
    for (int i = 0; i < 4000; ++i)
        source << "a" << i % 37 << " = " << i << " + b$ + \"text" << i % 11 << "\"\n";
    const auto code = compileSource(source.str());
    require(code.size() > 4 * 1024, "program too small to span several segments");
    requireRoundTrip(code, "long program");
    const auto stats = CompactProgram::encode(code).stats();
    require(stats.segments > 2, "expected multiple 512 B segments");
}

void testSixteenBitFieldLimitsRoundTrip() {
    auto code = compileSource("x = 1\n");
    Instruction edge{Op::Push, 1.0, {}, 65535, 65535};
    edge.basicLine = 65535;
    edge.setArgCount(65535);
    edge.setOptionCount(65535);
    edge.setLoopId(4000000000u);
    edge.target = edge.exitTarget = 0xFFFFFFFFu;
    code.push_back(edge);
    requireRoundTrip(code, "16-bit limits");
}

void testExtrasAndControlFlowRoundTrip() {
    requireRoundTrip(compileSource(
        "DEFine PROCedure p(a, b$)\nLOCal i\nFOR i = 1 TO 3 STEP 2\nPRINT a; b$\nEND FOR i\nEND DEFine\n"
        "DEFine FuNction f(x)\nRETurn x * 2\nEND DEFine\n"
        "INPUT n$, m\np 1, \"s\"\nPRINT f(2)\n"), "control flow");
}

void testDecodeEqualityDetectsDifferences() {
    auto code = compileSource("x = 1\ny = 2\n");
    auto program = CompactProgram::encode(code);
    code[1].target += 1;
    std::string why;
    require(!instructionsEqual(code[1], program.decode(1), &why) && why == "target", "mismatch not detected");
    bool threw = false;
    try { program.decode(code.size()); } catch (const std::out_of_range&) { threw = true; }
    require(threw, "decode past end must throw");
}

void testTruncateAndAppendMatchRebuild() {
    const auto code = compileSource(
        "DEFine PROCedure p(a, b$)\nLOCal i\nFOR i = 1 TO 3\nPRINT a; b$\nEND FOR i\nEND DEFine\n"
        "x = 1.5\ny$ = \"hello\"\np 1, y$\np 2, \"z\"\nPRINT x + 100000000000\n");
    require(code.size() > 20, "program too small");
    for (std::size_t keep = 0; keep <= code.size(); ++keep) {
        auto program = CompactProgram::encode(code);
        program.truncate(keep);
        require(program.size() == keep, "truncate size");
        for (std::size_t i = keep; i < code.size(); ++i) program.append(code[i]);
        std::string why;
        for (std::size_t i = 0; i < code.size(); ++i)
            require(instructionsEqual(code[i], program.decode(i), &why),
                    "truncate at " + std::to_string(keep) + ", instruction " + std::to_string(i) + ": " + why);
    }
    // Sequential reads (continuation) must equal random-access reads, wrapping the ring.
    InstructionSequence mutated = code;
    mutated.makeCompact();
    for (std::size_t i = 0; i < code.size(); ++i)
        require(instructionsEqual(code[i], mutated.instructionAt(i), nullptr), "sequential decode");
    for (std::size_t i = code.size(); i-- > 0;)
        require(instructionsEqual(code[i], mutated.instructionAt(i), nullptr), "reverse decode");
    // Relocating into a slab keeps the program intact, including appends and truncation afterwards.
    mutated.compact.get()->relocate();
    for (std::size_t i = 0; i < code.size(); ++i)
        require(instructionsEqual(code[i], mutated.instructionAt(i), nullptr), "decode after relocate");
    mutated.truncateInstructions(code.size() / 2);
    for (std::size_t i = code.size() / 2; i < code.size(); ++i) mutated.appendInstruction(code[i]);
    for (std::size_t i = 0; i < code.size(); ++i)
        require(instructionsEqual(code[i], mutated.instructionAt(i), nullptr), "decode after relocate+edit");
}

void testCorpus(const std::filesystem::path& root) {
    std::size_t programs = 0, skipped = 0, instructions = 0, code = 0, positions = 0;
    for (const char* dir : {"examples", "tests"}) {
        const auto base = root / dir;
        if (!std::filesystem::exists(base)) continue;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(base)) {
            if (entry.path().extension() != ".bas") continue;
            std::ifstream in(entry.path(), std::ios::binary);
            std::stringstream text;
            text << in.rdbuf();
            InstructionSequence compiled;
            try {
                compiled = compileSource(expandIncludes(text.str(), nullptr));
            } catch (const std::exception&) {
                ++skipped;  // programs that need services or are meant to fail compilation
                continue;
            }
            requireRoundTrip(compiled, entry.path().string());
            const auto stats = CompactProgram::encode(compiled).stats();
            ++programs;
            instructions += stats.instructions;
            code += stats.codeBytes;
            positions += stats.positionBytes;
        }
    }
    std::cout << "corpus: " << programs << " programs (" << skipped << " skipped), " << instructions
              << " instructions, code " << code << " B, positions " << positions << " B\n";
    if (root != std::filesystem::path{}) require(programs > 0, "no corpus programs compiled");
}

} // namespace

// CompactState::at() returns references into an 8-slot ring: the newest eight
// distinct instructions must stay valid, and wrapping must keep decoding exactly.
void testRingOfEightKeepsRecentInstructionsValid() {
    const auto code = compileSource(
        "DEFine PROCedure p(a, b$)\nPRINT a; b$\nEND DEFine\nFOR i = 1 TO 20\np i, \"s\"\nEND FOR i\nx$ = \"abc\"\ny = 1.5 * 3\np 2, x$\nPRINT y + 7\n");
    require(code.size() > 24, "ring test program too small");
    CompactState state;
    state.program = CompactProgram::encode(code);
    std::string why;
    std::vector<const Instruction*> held;
    for (std::size_t i = 0; i < 8; ++i) held.push_back(&state.at(i));
    for (std::size_t i = 0; i < 8; ++i)
        require(instructionsEqual(*held[i], code[i], &why), "ring slot " + std::to_string(i) + " corrupted: " + why);
    for (std::size_t round = 0; round < 3; ++round)
        for (std::size_t i = 0; i < code.size(); ++i) {
            const Instruction& now = state.at(i);
            require(instructionsEqual(now, code[i], &why), "ring wrap decode " + std::to_string(i) + ": " + why);
            if (i >= 7) {
                // The previous seven accesses must still be intact (borrowed extras included).
                for (std::size_t back = 1; back < 8; ++back)
                    require(instructionsEqual(state.at(i - back), code[i - back], &why),
                            "ring lost recent slot " + std::to_string(i - back) + ": " + why);
                require(instructionsEqual(state.at(i), code[i], &why), "ring re-read " + why);
            }
        }
    // Random-order access after wrapping, then invalidate and re-read.
    for (std::size_t i = code.size(); i-- > 0;)
        require(instructionsEqual(state.at(i), code[i], &why), "reverse decode " + std::to_string(i) + ": " + why);
    state.invalidate();
    for (std::size_t i = 0; i < code.size(); i += 3)
        require(instructionsEqual(state.at(i), code[i], &why), "post-invalidate decode: " + why);
}

// Extras (names, argument lists) of instructions dropped by truncate must not leak into
// instructions appended afterwards (runtime EVAL rollback).
void testTruncateThenAppendKeepsExtrasConsistent() {
    const auto code = compileSource("DEFine PROCedure p(a, b$)\nPRINT a; b$\nEND DEFine\np 1, \"x\"\n");
    CompactProgram program = CompactProgram::encode(code);
    const std::size_t keep = code.size() / 2;
    std::string why;
    for (int round = 0; round < 5; ++round) {
        program.truncate(keep);
        require(program.size() == keep, "truncate size");
        for (std::size_t i = keep; i < code.size(); ++i) program.append(code[i]);
        require(program.size() == code.size(), "append size");
        for (std::size_t i = 0; i < code.size(); ++i)
            require(instructionsEqual(program.decode(i), code[i], &why),
                    "round " + std::to_string(round) + " decode " + std::to_string(i) + ": " + why);
    }
}

int main(int argc, char** argv) {
    testLiteralsSurviveExactly();
    testRoundTripAcrossIndexStridesAndSegments();
    testSixteenBitFieldLimitsRoundTrip();
    testExtrasAndControlFlowRoundTrip();
    testDecodeEqualityDetectsDifferences();
    testTruncateAndAppendMatchRebuild();
    testRingOfEightKeepsRecentInstructionsValid();
    testTruncateThenAppendKeepsExtrasConsistent();
    if (argc > 1) testCorpus(argv[1]);
    std::cout << "compact program tests passed\n";
}
