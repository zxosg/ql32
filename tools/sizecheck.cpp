#include "language/bytecode.hpp"
#include "vm/execution_state.hpp"
#include "ql32/vm/scheduler.hpp"

#include <fstream>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

struct CompactValueProbe {
    enum class Tag : std::uint8_t { Number, String } tag = Tag::Number;
    union Payload { double number; std::uint32_t stringId; } payload{};
};

struct StringPoolEntryProbe {
    std::string text;
    std::uint32_t references = 0;
    std::uint32_t nextFree = 0;
};

struct StringInternNodeProbe {
    std::string text;
    std::uint32_t stringId = 0;
};

struct BufferAllocationProbe {
    std::uint64_t base = 0;
    std::vector<std::uint8_t> bytes;
};

struct BufferMapValueProbe {
    std::uint64_t key = 0;
    BufferAllocationProbe allocation;
};

std::string readSource(int argc, char** argv) {
    if (argc > 2) {
        std::cerr << "usage: ql32_sizecheck [program.bas]\n";
        std::exit(2);
    }
    if (argc == 2) {
        std::ifstream file(argv[1], std::ios::binary);
        if (!file) {
            std::cerr << "cannot open " << argv[1] << '\n';
            std::exit(2);
        }
        return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    }

    std::string source;
    for (std::size_t i = 0; i < 500; ++i) source += "PRINT 1\n";
    return source;
}

void printStringPoolTradeoff() {
    constexpr std::size_t count = 1000;
    const std::size_t currentEmptyArray = count * sizeof(std::string);
    const std::size_t sharedEmptyPool = count * sizeof(std::uint32_t) +
        sizeof(StringPoolEntryProbe) + sizeof(StringInternNodeProbe) + 4 * sizeof(void*);
    const std::size_t currentUniqueArray = count * sizeof(std::string);
    const std::size_t uniquePool = count * (sizeof(std::uint32_t) +
        sizeof(StringPoolEntryProbe) + sizeof(StringInternNodeProbe) + 4 * sizeof(void*));
    std::cout << "string representation probe (structural host estimate, 1000 array entries):\n"
              << "  sizeof(compact tagged value): " << sizeof(CompactValueProbe) << " bytes\n"
              << "  sizeof(pool entry with refcount/free link): "
              << sizeof(StringPoolEntryProbe) << " bytes\n"
              << "  sizeof(intern index node): " << sizeof(StringInternNodeProbe) << " bytes\n"
              << "  current std::string array objects, repeated empty: "
              << currentEmptyArray << " bytes\n"
              << "  pooled handles + one shared empty + one index node: "
              << sharedEmptyPool << " bytes\n"
              << "  current std::string array objects, 1000 unique SSO strings: "
              << currentUniqueArray << " bytes\n"
              << "  pooled handles + 1000 unique entries and index nodes: "
              << uniquePool << " bytes\n"
              << "  caveat: excludes string payload allocations, map buckets, allocator metadata, "
                 "and hashing scratch; this is a representation comparison, not a measured heap census\n";
}

void printBufferIndexTradeoff() {
    constexpr std::size_t allocationCount = 1000;
    constexpr std::size_t vectorCapacity = 1024;
    const std::size_t mapNodes = allocationCount *
        (sizeof(BufferMapValueProbe) + 4 * sizeof(void*));
    const std::size_t vectorRecords = vectorCapacity * sizeof(BufferAllocationProbe);
    std::cout << "buffer allocation index probe (structural host estimate, 1000 allocations):\n"
              << "  allocation record: " << sizeof(BufferAllocationProbe) << " bytes\n"
              << "  map value estimate: " << sizeof(BufferMapValueProbe) << " bytes\n"
              << "  current tree nodes, including estimated links: " << mapNodes << " bytes\n"
              << "  sorted vector records at capacity 1024: " << vectorRecords << " bytes\n"
              << "  caveat: tree links, allocator rounding, and vector growth vary by STL; "
                 "payload byte vectors are excluded\n";
}

} // namespace

