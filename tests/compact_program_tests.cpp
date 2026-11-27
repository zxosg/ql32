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
    require(stats.segments > 2, "expected multiple 1 KB segments");
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

int main(int argc, char** argv) {
    testLiteralsSurviveExactly();
    testRoundTripAcrossIndexStridesAndSegments();
    testSixteenBitFieldLimitsRoundTrip();
    testExtrasAndControlFlowRoundTrip();
    testDecodeEqualityDetectsDifferences();
    if (argc > 1) testCorpus(argv[1]);
    std::cout << "compact program tests passed\n";
}
