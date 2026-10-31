#pragma once

#include "ql32/interpreter.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace ql32::detail {

enum class PrintSeparator { None, Semicolon, Exclamation, Comma, Backslash };
enum class Op {
    Push, Load, Store, Add, Subtract, Multiply, Divide, IntegerDivide, Modulo,
    Power, Equal, NotEqual, Less, LessEqual, Greater, GreaterEqual,
    And, Or, Xor, Negate, Not, Print, PrintSeparator, PrintEnd, JumpIfFalse, Jump,
    ForInit, ForNext, LoopNext, ExitLoop, Call, Return, FunctionEnd,
    DimArray, ArrayGet, ArraySet, PushArrayRef, SelectBegin, SelectMatch, SelectRemainder, SelectEnd
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
    std::vector<std::string> parameters;
    std::vector<std::string> localNames;
    std::vector<ArrayArgumentCandidate> arrayArgumentCandidates;
    std::string ownerDefinition;
    std::size_t rank = 0;
    std::vector<SelectItemSpec> selectItems;
};

std::vector<Instruction> compileSource(const std::string& source);

} // namespace ql32::detail