int main(int argc, char** argv) {
    using namespace ql32;
    using namespace ql32::detail;
    const std::string source = readSource(argc, argv);
    CompactProgram compact;
    std::size_t instructionCount = 0;
    std::size_t richSequenceEstimate = 0;
#ifdef QL32_TEST_LEGACY_COMPILER
    const InstructionSequence code = compileSource(source);
    instructionCount = code.size();
    richSequenceEstimate = estimateInstructionSequenceBytes(code);
    compact = CompactProgram(InstructionSequence(code));
#else
    compact = compileSourceCompact(source);
    instructionCount = compact.size();
#endif

    std::cout << "sizeof(Value): " << sizeof(Value) << " bytes\n"
              << "sizeof(StackEntry): " << sizeof(StackEntry) << " bytes\n"
              << "sizeof(Instruction): " << sizeof(Instruction) << " bytes\n"
              << "sizeof(RuntimeFrame): " << sizeof(RuntimeFrame) << " bytes\n"
              << "sizeof(RuntimeArray): " << sizeof(RuntimeArray) << " bytes\n"
              << "sizeof(unordered_map<string, Value>): "
              << sizeof(std::unordered_map<std::string, Value>) << " bytes\n"
              << "instruction count: " << instructionCount << '\n'
              << "instruction record bytes: " << instructionCount * sizeof(Instruction) << '\n';
#ifdef QL32_TEST_LEGACY_COMPILER
    std::cout << "estimated bytecode sequence storage: " << richSequenceEstimate << " bytes\n"
              << "estimate caveat: counts string capacities even when inline; excludes "
                 "allocator bookkeeping and deque block slack\n";
#else
    std::cout << "legacy rich sequence estimate unavailable in this build\n";
#endif
    std::cout << "compact instruction record bytes: "
              << sizeof(CompactInstruction) << " bytes\n"
              << "compact records: " << compact.compactRecordCount() << '\n'
              << "rich sidecar instructions: " << compact.sidecarCount() << '\n'
              << "encoded program estimate including side tables: "
              << compact.encodedStorageBytes() << " bytes\n";

    printStringPoolTradeoff();
    printBufferIndexTradeoff();

    ql32::vm::SessionScheduler scheduler({}, 1024);
    std::ostringstream output;
    const auto task = scheduler.launch(
        "10 globalValue=7\n"
        "20 DIM numbers(99)\n"
        "30 numbers(0)=globalValue\n"
        "40 result=Measure(5)\n"
        "50 PRINT numbers(0)\n"
        "100 DEFine FuNction Measure(value)\n"
        "110 LOCal temporary\n"
        "120 temporary=value+1\n"
        "130 RETurn temporary\n"
        "140 END DEFine Measure\n", output);
    if (!scheduler.debugAttach(task) || !scheduler.debugSetDiagnostics(task, true, false) ||
        !scheduler.debugSetBreakpoint(task, 120)) {
        std::cerr << "could not configure runtime metrics probe\n";
        return 1;
    }
    bool paused = false;
    for (int tick = 0; tick < 4 && !paused; ++tick) {
        if (!scheduler.tick()) break;
        const auto status = scheduler.task(task);
        paused = status && status->status == ql32::vm::TaskStatus::DebugPaused;
    }
    const auto snapshot = scheduler.debugInspect(task);
    if (!paused || !snapshot) {
        std::cerr << "runtime metrics probe did not reach its breakpoint\n";
        return 1;
    }
    std::cout << "runtime bytecode storage estimate: " << snapshot->bytecodeStorageBytes
              << " bytes\n"
              << "runtime global storage estimate: " << snapshot->globalStorageBytes
              << " bytes\n"
              << "runtime local storage estimate: " << snapshot->localStorageBytes
              << " bytes\n"
              << "runtime array storage estimate: " << snapshot->arrayStorageBytes
              << " bytes\n"
              << "runtime operand stack high-water: " << snapshot->stackHighWater
              << " entries\n"
              << "runtime call frame high-water: " << snapshot->frameHighWater
              << " frames\n";
}
