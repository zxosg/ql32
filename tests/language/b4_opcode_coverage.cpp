#include "language/bytecode.hpp"

#include <array>
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace ql32::detail;

constexpr std::array<std::pair<Op, const char*>, 50> allOps{{
    {Op::Push, "Push"}, {Op::Load, "Load"}, {Op::Store, "Store"},
    {Op::Add, "Add"}, {Op::Subtract, "Subtract"}, {Op::Multiply, "Multiply"},
    {Op::Divide, "Divide"}, {Op::IntegerDivide, "IntegerDivide"},
    {Op::Modulo, "Modulo"}, {Op::Power, "Power"}, {Op::Equal, "Equal"},
    {Op::NotEqual, "NotEqual"}, {Op::Less, "Less"}, {Op::LessEqual, "LessEqual"},
    {Op::Greater, "Greater"}, {Op::GreaterEqual, "GreaterEqual"},
    {Op::And, "And"}, {Op::Or, "Or"}, {Op::Xor, "Xor"}, {Op::Negate, "Negate"},
    {Op::Not, "Not"}, {Op::PrintBegin, "PrintBegin"},
    {Op::PrintChannel, "PrintChannel"}, {Op::Print, "Print"},
    {Op::PrintSeparator, "PrintSeparator"}, {Op::PrintEnd, "PrintEnd"},
    {Op::ChannelDefault, "ChannelDefault"}, {Op::ChannelRoute, "ChannelRoute"},
    {Op::ChannelSeek, "ChannelSeek"}, {Op::ChannelRead, "ChannelRead"},
    {Op::ChannelWrite, "ChannelWrite"}, {Op::Input, "Input"},
    {Op::JumpIfFalse, "JumpIfFalse"}, {Op::Jump, "Jump"},
    {Op::ForInit, "ForInit"}, {Op::ForNext, "ForNext"}, {Op::LoopNext, "LoopNext"},
    {Op::ExitLoop, "ExitLoop"}, {Op::Call, "Call"}, {Op::Return, "Return"},
    {Op::FunctionEnd, "FunctionEnd"}, {Op::DimArray, "DimArray"},
    {Op::ArrayGet, "ArrayGet"}, {Op::ArraySet, "ArraySet"},
    {Op::StringSlice, "StringSlice"}, {Op::PushArrayRef, "PushArrayRef"},
    {Op::SelectBegin, "SelectBegin"}, {Op::SelectMatch, "SelectMatch"},
    {Op::SelectRemainder, "SelectRemainder"}, {Op::SelectEnd, "SelectEnd"},
}};
static_assert(allOps.size() == static_cast<std::size_t>(Op::SelectEnd) + 1,
              "update the B4 corpus when the Op enum changes");

template <typename Left, typename Right, typename Equal>
bool sameSequence(const Left& left, const Right& right, Equal equal) {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0; index < left.size(); ++index)
        if (!equal(left[index], right[index])) return false;
    return true;
}

bool sameInstruction(const Instruction& a, const Instruction& b) {
    const auto sameForItem = [](const ForItemSpec& x, const ForItemSpec& y) {
        return x.hasEnd == y.hasEnd && x.hasStep == y.hasStep;
    };
    const auto sameCandidate = [](const ArrayArgumentCandidate& x,
                                  const ArrayArgumentCandidate& y) {
        return x.parameterIndex == y.parameterIndex && x.name == y.name &&
            x.loadInstruction == y.loadInstruction;
    };
    const auto sameSelectItem = [](const SelectItemSpec& x, const SelectItemSpec& y) {
        return x.hasEnd == y.hasEnd;
    };
    return a.op == b.op && a.value == b.value && a.name == b.name &&
        a.line == b.line && a.column == b.column && a.separator == b.separator &&
        a.target == b.target && a.exitTarget == b.exitTarget && a.loopId == b.loopId &&
        sameSequence(a.forItems, b.forItems, sameForItem) &&
        a.explicitNext == b.explicitNext && a.clearTarget == b.clearTarget &&
        a.argCount == b.argCount && a.expectsValue == b.expectsValue &&
        a.builtin == b.builtin && a.channelArgument == b.channelArgument &&
        sameSequence(a.parameters, b.parameters, [](const auto& x, const auto& y) {
            return x == y;
        }) && sameSequence(a.localNames, b.localNames, [](const auto& x, const auto& y) {
            return x == y;
        }) && sameSequence(a.arrayArgumentCandidates, b.arrayArgumentCandidates,
                           sameCandidate) &&
        a.ownerDefinition == b.ownerDefinition && a.targetVariable == b.targetVariable &&
        a.symbolSlot == b.symbolSlot && a.localSlot == b.localSlot && a.rank == b.rank &&
        a.sliceHasStart == b.sliceHasStart && a.sliceHasEnd == b.sliceHasEnd &&
        a.sliceSingle == b.sliceSingle &&
        sameSequence(a.selectItems, b.selectItems, sameSelectItem) &&
        a.sourceJump == b.sourceJump && a.targetIsLineNumber == b.targetIsLineNumber &&
        a.statementStart == b.statementStart && a.basicLine == b.basicLine &&
        a.sourceLineTarget == b.sourceLineTarget &&
        sameSequence(a.jumpScopes, b.jumpScopes, [](auto x, auto y) { return x == y; });
}

