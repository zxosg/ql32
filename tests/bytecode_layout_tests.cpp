// Bytecode storage layout and sharing regressions (memory compaction).
#include "language/bytecode.hpp"

#include <cstdlib>
#include <iostream>
#include <sstream>

using namespace ql32::detail;

namespace {

void require(bool ok, const char* message) {
    if (!ok) {
        std::cerr << "bytecode layout test failed: " << message << '\n';
        std::exit(1);
    }
}

void testRecordSizeBudget() {
    // 48 B on LP64 (40 B on 32-bit) at the time of writing; the budget guards against regrowth.
    require(sizeof(Instruction) <= 48, "Instruction grew beyond its memory budget");
}

void testNamesAndStringLiteralsAreShared() {
    const auto code = compileSource("a$ = \"hello\"\nb$ = \"hello\"\nx = 1\nx = x + 1\n");
    const SharedText* firstLoadX = nullptr;
    const SharedText* firstHello = nullptr;
    for (const auto& instruction : code) {
        if (instruction.op == Op::Store && instruction.name == "X") {
            if (!firstLoadX) firstLoadX = &instruction.name;
            else require(instruction.name.storageId() == firstLoadX->storageId(),
                         "repeated identifier is not shared");
        }
        if (instruction.op == Op::Push && instruction.textLiteral) {
            if (!firstHello) firstHello = &instruction.name;
            else require(instruction.name.storageId() == firstHello->storageId(),
                         "repeated string literal is not shared");
        }
    }
    require(firstLoadX && firstHello, "expected instructions were not compiled");
}

void testLiteralsKeepTheirType() {
    const auto code = compileSource("PRINT \"\"; \"text\"; 2.5\n");
    int strings = 0, numbers = 0;
    for (const auto& instruction : code) {
        if (instruction.op != Op::Push) continue;
        if (instruction.textLiteral) ++strings;
        else { ++numbers; require(instruction.literal() == 2.5, "numeric literal changed"); }
    }
    require(strings == 2 && numbers == 1, "literal kinds changed");
}

void testInstructionsWithoutMetadataAllocateNothingExtra() {
    Instruction plain{Op::Add, 0.0, {}, 1, 1};
    require(&plain.extra() == &Instruction{}.extra(), "plain instruction allocated extras");
    Instruction withParameters = plain;
    withParameters.mutableExtra().parameters.push_back("A");
    Instruction copy = withParameters;
    copy.mutableExtra().parameters.push_back("B");
    require(withParameters.extra().parameters.size() == 1 && copy.extra().parameters.size() == 2,
            "instruction copy shares mutable metadata");
}

} // namespace

int main() {
    testRecordSizeBudget();
    testNamesAndStringLiteralsAreShared();
    testLiteralsKeepTheirType();
    testInstructionsWithoutMetadataAllocateNothingExtra();
    return 0;
}
