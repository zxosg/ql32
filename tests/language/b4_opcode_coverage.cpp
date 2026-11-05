#include "language/bytecode.hpp"

#include <array>
#include <algorithm>
#include <cstddef>
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
        CompactProgram program;
        try {
            program = compileSource(source);
        } catch (const std::exception& error) {
            // This fixture deliberately contains malformed BASIC; the CLI
            // golden test owns its exact diagnostic assertion.
            if (relative == "tests/cli/cli_syntax_error.bas") continue;
            std::cerr << path << ": " << error.what() << '\n';
            return 1;
        }

        Instruction firstPass;
        for (std::size_t address = 0; address < program.size(); ++address) {
            const Instruction& instruction = program.instructionAt(address, firstPass);
            const auto index = static_cast<std::size_t>(instruction.op);
            if (index >= seen.size()) {
                std::cerr << path << ": opcode value is outside the coverage table\n";
                return 1;
            }
            seen[index] = true;
        }
    }

    CompactProgram enclosing = compileSource(
        "DIM data(2)\n"
        "DEFine FuNction AddLocal(localValue)\n"
        "RETurn localValue + 1\n"
        "END DEFine AddLocal\n");
    for (const std::string expression : {"8+9", "AddLocal(4)", "data(0)+1"}) {
        CompactProgram compactExpression;
        try {
            compactExpression = compileExpression(expression, enclosing);
        } catch (const std::exception& error) {
            std::cerr << "EVAL expression failed: " << error.what() << '\n';
            return 1;
        }
        if (compactExpression.empty()) return 1;
    }

    bool complete = true;
    for (const auto& item : allOps) {
        if (!seen[static_cast<std::size_t>(item.first)]) {
            std::cerr << "opcode has no B4 fixture: " << item.second << '\n';
            complete = false;
        }
    }
    if (!complete) return 1;
    std::cout << "B4 direct compiler emitted all " << allOps.size() << " opcodes across "
              << fixtures.size() << " source fixtures and decoded EVAL expressions\n";
}