bool sameCompactEncoding(const CompactProgram& a, const CompactProgram& b) {
    if (a.encodedRecords().size() != b.encodedRecords().size() ||
        a.encodedSidecars().size() != b.encodedSidecars().size() ||
        a.encodedStringLiterals() != b.encodedStringLiterals() ||
        a.functions.size() != b.functions.size() || a.arrays != b.arrays ||
        a.constants != b.constants)
        return false;
    for (std::size_t i = 0; i < a.encodedRecords().size(); ++i)
        if (std::memcmp(&a.encodedRecords()[i], &b.encodedRecords()[i],
                        sizeof(CompactInstruction)) != 0)
            return false;
    for (std::size_t i = 0; i < a.encodedSidecars().size(); ++i)
        if (!sameInstruction(a.encodedSidecars()[i], b.encodedSidecars()[i]))
            return false;
    for (const auto& [name, left] : a.functions) {
        const auto found = b.functions.find(name);
        if (found == b.functions.end()) return false;
        const auto& right = found->second;
        if (left.address != right.address || left.function != right.function ||
            left.parameters != right.parameters || left.locals != right.locals ||
            left.localSymbols != right.localSymbols)
            return false;
    }
    return a.numericPrecision == b.numericPrecision &&
        a.cpuFrequencyMHz == b.cpuFrequencyMHz;
}

std::vector<std::string> discoverFixtures(const std::string& root) {
    namespace fs = std::filesystem;
    std::vector<std::string> fixtures;
    for (const auto& entry : fs::directory_iterator(root + "/tests/cli")) {
        const std::string filename = entry.path().filename().string();
        // These are REPL command streams (LIST/RUN and NEW/CLEAR), not source
        // units that compileSource can consume as one BASIC program.
        if (entry.path().extension() == ".bas" && filename != "cli_abbreviations.bas" &&
            filename != "cli_program_commands.bas")
            fixtures.push_back(fs::relative(entry.path(), root).generic_string());
    }
    for (const auto& entry : fs::directory_iterator(root + "/tests/language")) {
        const std::string filename = entry.path().filename().string();
        if (entry.path().extension() == ".bas" && filename.rfind("b4_", 0) == 0)
            fixtures.push_back(fs::relative(entry.path(), root).generic_string());
    }
    fixtures.push_back("examples/attractor.bas");
    fixtures.push_back("examples/mandelbrot.bas");
    for (const auto& entry : fs::directory_iterator(root + "/benchmarks")) {
        if (entry.path().extension() == ".bas")
            fixtures.push_back(fs::relative(entry.path(), root).generic_string());
    }
    std::sort(fixtures.begin(), fixtures.end());
    fixtures.erase(std::unique(fixtures.begin(), fixtures.end()), fixtures.end());
    return fixtures;
}

