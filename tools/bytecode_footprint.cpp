// Host-only measurement of compiled bytecode footprint per BASIC program.
// Usage: ql32_bytecode_footprint file.bas...   (CSV on stdout)
#include "language/bytecode.hpp"

#include <fstream>
#include <iostream>
#include <set>
#include <sstream>

using namespace ql32::detail;

namespace {

std::size_t heapOf(const std::string& s) { return s.capacity() > 15 ? s.capacity() + 1 : 0; }

std::size_t heapOf(const ql32::Value& v) {
    const auto* s = std::get_if<std::string>(&v);
    return s ? heapOf(*s) : 0;
}

} // namespace

int main(int argc, char** argv) {
    std::cout << "file,instructions,record_bytes,out_of_line_bytes,total_bytes,"
                 "name_refs,distinct_names,push_values,distinct_push_values,"
                 "functions,arrays,constants,data_items\n";
    for (int i = 1; i < argc; ++i) {
        std::ifstream in(argv[i], std::ios::binary);
        std::stringstream text;
        text << in.rdbuf();
        InstructionSequence code;
        try {
            code = compileSource(expandIncludes(text.str(), nullptr));
        } catch (const std::exception&) {
            continue;
        }
        std::size_t outOfLine = 0, nameRefs = 0, pushValues = 0;
        std::set<std::string> names, pushes;
        for (const auto& ins : code) {
            outOfLine += heapOf(ins.name) + heapOf(ins.value);
            if (!ins.name.empty()) { ++nameRefs; names.insert(ins.name); }
            if (ins.op == Op::Push) {
                ++pushValues;
                const auto* s = std::get_if<std::string>(&ins.value);
                pushes.insert(s ? "s:" + *s : "n:" + std::to_string(std::get<double>(ins.value)));
            }
            auto vec = [&](const auto& v, std::size_t elem) {
                if (!v.empty()) outOfLine += sizeof(void*) * 3 + v.size() * elem;
            };
            vec(ins.forItems, sizeof(ForItemSpec));
            vec(ins.selectItems, sizeof(SelectItemSpec));
            vec(ins.jumpScopes, sizeof(std::size_t));
            vec(ins.arrayArgumentCandidates, sizeof(ArrayArgumentCandidate));
            for (const auto& s : ins.parameters) outOfLine += sizeof(s) + heapOf(s);
            for (const auto& s : ins.inputVariables) outOfLine += sizeof(s) + heapOf(s);
            for (const auto& s : ins.localNames) outOfLine += sizeof(s) + heapOf(s);
            if (!ins.ownerDefinition.empty()) outOfLine += sizeof(std::string) + 8 + ins.ownerDefinition.get().size();
            if (!ins.targetVariable.empty()) outOfLine += sizeof(std::string) + 8 + ins.targetVariable.get().size();
        }
        const std::size_t record = code.size() * sizeof(Instruction);
        std::cout << argv[i] << ',' << code.size() << ',' << record << ',' << outOfLine << ','
                  << record + outOfLine << ',' << nameRefs << ',' << names.size() << ','
                  << pushValues << ',' << pushes.size() << ',' << code.functions.size() << ','
                  << code.arrays.size() << ',' << code.constants.size() << ','
                  << code.dataItems.size() << '\n';
    }
    std::cerr << "sizeof(Instruction)=" << sizeof(Instruction) << " sizeof(Value)="
              << sizeof(ql32::Value) << '\n';
}
