// Host-only measurement of compiled bytecode footprint per BASIC program.
// Usage: ql32_bytecode_footprint file.bas...   (CSV on stdout)
#include "language/bytecode.hpp"
#include "language/compact_program.hpp"

#include <cstdlib>
#include <new>

#include <fstream>
#include <iostream>
#include <set>
#include <sstream>

using namespace ql32::detail;

// Instrumented allocator: live and peak bytes of every operator new.
namespace {
std::size_t gLive = 0, gPeak = 0;
constexpr std::size_t Header = 16;
} // namespace
void* operator new(std::size_t size) {
    void* raw = std::malloc(size + Header);
    if (!raw) throw std::bad_alloc();
    *static_cast<std::size_t*>(raw) = size;
    gLive += size;
    if (gLive > gPeak) gPeak = gLive;
    return static_cast<char*>(raw) + Header;
}
void operator delete(void* p) noexcept {
    if (!p) return;
    void* raw = static_cast<char*>(p) - Header;
    gLive -= *static_cast<std::size_t*>(raw);
    std::free(raw);
}
void operator delete(void* p, std::size_t) noexcept { operator delete(p); }

namespace {

std::size_t heapOf(const std::string& s) { return s.capacity() > 15 ? s.capacity() + 1 : 0; }

} // namespace

int main(int argc, char** argv) {
    std::cout << "file,instructions,record_bytes,out_of_line_bytes,total_bytes,"
                 "name_refs,distinct_names,push_values,distinct_push_values,"
                 "functions,arrays,constants,data_items,heap_retained,heap_peak,"
                 "compact_code,compact_positions,compact_index,compact_names,compact_extras,"
                 "compact_bytes_per_instr,compact_heap\n";
    for (int i = 1; i < argc; ++i) {
        std::ifstream in(argv[i], std::ios::binary);
        std::stringstream text;
        text << in.rdbuf();
        std::string source;
        try {
            source = expandIncludes(text.str(), nullptr);
        } catch (const std::exception&) {
            continue;
        }
        InstructionSequence code;
        const std::size_t liveBefore = gLive;
        gPeak = gLive;
        try {
            code = compileSource(source);
        } catch (const std::exception&) {
            continue;
        }
        const std::size_t retained = gLive - liveBefore;
        const std::size_t peak = gPeak - liveBefore;
        const std::size_t beforeCompact = gLive;
        auto compactProgram = CompactProgram::encode(code);
        const std::size_t compactRetained = gLive - beforeCompact;
        const auto compact = compactProgram.stats();
        std::size_t outOfLine = 0, nameRefs = 0, pushValues = 0;
        std::set<std::string> names, pushes;
        std::set<const void*> counted;
        // Shared text: one control block plus string object plus any heap buffer.
        auto shared = [&](const SharedText& t) {
            if (t.empty() || !counted.insert(t.storageId()).second) return;
            outOfLine += 2 * sizeof(void*) + sizeof(std::string) + heapOf(t.str());
        };
        for (const auto& ins : code) {
            shared(ins.name);
            if (!ins.name.empty()) { ++nameRefs; names.insert(ins.name); }
            if (ins.op == Op::Push) {
                ++pushValues;
                pushes.insert(ins.textLiteral ? "s:" + ins.name.str()
                                              : "n:" + std::to_string(ins.literal()));
            }
            auto vec = [&](const auto& v, std::size_t elem) {
                if (!v.empty()) outOfLine += sizeof(void*) * 3 + v.size() * elem;
            };
            if (&ins.extra() != &Instruction{}.extra()) outOfLine += sizeof(InstructionExtras);
            vec(ins.extra().forItems, sizeof(ForItemSpec));
            vec(ins.extra().selectItems, sizeof(SelectItemSpec));
            vec(ins.extra().jumpScopes, sizeof(std::size_t));
            vec(ins.extra().arrayArgumentCandidates, sizeof(ArrayArgumentCandidate));
            for (const auto& s : ins.extra().parameters) outOfLine += sizeof(s) + heapOf(s);
            for (const auto& s : ins.extra().inputVariables) outOfLine += sizeof(s) + heapOf(s);
            for (const auto& s : ins.extra().localNames) outOfLine += sizeof(s) + heapOf(s);
            if (!ins.extra().ownerDefinition.empty()) outOfLine += sizeof(std::string) + 8 + ins.extra().ownerDefinition.get().size();
            if (!ins.extra().targetVariable.empty()) outOfLine += sizeof(std::string) + 8 + ins.extra().targetVariable.get().size();
        }
        const std::size_t record = code.size() * sizeof(Instruction);
        std::cout << argv[i] << ',' << code.size() << ',' << record << ',' << outOfLine << ','
                  << record + outOfLine << ',' << nameRefs << ',' << names.size() << ','
                  << pushValues << ',' << pushes.size() << ',' << code.functions.size() << ','
                  << code.arrays.size() << ',' << code.constants.size() << ','
                  << code.dataItems.size() << ',' << retained << ',' << peak << ',' << compact.codeBytes << ','
                  << compact.positionBytes << ',' << compact.indexBytes << ',' << compact.nameTableEntries
                  << ',' << compact.extrasRecords << ','
                  << (code.empty() ? 0.0 : double(compact.codeBytes + compact.positionBytes + compact.indexBytes) / code.size()) << ','
                  << compactRetained << '\n';
    }
    std::cerr << "sizeof(Instruction)=" << sizeof(Instruction) << " sizeof(Value)="
              << sizeof(ql32::Value) << '\n';
}