std::string readFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("cannot open fixture: " + path);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: ql32_b4_opcode_coverage <source-root>\n";
        return 2;
    }
    const std::string root = argv[1];
    const std::vector<std::string> fixtures = discoverFixtures(root);
    std::array<bool, allOps.size()> seen{};
    for (const std::string& relative : fixtures) {
        const std::string path = root + "/" + relative;
        const std::string source = readFile(path);
        InstructionSequence rich;
        std::string legacyError;
        try {
            rich = compileSource(source);
        } catch (const std::exception& error) {
            legacyError = error.what();
        }
        std::string directError;
        CompactProgram direct;
#ifdef QL32_TEST_COMPACT
        try {
            direct = compileSourceCompact(source);
        } catch (const std::exception& error) {
            directError = error.what();
        }
        if (legacyError != directError) {
            std::cerr << path << ": legacy/direct compile diagnostics differ: ["
                      << legacyError << "] / [" << directError << "]\n";
            return 1;
        }
        if (!legacyError.empty()) continue;
#else
        if (!legacyError.empty()) {
            std::cerr << path << ": " << legacyError << '\n';
            return 1;
        }
#endif
        for (const Instruction& instruction : rich) {
            const auto index = static_cast<std::size_t>(instruction.op);
            if (index >= seen.size()) {
                std::cerr << path << ": opcode value is outside the coverage table\n";
                return 1;
            }
            seen[index] = true;
        }
#ifdef QL32_TEST_COMPACT
        const std::vector<Instruction> expected(rich.begin(), rich.end());
        CompactProgram compact(std::move(rich));
        if (!sameCompactEncoding(compact, direct)) {
            std::cerr << path << ": direct and legacy compact encodings differ"
                      << " (records " << compact.encodedRecords().size() << '/'
                      << direct.encodedRecords().size() << ", sidecars "
                      << compact.encodedSidecars().size() << '/'
                      << direct.encodedSidecars().size() << ", literals "
                      << compact.encodedStringLiterals().size() << '/'
                      << direct.encodedStringLiterals().size() << ")\n";
            for (std::size_t i = 0; i < compact.size() && i < direct.size(); ++i) {
                Instruction left, right;
                if (!sameInstruction(compact.instructionAt(i, left),
                                     direct.instructionAt(i, right))) {
                    std::cerr << "first semantic difference at instruction " << i << '\n';
                    break;
                }
            }
            for (std::size_t i = 0; i < compact.encodedRecords().size() &&
                 i < direct.encodedRecords().size(); ++i) {
                const auto& a = compact.encodedRecords()[i];
                const auto& b = direct.encodedRecords()[i];
                if (std::memcmp(&a, &b, sizeof(CompactInstruction)) != 0) {
                    std::uint64_t leftPayload = 0, rightPayload = 0;
                    std::memcpy(&leftPayload, &a.payload, sizeof(leftPayload));
                    std::memcpy(&rightPayload, &b.payload, sizeof(rightPayload));
                    std::cerr << "first record difference " << i << ": "
                              << unsigned(a.opcode) << '/' << unsigned(b.opcode) << " flags "
                              << unsigned(a.flags) << '/' << unsigned(b.flags) << " loc "
                              << a.line << ':' << a.column << '/' << b.line << ':' << b.column
                              << " payload " << leftPayload << '/' << rightPayload << '\n';
                    break;
                }
            }
            if (compact.functions.size() != direct.functions.size())
                std::cerr << "function metadata sizes differ\n";
            return 1;
        }
        Instruction scratch;
        for (std::size_t index = 0; index < expected.size(); ++index) {
            if (!sameInstruction(expected[index], compact.instructionAt(index, scratch))) {
                std::cerr << path << ": compact round-trip mismatch at instruction "
                          << index << '\n';
                return 1;
            }
        }
#endif
    }

#ifdef QL32_TEST_COMPACT
    InstructionSequence enclosingRich = compileSource(
        "DIM data(2)\n"
        "DEFine FuNction AddLocal(localValue)\n"
        "RETurn localValue + 1\n"
        "END DEFine AddLocal\n");
    CompactProgram enclosing(std::move(enclosingRich));
    for (const std::string expression : {"8+9", "AddLocal(4)", "data(0)+1"}) {
        InstructionSequence richExpression;
        try {
            richExpression = compileExpression(expression, enclosing);
        } catch (const std::exception& error) {
            std::cerr << "legacy EVAL expression failed: " << error.what() << '\n';
            return 1;
        }
        CompactProgram oldExpression(std::move(richExpression));
        CompactProgram directExpression;
        try {
            directExpression = compileExpressionCompact(expression, enclosing);
        } catch (const std::exception& error) {
            std::cerr << "direct EVAL expression failed: " << error.what() << '\n';
            return 1;
        }
        if (!sameCompactEncoding(oldExpression, directExpression)) {
            std::cerr << "EVAL expression compact encodings differ: " << expression << '\n';
            return 1;
        }
    }
#endif

    bool complete = true;
    for (const auto& item : allOps) {
        if (!seen[static_cast<std::size_t>(item.first)]) {
            std::cerr << "opcode has no B4 fixture: " << item.second << '\n';
            complete = false;
        }
    }
    if (!complete) return 1;
    std::cout << "B4 corpus emitted all " << allOps.size() << " opcodes across "
              << fixtures.size() << " source fixtures";
#ifdef QL32_TEST_COMPACT
    std::cout << " and round-tripped every instruction field";
    std::cout << " and matched direct compact emission";
#endif
    std::cout << '\n';
}
