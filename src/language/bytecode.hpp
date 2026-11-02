#pragma once

#include "ql32/interpreter.hpp"

#include <cstddef>
#include <deque>
#include <string>
#include <vector>

namespace ql32::detail {

enum class PrintSeparator { None, Semicolon, Exclamation, Comma, Backslash };
enum class Op {
    Push, Load, Store, Add, Subtract, Multiply, Divide, IntegerDivide, Modulo,
    Power, Equal, NotEqual, Less, LessEqual, Greater, GreaterEqual,
    And, Or, Xor, Negate, Not, PrintBegin, PrintChannel, Print, PrintSeparator, PrintEnd,
    ChannelDefault, ChannelRoute, ChannelSeek, ChannelRead, ChannelWrite, Input, JumpIfFalse, Jump,
    ForInit, ForNext, LoopNext, ExitLoop, Call, Return, FunctionEnd,
    DimArray, ArrayGet, ArraySet, StringSlice, PushArrayRef, SelectBegin, SelectMatch, SelectRemainder, SelectEnd
};

struct ForItemSpec {
    bool hasEnd = false;
    bool hasStep = false;
};

struct SelectItemSpec {
    bool hasEnd = false;
};


struct ArrayArgumentCandidate {
    std::size_t parameterIndex = 0;
    std::string name;
    std::size_t loadInstruction = 0;
};


struct Instruction {
    Op op;
    Value value = 0.0;
    std::string name;
    int line = 1;
    int column = 1;
    PrintSeparator separator = PrintSeparator::None;
    std::size_t target = 0;
    std::size_t exitTarget = 0;
    std::size_t loopId = 0;
    std::vector<ForItemSpec> forItems;
    bool explicitNext = false;
    bool clearTarget = false;
    std::size_t argCount = 0;
    bool expectsValue = false;
    bool builtin = false;
    bool channelArgument = false;
    std::vector<std::string> parameters;
    std::vector<std::string> localNames;
    std::vector<ArrayArgumentCandidate> arrayArgumentCandidates;
    std::string ownerDefinition;
    std::string targetVariable;
    std::size_t rank = 0;
    bool sliceHasStart = false;
    bool sliceHasEnd = false;
    bool sliceSingle = false;
    std::vector<SelectItemSpec> selectItems;
    bool sourceJump = false;
    bool targetIsLineNumber = false;
    int sourceLineTarget = 0;
    std::vector<std::size_t> jumpScopes;
};

// Chunked instruction storage keeps bytecode growth from requiring one large
// contiguous allocation on memory-constrained targets.
using InstructionSequence = std::deque<Instruction>;

InstructionSequence compileSource(const std::string& source);
std::string expandIncludes(const std::string& source, runtime::StorageService* storage);

} // namespace ql32::detail
