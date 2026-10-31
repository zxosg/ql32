#include "ql32/interpreter.hpp"
#include "../language/bytecode.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <deque>
#include <limits>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>
#include <unordered_map>

namespace ql32 {
namespace {
using detail::Instruction;
using detail::Op;
using detail::PrintSeparator;
using detail::ForItemSpec;
using detail::SelectItemSpec;
using detail::ArrayArgumentCandidate;

struct ForRange { double start = 0; double end = 0; double step = 1; bool single = false; };
struct ForState { std::vector<ForRange> ranges; std::size_t item = 0; };
struct RuntimeFrame {
    std::size_t returnAddress = 0;
    bool function = false;
    std::unordered_map<std::string, Value> locals;
    std::unordered_map<std::string, std::string> arrayAliases;
};
struct ArrayReference { std::string name; };
using StackEntry = std::variant<double, std::string, ArrayReference>;
struct RuntimeArray { std::vector<std::size_t> lengths; std::vector<Value> values; };
struct SelectState { double value = 0; bool matched = false; };

void verifyBytecode(const std::vector<Instruction>& code) {
    const auto invalid = [](const Instruction& instruction, const char* reason) {
        throw std::runtime_error("line " + std::to_string(instruction.line) +
            ", column " + std::to_string(instruction.column) +
            ": invalid bytecode: " + reason);
    };
    const auto validTarget = [&](const Instruction& instruction, std::size_t target) {
        if (target > code.size()) invalid(instruction, "jump target is outside the program");
    };
    for (const Instruction& instruction : code) {
        if (instruction.line < 1 || instruction.column < 1)
            invalid(instruction, "source location is invalid");
        switch (instruction.op) {
        case Op::Jump:
        case Op::JumpIfFalse:
        case Op::LoopNext:
        case Op::ExitLoop:
            validTarget(instruction, instruction.target);
            break;
        case Op::ForInit:
            if (instruction.forItems.empty()) invalid(instruction, "FOR has no range items");
            validTarget(instruction, instruction.target);
            break;
        case Op::ForNext:
            validTarget(instruction, instruction.target);
            validTarget(instruction, instruction.exitTarget);
            break;
        case Op::Call:
            if (instruction.target >= code.size()) invalid(instruction, "call target is invalid");
            if (instruction.parameters.size() != instruction.argCount)
                invalid(instruction, "call argument metadata is inconsistent");
            break;
        case Op::DimArray:
        case Op::ArrayGet:
        case Op::ArraySet:
            if (instruction.rank == 0) invalid(instruction, "array rank must be positive");
            break;
        case Op::SelectMatch:
            if (instruction.selectItems.empty()) invalid(instruction, "SELECT arm is empty");
            break;
        case Op::Push:
        case Op::PushArrayRef:
        case Op::Load:
        case Op::Store:
        case Op::Add:
        case Op::Subtract:
        case Op::Multiply:
        case Op::Divide:
        case Op::IntegerDivide:
        case Op::Modulo:
        case Op::Power:
        case Op::Equal:
        case Op::NotEqual:
        case Op::Less:
        case Op::LessEqual:
        case Op::Greater:
        case Op::GreaterEqual:
        case Op::And:
        case Op::Or:
        case Op::Xor:
        case Op::Negate:
        case Op::Not:
        case Op::Print:
        case Op::PrintSeparator:
        case Op::PrintEnd:
        case Op::Return:
        case Op::FunctionEnd:
        case Op::SelectBegin:
        case Op::SelectRemainder:
        case Op::SelectEnd:
            break;
        }
    }

    std::vector<std::optional<std::size_t>> stackDepth(code.size() + 1);
    std::deque<std::size_t> pending;
    const auto merge = [&](std::size_t target, std::size_t depth,
                           const Instruction& source) {
        validTarget(source, target);
        if (stackDepth[target] && *stackDepth[target] != depth)
            throw std::runtime_error("line " + std::to_string(source.line) +
                ", column " + std::to_string(source.column) +
                ": invalid bytecode: control-flow paths join at " +
                std::to_string(target) + " with stack depths " +
                std::to_string(*stackDepth[target]) + " and " + std::to_string(depth));
        if (!stackDepth[target]) {
            stackDepth[target] = depth;
            pending.push_back(target);
        }
    };
    stackDepth[0] = 0;
    pending.push_back(0);
    for (const Instruction& instruction : code) {
        if (instruction.op == Op::Call) merge(instruction.target, 0, instruction);
    }

    while (!pending.empty()) {
        const std::size_t pc = pending.front();
        pending.pop_front();
        const std::size_t depth = *stackDepth[pc];
        if (pc == code.size()) {
            if (depth != 0)
                throw std::runtime_error("invalid bytecode: program exits with values on the stack");
            continue;
        }
        const Instruction& instruction = code[pc];
        std::size_t consumed = 0;
        std::size_t produced = 0;
        switch (instruction.op) {
        case Op::Push:
        case Op::PushArrayRef:
        case Op::Load:
        case Op::SelectRemainder:
            produced = 1;
            break;
        case Op::Store:
        case Op::Print:
        case Op::JumpIfFalse:
        case Op::SelectBegin:
            consumed = 1;
            break;
        case Op::Add:
        case Op::Subtract:
        case Op::Multiply:
        case Op::Divide:
        case Op::IntegerDivide:
        case Op::Modulo:
        case Op::Power:
        case Op::Equal:
        case Op::NotEqual:
        case Op::Less:
        case Op::LessEqual:
        case Op::Greater:
        case Op::GreaterEqual:
        case Op::And:
        case Op::Or:
        case Op::Xor:
            consumed = 2;
            produced = 1;
            break;
        case Op::Negate:
        case Op::Not:
            consumed = 1;
            produced = 1;
            break;
        case Op::ForInit:
            for (const ForItemSpec& item : instruction.forItems)
                consumed += 1 + static_cast<std::size_t>(item.hasEnd) +
                            static_cast<std::size_t>(item.hasStep);
            break;
        case Op::Call:
            consumed = instruction.argCount;
            produced = instruction.expectsValue ? 1 : 0;
            break;
        case Op::Return:
            consumed = instruction.expectsValue ? 1 : 0;
            break;
        case Op::DimArray:
            consumed = instruction.rank;
            break;
        case Op::ArrayGet:
            consumed = instruction.rank;
            produced = 1;
            break;
        case Op::ArraySet:
            consumed = instruction.rank + 1;
            break;
        case Op::SelectMatch:
            for (const SelectItemSpec& item : instruction.selectItems)
                consumed += 1 + static_cast<std::size_t>(item.hasEnd);
            produced = 1;
            break;
        case Op::Jump:
        case Op::ForNext:
        case Op::LoopNext:
        case Op::ExitLoop:
        case Op::FunctionEnd:
        case Op::PrintSeparator:
        case Op::PrintEnd:
        case Op::SelectEnd:
            break;
        }
        if (depth < consumed) invalid(instruction, "operand stack underflow");
        const std::size_t after = depth - consumed + produced;
        if (instruction.op == Op::Return || instruction.op == Op::FunctionEnd) {
            if (after != 0) invalid(instruction, "definition exits with values on the stack");
            continue;
        }
        if (instruction.op == Op::Jump || instruction.op == Op::LoopNext ||
            instruction.op == Op::ExitLoop) {
            merge(instruction.target, after, instruction);
        } else if (instruction.op == Op::JumpIfFalse || instruction.op == Op::ForInit) {
            merge(instruction.target, after, instruction);
            merge(pc + 1, after, instruction);
        } else if (instruction.op == Op::ForNext) {
            merge(instruction.target, after, instruction);
            merge(instruction.exitTarget, after, instruction);
        } else {
            merge(pc + 1, after, instruction);
        }
    }
}

double numeric(const Value& value, const Instruction& at) {
    if (const auto* number = std::get_if<double>(&value)) return *number;
    throw std::runtime_error("line " + std::to_string(at.line) + ", column " +
                             std::to_string(at.column) + ": numeric value required");
}

[[noreturn]] void runtimeError(const Instruction& at, const char* message) {
    throw std::runtime_error("line " + std::to_string(at.line) + ", column " +
                             std::to_string(at.column) + ": " + message);
}

std::size_t utf8Length(const std::string& text) {
    std::size_t count = 0;
    for (const unsigned char byte : text)
        if ((byte & 0xc0) != 0x80) ++count;
    return count;
}

void writeText(const std::string& text, std::ostream& output, std::size_t& column) {
    output << text;
    column += utf8Length(text);
}

void printValue(const Value& value, std::ostream& output, std::size_t& column) {
    if (const auto* number = std::get_if<double>(&value)) {
        if (*number == 0) {
            writeText("0", output, column);
        } else {
            char buffer[64];
            const auto converted = std::to_chars(buffer, buffer + sizeof(buffer), *number);
            if (converted.ec != std::errc{}) throw std::runtime_error("numeric formatting failed");
            writeText(std::string(buffer, converted.ptr), output, column);
        }
    } else {
        writeText(std::get<std::string>(value), output, column);
    }
}

void printSeparator(PrintSeparator separator, std::ostream& output, std::size_t& column) {
    switch (separator) {
    case PrintSeparator::None:
    case PrintSeparator::Semicolon:
        break;
    case PrintSeparator::Exclamation:
        if (column != 0) writeText(" ", output, column);
        break;
    case PrintSeparator::Comma: {
        const std::size_t spaces = 8 - (column % 8);
        writeText(std::string(spaces, ' '), output, column);
        break;
    }
    case PrintSeparator::Backslash:
        output << '\n';
        column = 0;
        break;
    }
}

} // namespace

void Interpreter::execute(const std::string& source, std::ostream& output, std::stop_token stop) {
    const auto code = detail::compileSource(source);
    verifyBytecode(code);
    std::vector<StackEntry> stack;
    std::unordered_map<std::size_t, ForState> forStates;
    std::vector<RuntimeFrame> callFrames;
    std::unordered_map<std::string, RuntimeArray> arrays;
    std::vector<SelectState> selectStates;
    auto popEntry = [&]() -> StackEntry {
        if (stack.empty()) throw std::runtime_error("internal VM stack underflow");
        StackEntry value = std::move(stack.back());
        stack.pop_back();
        return value;
    };
    auto pop = [&]() -> Value {
        StackEntry entry = popEntry();
        if (const auto* number = std::get_if<double>(&entry)) return *number;
        if (const auto* text = std::get_if<std::string>(&entry)) return *text;
        throw std::runtime_error("array reference used outside an array parameter");
    };
    auto pushValue = [&](Value value) {
        if (const auto* number = std::get_if<double>(&value)) stack.emplace_back(*number);
        else stack.emplace_back(std::get<std::string>(value));
    };
    auto resolveArrayName = [&](const Instruction& instruction) {
        std::string name = instruction.name;
        if (!callFrames.empty()) {
            const auto alias = callFrames.back().arrayAliases.find(name);
            if (alias != callFrames.back().arrayAliases.end()) name = alias->second;
        }
        return name;
    };
    auto assignLoopValue = [&](const Instruction& instruction, double value) {
        if (!std::isfinite(value)) runtimeError(instruction, "numeric overflow");
        if (!instruction.name.empty() && instruction.name.back() == '%' &&
            (std::trunc(value) != value ||
             value < static_cast<double>(std::numeric_limits<std::int32_t>::min()) ||
             value > static_cast<double>(std::numeric_limits<std::int32_t>::max())))
            runtimeError(instruction, "Int32 value out of range");
        variables_[instruction.name] = value;
    };
    auto rangeContains = [](const ForRange& range, double value) {
        if (range.single) return true;
        return range.step > 0 ? value <= range.end : value >= range.end;
    };
    auto arrayOffset = [&](const Instruction& instruction) {
        const auto found = arrays.find(resolveArrayName(instruction));
        if (found == arrays.end()) runtimeError(instruction, "array is not dimensioned");
        RuntimeArray& array = found->second;
        if (array.lengths.size() != instruction.rank)
            throw std::runtime_error("line " + std::to_string(instruction.line) +
                ", column " + std::to_string(instruction.column) +
                ": incorrect array rank (expected " + std::to_string(instruction.rank) +
                ", actual " + std::to_string(array.lengths.size()) + ")");
        std::vector<std::size_t> indices(instruction.rank);
        for (std::size_t i = instruction.rank; i-- > 0;) {
            const double index = numeric(pop(), instruction);
            if (std::trunc(index) != index || index < 0 || index > 2147483647.0)
                runtimeError(instruction, "array index must be a nonnegative integer");
            indices[i] = static_cast<std::size_t>(index);
        }
        std::size_t offset = 0;
        for (std::size_t i = 0; i < indices.size(); ++i) {
            if (indices[i] >= array.lengths[i]) runtimeError(instruction, "array index out of bounds");
            offset = offset * array.lengths[i] + indices[i];
        }
        return std::pair<RuntimeArray*, std::size_t>{&array, offset};
    };

    std::size_t pc = 0;
    while (pc < code.size()) {
        if (stop.stop_requested()) throw std::runtime_error("execution cancelled");
        const auto& instruction = code[pc++];
        switch (instruction.op) {
        case Op::Call: {
            if (callFrames.size() >= 256) runtimeError(instruction, "call stack limit exceeded");
            std::vector<StackEntry> arguments(instruction.argCount);
            for (std::size_t i = instruction.argCount; i-- > 0;)
                arguments[i] = popEntry();
            RuntimeFrame frame;
            frame.returnAddress = pc;
            frame.function = instruction.expectsValue;
            for (std::size_t i = 0; i < instruction.parameters.size(); ++i) {
                const std::string& parameter = instruction.parameters[i];
                if (const auto* array = std::get_if<ArrayReference>(&arguments[i])) {
                    std::string actualName = array->name;
                    if (!callFrames.empty()) {
                        const auto alias = callFrames.back().arrayAliases.find(actualName);
                        if (alias != callFrames.back().arrayAliases.end()) actualName = alias->second;
                    }
                    if (!arrays.contains(actualName))
                        runtimeError(instruction, "array argument is not dimensioned");
                    if ((!parameter.empty() && parameter.back() == '$') !=
                        (!actualName.empty() && actualName.back() == '$'))
                        runtimeError(instruction, "array argument type does not match parameter");
                    frame.arrayAliases[parameter] = std::move(actualName);
                } else if (const auto* number = std::get_if<double>(&arguments[i])) {
                    frame.locals[parameter] = *number;
                } else {
                    frame.locals[parameter] = std::get<std::string>(arguments[i]);
                }
            }
            for (const std::string& local : instruction.localNames) {
                if (!frame.locals.contains(local)) {
                    if (!local.empty() && local.back() == '$') frame.locals[local] = std::string{};
                    else frame.locals[local] = 0.0;
                }
            }
            callFrames.push_back(std::move(frame));
            pc = instruction.target;
            break;
        }
        case Op::Return: {
            if (callFrames.empty()) runtimeError(instruction, "RETURN outside a definition");
            RuntimeFrame& frame = callFrames.back();
            Value result = 0.0;
            if (instruction.expectsValue) {
                if (!frame.function) runtimeError(instruction, "procedure cannot return a value");
                result = pop();
            } else if (frame.function) {
                runtimeError(instruction, "function must return a value");
            }
            pc = frame.returnAddress;
            const bool returnsValue = frame.function;
            callFrames.pop_back();
            if (returnsValue) pushValue(std::move(result));
            break;
        }
        case Op::FunctionEnd:
            runtimeError(instruction, "function did not return a value");
        case Op::DimArray: {
            std::vector<std::size_t> lengths(instruction.rank);
            std::size_t elements = 1;
            for (std::size_t i = instruction.rank; i-- > 0;) {
                const double upper = numeric(pop(), instruction);
                if (std::trunc(upper) != upper || upper < 0 || upper > 2147483647.0)
                    runtimeError(instruction, "array upper bound must be a nonnegative integer");
                const std::size_t length = static_cast<std::size_t>(upper) + 1;
                if (elements > 1000000 / length)
                    runtimeError(instruction, "array allocation limit exceeded");
                elements *= length;
                lengths[i] = length;
            }
            RuntimeArray array;
            array.lengths = std::move(lengths);
            if (!instruction.name.empty() && instruction.name.back() == '$')
                array.values.resize(elements, std::string{});
            else
                array.values.resize(elements, 0.0);
            arrays[instruction.name] = std::move(array);
            break;
        }
        case Op::ArrayGet: {
            auto [array, offset] = arrayOffset(instruction);
            pushValue(array->values[offset]);
            break;
        }
        case Op::ArraySet: {
            Value value = pop();
            auto [array, offset] = arrayOffset(instruction);
            const std::string actualName = resolveArrayName(instruction);
            if (!actualName.empty() && actualName.back() == '$') {
                if (!std::holds_alternative<std::string>(value))
                    runtimeError(instruction, "string value required for array element");
            } else {
                const double number = numeric(value, instruction);
                if (!actualName.empty() && actualName.back() == '%' &&
                    (std::trunc(number) != number ||
                     number < static_cast<double>(std::numeric_limits<std::int32_t>::min()) ||
                     number > static_cast<double>(std::numeric_limits<std::int32_t>::max())))
                    runtimeError(instruction, "Int32 array element out of range");
            }
            array->values[offset] = std::move(value);
            break;
        }
        case Op::SelectBegin: {
            const double value = numeric(pop(), instruction);
            selectStates.push_back({value, false});
            break;
        }
        case Op::SelectMatch: {
            if (selectStates.empty()) runtimeError(instruction, "inactive SELECT block");
            bool matched = false;
            for (std::size_t i = instruction.selectItems.size(); i-- > 0;) {
                const SelectItemSpec& item = instruction.selectItems[i];
                double end = 0;
                if (item.hasEnd) end = numeric(pop(), instruction);
                const double start = numeric(pop(), instruction);
                const double selected = selectStates.back().value;
                const bool thisMatch = item.hasEnd ? (selected >= start && selected <= end)
                                                   : (selected == start);
                matched = matched || thisMatch;
            }
            if (matched) selectStates.back().matched = true;
            stack.emplace_back(matched ? 1.0 : 0.0);
            break;
        }
        case Op::SelectRemainder: {
            if (selectStates.empty()) runtimeError(instruction, "inactive SELECT block");
            const bool matched = !selectStates.back().matched;
            if (matched) selectStates.back().matched = true;
            stack.emplace_back(matched ? 1.0 : 0.0);
            break;
        }
        case Op::SelectEnd:
            if (selectStates.empty()) runtimeError(instruction, "inactive SELECT block");
            selectStates.pop_back();
            break;
        case Op::ForInit: {
            ForState state;
            state.ranges.resize(instruction.forItems.size());
            for (std::size_t i = instruction.forItems.size(); i-- > 0;) {
                const ForItemSpec& spec = instruction.forItems[i];
                ForRange& range = state.ranges[i];
                if (spec.hasStep) range.step = numeric(pop(), instruction);
                if (spec.hasEnd) range.end = numeric(pop(), instruction);
                range.start = numeric(pop(), instruction);
                range.single = !spec.hasEnd;
                if (spec.hasEnd && range.step == 0)
                    runtimeError(instruction, "FOR step cannot be zero");
            }
            while (state.item < state.ranges.size() &&
                   !rangeContains(state.ranges[state.item], state.ranges[state.item].start))
                ++state.item;
            if (state.item == state.ranges.size()) {
                pc = instruction.target;
                break;
            }
            const double initial = state.ranges[state.item].start;
            assignLoopValue(instruction, initial);
            forStates[instruction.loopId] = std::move(state);
            break;
        }
        case Op::ForNext: {
            auto found = forStates.find(instruction.loopId);
            if (found == forStates.end()) runtimeError(instruction, "inactive FOR loop");
            ForState& state = found->second;
            const auto variable = variables_.find(instruction.name);
            if (variable == variables_.end()) runtimeError(instruction, "missing FOR variable");
            const double current = numeric(variable->second, instruction);
            const ForRange& range = state.ranges[state.item];
            const double next = range.single ? current : current + range.step;
            if (!range.single && !std::isfinite(next))
                runtimeError(instruction, "numeric overflow");
            if (!range.single && rangeContains(range, next)) {
                assignLoopValue(instruction, next);
                for (auto loop = forStates.begin(); loop != forStates.end();) {
                    if (loop->first > instruction.loopId) loop = forStates.erase(loop);
                    else ++loop;
                }
                pc = instruction.target;
                break;
            }
            ++state.item;
            while (state.item < state.ranges.size() &&
                   !rangeContains(state.ranges[state.item], state.ranges[state.item].start))
                ++state.item;
            if (state.item == state.ranges.size()) {
                if (!instruction.explicitNext) {
                    forStates.erase(found);
                    pc = instruction.exitTarget;
                } else {
                    --state.item;
                }
                break;
            }
            assignLoopValue(instruction, state.ranges[state.item].start);
            for (auto loop = forStates.begin(); loop != forStates.end();) {
                if (loop->first > instruction.loopId) loop = forStates.erase(loop);
                else ++loop;
            }
            pc = instruction.target;
            break;
        }
        case Op::LoopNext:
            for (auto loop = forStates.begin(); loop != forStates.end();) {
                if (loop->first > instruction.loopId) loop = forStates.erase(loop);
                else ++loop;
            }
            pc = instruction.target;
            break;
        case Op::ExitLoop:
            for (auto loop = forStates.begin(); loop != forStates.end();) {
                if (loop->first >= instruction.loopId) loop = forStates.erase(loop);
                else ++loop;
            }
            pc = instruction.target;
            break;
        case Op::JumpIfFalse:
            if (numeric(pop(), instruction) == 0) pc = instruction.target;
            break;
        case Op::Jump:
            pc = instruction.target;
            break;
        case Op::Push: pushValue(instruction.value); break;
        case Op::PushArrayRef: stack.emplace_back(ArrayReference{instruction.name}); break;
        case Op::Load: {
            if (!callFrames.empty()) {
                const auto local = callFrames.back().locals.find(instruction.name);
                if (local != callFrames.back().locals.end()) {
                    pushValue(local->second);
                    break;
                }
            }
            const auto found = variables_.find(instruction.name);
            if (found != variables_.end()) {
                pushValue(found->second);
            } else if (!instruction.name.empty() && instruction.name.back() == '$') {
                stack.emplace_back(std::string{});
            } else {
                stack.emplace_back(0.0);
            }
            break;
        }
        case Op::Store: {
            Value value = pop();
            if (!instruction.name.empty() && instruction.name.back() == '$') {
                if (!std::holds_alternative<std::string>(value))
                    throw std::runtime_error("line " + std::to_string(instruction.line) +
                                             ", column " + std::to_string(instruction.column) +
                                             ": string value required");
            } else {
                const double number = numeric(value, instruction);
                if (!instruction.name.empty() && instruction.name.back() == '%') {
                    if (std::trunc(number) != number ||
                        number < static_cast<double>(std::numeric_limits<std::int32_t>::min()) ||
                        number > static_cast<double>(std::numeric_limits<std::int32_t>::max()))
                        throw std::runtime_error("line " + std::to_string(instruction.line) +
                                                 ", column " + std::to_string(instruction.column) +
                                                 ": Int32 value out of range");
                }
            }
            if (!callFrames.empty() && callFrames.back().locals.contains(instruction.name))
                callFrames.back().locals[instruction.name] = std::move(value);
            else
                variables_[instruction.name] = std::move(value);
            break;
        }
        case Op::Negate: {
            const double value = -numeric(pop(), instruction);
            if (!std::isfinite(value)) runtimeError(instruction, "numeric overflow");
            stack.push_back(value);
            break;
        }
        case Op::Not: {
            const double value = numeric(pop(), instruction);
            stack.emplace_back(value == 0 ? 1.0 : 0.0);
            break;
        }
        case Op::Add: case Op::Subtract: case Op::Multiply: case Op::Divide:
        case Op::IntegerDivide: case Op::Modulo: case Op::Power: {
            Value right = pop();
            Value left = pop();
            if (instruction.op == Op::Add &&
                std::holds_alternative<std::string>(left) &&
                std::holds_alternative<std::string>(right)) {
                stack.push_back(std::get<std::string>(left) + std::get<std::string>(right));
                break;
            }
            const double a = numeric(left, instruction);
            const double b = numeric(right, instruction);
            if ((instruction.op == Op::Divide || instruction.op == Op::IntegerDivide ||
                 instruction.op == Op::Modulo) && b == 0)
                runtimeError(instruction, "division by zero");
            if (instruction.op == Op::Power && a == 0 && b < 0)
                runtimeError(instruction, "division by zero");
            if (instruction.op == Op::Power && a < 0 && std::trunc(b) != b)
                runtimeError(instruction, "invalid exponent");
            double result = 0;
            if (instruction.op == Op::Add) result = a + b;
            else if (instruction.op == Op::Subtract) result = a - b;
            else if (instruction.op == Op::Multiply) result = a * b;
            else if (instruction.op == Op::Divide) result = a / b;
            else if (instruction.op == Op::Power) result = std::pow(a, b);
            else {
                if (std::trunc(a) != a || std::trunc(b) != b ||
                    a < static_cast<double>(std::numeric_limits<std::int32_t>::min()) ||
                    a > static_cast<double>(std::numeric_limits<std::int32_t>::max()) ||
                    b < static_cast<double>(std::numeric_limits<std::int32_t>::min()) ||
                    b > static_cast<double>(std::numeric_limits<std::int32_t>::max()))
                    runtimeError(instruction, "Int32 operands required");
                const auto lhs = static_cast<std::int64_t>(a);
                const auto rhs = static_cast<std::int64_t>(b);
                std::int64_t quotient = lhs / rhs;
                std::int64_t remainder = lhs % rhs;
                if (remainder != 0 && ((lhs < 0) != (rhs < 0))) {
                    --quotient;
                    remainder += rhs;
                }
                if (instruction.op == Op::IntegerDivide) {
                    if (quotient < std::numeric_limits<std::int32_t>::min() ||
                        quotient > std::numeric_limits<std::int32_t>::max())
                        runtimeError(instruction, "Int32 result out of range");
                    result = static_cast<double>(quotient);
                } else {
                    result = static_cast<double>(remainder);
                }
            }
            if (!std::isfinite(result)) runtimeError(instruction, "numeric overflow");
            stack.push_back(result);
            break;
        }
        case Op::Equal: case Op::NotEqual: case Op::Less: case Op::LessEqual:
        case Op::Greater: case Op::GreaterEqual: {
            const Value right = pop();
            const Value left = pop();
            bool result = false;
            const bool strings = std::holds_alternative<std::string>(left) &&
                                 std::holds_alternative<std::string>(right);
            if (instruction.op == Op::Equal || instruction.op == Op::NotEqual) {
                if (strings) result = std::get<std::string>(left) == std::get<std::string>(right);
                else if (std::holds_alternative<double>(left) && std::holds_alternative<double>(right))
                    result = std::get<double>(left) == std::get<double>(right);
                else runtimeError(instruction, "values must have matching types");
                if (instruction.op == Op::NotEqual) result = !result;
            } else {
                if (strings) runtimeError(instruction, "relational comparison requires numbers");
                const double a = numeric(left, instruction);
                const double b = numeric(right, instruction);
                switch (instruction.op) {
                case Op::Less: result = a < b; break;
                case Op::LessEqual: result = a <= b; break;
                case Op::Greater: result = a > b; break;
                case Op::GreaterEqual: result = a >= b; break;
                default: break;
                }
            }
            stack.emplace_back(result ? 1.0 : 0.0);
            break;
        }
        case Op::And: case Op::Or: case Op::Xor: {
            const double right = numeric(pop(), instruction);
            const double left = numeric(pop(), instruction);
            const bool a = left != 0;
            const bool b = right != 0;
            const bool result = instruction.op == Op::And ? (a && b) :
                                instruction.op == Op::Or ? (a || b) : (a != b);
            stack.emplace_back(result ? 1.0 : 0.0);
            break;
        }
        case Op::Print:
            printSeparator(instruction.separator, output, output_column_);
            printValue(pop(), output, output_column_);
            break;
        case Op::PrintSeparator:
            printSeparator(instruction.separator, output, output_column_);
            break;
        case Op::PrintEnd:
            output << '\n';
            output_column_ = 0;
            break;
        }
    }
}


} // namespace ql32
