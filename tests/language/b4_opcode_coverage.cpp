#include "language/bytecode.hpp"

#include <array>
#include <cstddef>
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
    const std::array<const char*, 17> fixtures{{
        "tests/cli/cli_numeric_portable.bas",
        "tests/cli/cli_expression_rules.bas",
        "tests/cli/cli_functions.bas",
        "tests/cli/cli_procedure_locals.bas",
        "tests/cli/cli_arrays.bas",
        "tests/cli/cli_array_by_reference.bas",
        "tests/cli/cli_select_edges.bas",
        "tests/cli/cli_language_slice.bas",
        "tests/cli/cli_call_depth.bas",
        "tests/cli/cli_array_bounds.bas",
        "tests/cli/cli_div_overflow.bas",
        "tests/cli/cli_print_separators.bas",
        "tests/cli/cli_repeat_control.bas",
        "tests/cli/cli_for_exit.bas",
        "tests/language/b4_extra_opcodes.bas",
        "tests/language/b4_input_compile.bas",
        "examples/mandelbrot.bas",
    }};
    std::array<bool, allOps.size()> seen{};
    for (const char* relative : fixtures) {
        const std::string path = root + "/" + relative;
        InstructionSequence rich;
        try {
            rich = compileSource(readFile(path));
        } catch (const std::exception& error) {
            std::cerr << path << ": " << error.what() << '\n';
            return 1;
        }
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

    bool complete = true;
    for (const auto& item : allOps) {
        if (!seen[static_cast<std::size_t>(item.first)]) {
            std::cerr << "opcode has no B4 fixture: " << item.second << '\n';
            complete = false;
        }
    }
    if (!complete) return 1;
    std::cout << "B4 corpus emitted all " << allOps.size() << " opcodes across "
              << fixtures.size() << " fixtures";
#ifdef QL32_TEST_COMPACT
    std::cout << " and round-tripped every instruction field";
#endif
    std::cout << '\n';
}
