#include "ql32/interpreter.hpp"
#include "../language/bytecode.hpp"
#include "execution_state.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <deque>
#include <limits>
#include <new>
#include <optional>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
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
using detail::ForRange;
using detail::ForState;
using detail::RuntimeFrame;
using detail::ArrayReference;
using detail::StackEntry;
using detail::RuntimeArray;
using detail::SelectState;
using detail::DialogBuilder;
using detail::MenuBuilder;

bool mayContainInclude(std::string_view source) {
    constexpr std::string_view directive = "INCLUDE";
    for (std::size_t start = 0; start + directive.size() <= source.size(); ++start) {
        std::size_t offset = 0;
        while (offset < directive.size()) {
            const auto character = static_cast<unsigned char>(source[start + offset]);
            if (static_cast<char>(std::toupper(character)) != directive[offset]) break;
            ++offset;
        }
        if (offset == directive.size()) return true;
    }
    return false;
}

void verifyBytecode(const detail::InstructionSequence& code) {
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
            if (!instruction.builtin && instruction.target >= code.size())
                invalid(instruction, "call target is invalid");
            if (!instruction.builtin && instruction.parameters.size() != instruction.argCount)
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
        case Op::StringSlice:
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
        case Op::PrintChannel:
        case Op::PrintSeparator:
        case Op::PrintEnd:
        case Op::PrintBegin:
        case Op::ChannelDefault:
        case Op::ChannelRoute:
        case Op::ChannelSeek:
        case Op::ChannelRead:
        case Op::ChannelWrite:
        case Op::Input:
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
        if (instruction.op == Op::Call && !instruction.builtin)
            merge(instruction.target, 0, instruction);
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
        case Op::PrintChannel:
            consumed = 1;
            break;
        case Op::ChannelDefault:
            consumed = 1;
            break;
        case Op::ChannelRoute:
            consumed = 2;
            break;
        case Op::ChannelSeek:
        case Op::ChannelRead:
            consumed = 2;
            break;
        case Op::ChannelWrite:
            consumed = 3;
            break;
        case Op::Input:
            consumed = static_cast<std::size_t>(instruction.expectsValue) +
                       static_cast<std::size_t>(instruction.channelArgument) +
                       static_cast<std::size_t>(instruction.clearTarget);
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
        case Op::StringSlice:
            consumed = 1 + static_cast<std::size_t>(instruction.sliceHasStart) +
                       static_cast<std::size_t>(instruction.sliceHasEnd);
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
        case Op::PrintBegin:
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
    const std::string detail = "line " + std::to_string(at.line) + ", column " +
                               std::to_string(at.column) + ": " + message;
    throw runtime::Error(runtime::errorCodeForMessage(detail), detail);
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

std::string formatNumber(double number) {
    char buffer[400];
    std::string fixed;
    for (int precision = 0; precision <= 340; ++precision) {
        const int length = std::snprintf(buffer, sizeof(buffer), "%.*f", precision, number);
        if (length <= 0 || static_cast<std::size_t>(length) >= sizeof(buffer)) break;
        char* parsedEnd = nullptr;
        if (std::strtod(buffer, &parsedEnd) == number && parsedEnd == buffer + length) {
            fixed.assign(buffer, static_cast<std::size_t>(length));
            break;
        }
    }

    std::string scientific;
    for (int precision = 1; precision <= std::numeric_limits<double>::max_digits10;
         ++precision) {
        const int length = std::snprintf(buffer, sizeof(buffer), "%.*g", precision, number);
        if (length <= 0 || static_cast<std::size_t>(length) >= sizeof(buffer)) break;
        char* parsedEnd = nullptr;
        if (std::strtod(buffer, &parsedEnd) == number && parsedEnd == buffer + length) {
            scientific.assign(buffer, static_cast<std::size_t>(length));
            break;
        }
    }

    if (fixed.empty() && scientific.empty())
        throw std::runtime_error("numeric formatting failed");
    if (fixed.empty()) return scientific;
    if (scientific.empty() || fixed.size() <= scientific.size()) return fixed;
    return scientific;
}

void printValue(const Value& value, std::ostream& output, std::size_t& column) {
    if (const auto* number = std::get_if<double>(&value)) {
        if (*number == 0) {
            writeText("0", output, column);
        } else {
            writeText(formatNumber(*number), output, column);
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

const char* opcodeName(Op op) {
    switch (op) {
    case Op::Push: return "PUSH"; case Op::Load: return "LOAD"; case Op::Store: return "STORE";
    case Op::Add: return "ADD"; case Op::Subtract: return "SUBTRACT";
    case Op::Multiply: return "MULTIPLY"; case Op::Divide: return "DIVIDE";
    case Op::IntegerDivide: return "INTEGER_DIVIDE"; case Op::Modulo: return "MODULO";
    case Op::Power: return "POWER"; case Op::Equal: return "EQUAL";
    case Op::NotEqual: return "NOT_EQUAL"; case Op::Less: return "LESS";
    case Op::LessEqual: return "LESS_EQUAL"; case Op::Greater: return "GREATER";
    case Op::GreaterEqual: return "GREATER_EQUAL"; case Op::And: return "AND";
    case Op::Or: return "OR"; case Op::Xor: return "XOR"; case Op::Negate: return "NEGATE";
    case Op::Not: return "NOT"; case Op::PrintBegin: return "PRINT_BEGIN";
    case Op::PrintChannel: return "PRINT_CHANNEL"; case Op::Print: return "PRINT";
    case Op::PrintSeparator: return "PRINT_SEPARATOR"; case Op::PrintEnd: return "PRINT_END";
    case Op::ChannelDefault: return "CHANNEL_DEFAULT"; case Op::ChannelRoute: return "CHANNEL_ROUTE";
    case Op::ChannelSeek: return "CHANNEL_SEEK"; case Op::ChannelRead: return "CHANNEL_READ";
    case Op::ChannelWrite: return "CHANNEL_WRITE"; case Op::Input: return "INPUT";
    case Op::JumpIfFalse: return "JUMP_IF_FALSE"; case Op::Jump: return "JUMP";
    case Op::ForInit: return "FOR_INIT"; case Op::ForNext: return "FOR_NEXT";
    case Op::LoopNext: return "LOOP_NEXT"; case Op::ExitLoop: return "EXIT_LOOP";
    case Op::Call: return "CALL"; case Op::Return: return "RETURN";
    case Op::FunctionEnd: return "FUNCTION_END"; case Op::DimArray: return "DIM_ARRAY";
    case Op::ArrayGet: return "ARRAY_GET"; case Op::ArraySet: return "ARRAY_SET";
    case Op::StringSlice: return "STRING_SLICE"; case Op::PushArrayRef: return "PUSH_ARRAY_REF";
    case Op::SelectBegin: return "SELECT_BEGIN"; case Op::SelectMatch: return "SELECT_MATCH";
    case Op::SelectRemainder: return "SELECT_REMAINDER"; case Op::SelectEnd: return "SELECT_END";
    }
    return "UNKNOWN";
}

} // namespace

void Interpreter::execute(const std::string& source, std::ostream& output,
                          runtime::CancellationToken stop) {
    const auto status = executeResumable(source, output, stop);
    if (status == ExecutionStatus::Suspended || status == ExecutionStatus::WaitingChannel)
        throw std::runtime_error("execution suspended; use executeResumable() and resume()");
}

ExecutionStatus Interpreter::executeResumable(const std::string& source, std::ostream& output,
                                               runtime::CancellationToken stop) {
    return executeSlice(source, output, std::numeric_limits<std::size_t>::max(), stop);
}

ExecutionStatus Interpreter::executeSlice(const std::string& source, std::ostream& output,
                                           std::size_t instructionBudget,
                                           runtime::CancellationToken stop) {
    if (execution_) throw std::runtime_error("interpreter already has a suspended execution");
    if (instructionBudget == 0)
        throw std::invalid_argument("instruction budget must be positive");
    bool preparingProgram = true;
    try {
        execution_ = std::make_unique<detail::VMExecutionState>();
        if (mayContainInclude(source)) {
            const std::string expanded = detail::expandIncludes(source, storage_.get());
            execution_->code = detail::compileSource(expanded);
        } else {
            // Compile ordinary programs directly from their source. Avoid
            // retaining an expanded duplicate while tokens and bytecode grow.
            execution_->code = detail::compileSource(source);
        }
        verifyBytecode(execution_->code);
        execution_->debuggerAttached = debuggerAttached_;
        execution_->debugPaused = debugPauseRequested_;
        execution_->diagnosticsEnabled = diagnosticsEnabled_;
        execution_->traceEnabled = traceEnabled_;
        const bool numbered = std::any_of(execution_->code.begin(), execution_->code.end(),
            [](const Instruction& instruction) {
                return instruction.statementStart && instruction.basicLine != 0;
            });
        for (const auto& [line, enabled] : pendingBreakpoints_) {
            for (std::size_t address = 0; address < execution_->code.size(); ++address) {
                const Instruction& instruction = execution_->code[address];
                if (!instruction.statementStart ||
                    (numbered ? instruction.basicLine != line : instruction.line != line)) continue;
                if (enabled) execution_->breakpoints.insert(address);
                else execution_->breakpoints.erase(address);
                break;
            }
        }
        preparingProgram = false;
        if (execution_->code.cpuFrequencyMHz != 0 && cpuFrequency_ &&
            !cpuFrequency_->setFrequencyMHz(execution_->code.cpuFrequencyMHz))
            throw runtime::Error(runtime::ErrorCode::ServiceUnavailable,
                                 "platform could not set requested CPU frequency");
        const auto status = runExecution(output, stop, instructionBudget);
        if (status == ExecutionStatus::Completed) execution_.reset();
        return status;
    } catch (const std::bad_alloc&) {
        if (ui_) ui_->cancelSession(session_id_);
        std::string detail = preparingProgram ? "out of memory preparing BASIC program" :
                                                 "out of memory executing BASIC program";
        if (!preparingProgram && execution_ && execution_->pc != 0 &&
            execution_->pc <= execution_->code.size()) {
            const Instruction& current = execution_->code[execution_->pc - 1];
            detail += " at line " + std::to_string(current.line);
        }
        execution_.reset();
        setErrorContext(runtime::ErrorCode::OutOfMemory, detail);
        throw std::runtime_error(detail);
    } catch (const runtime::Error& error) {
        if (ui_) ui_->cancelSession(session_id_);
        execution_.reset();
        setErrorContext(error.code(), error.detail());
        throw;
    } catch (const runtime::ServiceError& error) {
        if (ui_) ui_->cancelSession(session_id_);
        execution_.reset();
        setErrorContext(error.languageCode(), error.detail());
        throw;
    } catch (const std::exception& error) {
        if (ui_) ui_->cancelSession(session_id_);
        execution_.reset();
        setErrorContext(runtime::errorCodeFor(error), runtime::extendedErrorText(error));
        throw;
    } catch (...) {
        if (ui_) ui_->cancelSession(session_id_);
        execution_.reset();
        setErrorContext(runtime::ErrorCode::GeneralFailure, "unknown BASIC execution failure");
        throw;
    }
}

ExecutionStatus Interpreter::resume(std::ostream& output, runtime::CancellationToken stop) {
    return resumeSlice(output, std::numeric_limits<std::size_t>::max(), stop);
}

ExecutionStatus Interpreter::resumeSlice(std::ostream& output,
                                          std::size_t instructionBudget,
                                          runtime::CancellationToken stop) {
    if (hasPendingInteractivePager()) return resumeInteractivePager(output);
    if (!execution_) throw std::runtime_error("interpreter has no suspended execution");
    if (instructionBudget == 0)
        throw std::invalid_argument("instruction budget must be positive");
    try {
        const auto status = runExecution(output, stop, instructionBudget);
        if (status == ExecutionStatus::Completed) execution_.reset();
        return status;
    } catch (const runtime::Error& error) {
        if (ui_) ui_->cancelSession(session_id_);
        execution_.reset();
        setErrorContext(error.code(), error.detail());
        throw;
    } catch (const runtime::ServiceError& error) {
        if (ui_) ui_->cancelSession(session_id_);
        execution_.reset();
        setErrorContext(error.languageCode(), error.detail());
        throw;
    } catch (const std::bad_alloc&) {
        if (ui_) ui_->cancelSession(session_id_);
        execution_.reset();
        setErrorContext(runtime::ErrorCode::OutOfMemory, "out of memory resuming BASIC program");
        throw;
    } catch (const std::exception& error) {
        if (ui_) ui_->cancelSession(session_id_);
        execution_.reset();
        setErrorContext(runtime::errorCodeFor(error), runtime::extendedErrorText(error));
        throw;
    } catch (...) {
        if (ui_) ui_->cancelSession(session_id_);
        execution_.reset();
        setErrorContext(runtime::ErrorCode::GeneralFailure, "unknown BASIC execution failure");
        throw;
    }
}

ExecutionStatus Interpreter::runExecution(std::ostream& output,
                                           runtime::CancellationToken stop,
                                           std::size_t instructionBudget) {
    auto& state = *execution_;
    if (state.diagnosticsEnabled) {
        state.stackHighWater = std::max(state.stackHighWater, state.stack.size());
        state.frameHighWater = std::max(state.frameHighWater, state.callFrames.size());
    }
    auto& code = state.code;
    auto& stack = state.stack;
    auto& forStates = state.forStates;
    auto& callFrames = state.callFrames;
    auto& arrays = state.arrays;
    auto& selectStates = state.selectStates;
    auto& pc = state.pc;
    if (state.debuggerAttached && state.debugPaused)
        return ExecutionStatus::DebugPaused;
    const bool singlePrecision = code.numericPrecision == detail::NumericPrecision::Single;
    const Instruction* activeInstruction = pc == 0 ? nullptr : &code[pc - 1];
    auto quantizeNumeric = [&](double value, const Instruction* at = nullptr) {
        if (!singlePrecision) return value;
        const float rounded = static_cast<float>(value);
        if (std::isfinite(value) && !std::isfinite(rounded)) {
            if (at) runtimeError(*at, "single-precision numeric overflow");
            throw runtime::Error(runtime::ErrorCode::NumericOverflow,
                                 "single-precision numeric overflow");
        }
        return static_cast<double>(rounded);
    };
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
    auto pushValue = [&](Value value, bool preserveInteger = false) {
        if (const auto* number = std::get_if<double>(&value))
            stack.emplace_back(preserveInteger ? *number :
                               quantizeNumeric(*number, activeInstruction));
        else stack.emplace_back(std::get<std::string>(value));
    };
    auto storeInputValue = [&](const std::string& name, const std::string& text,
                               const Instruction& at) {
        Value value;
        if (!name.empty() && name.back() == '$') {
            value = text;
        } else {
            char* end = nullptr;
            const double number = std::strtod(text.c_str(), &end);
            if (text.empty() || end != text.c_str() + text.size() || !std::isfinite(number))
                runtimeError(at, "numeric input required");
            if (!name.empty() && name.back() == '%' &&
                (std::trunc(number) != number ||
                 number < static_cast<double>(std::numeric_limits<std::int32_t>::min()) ||
                 number > static_cast<double>(std::numeric_limits<std::int32_t>::max())))
                runtimeError(at, "Int32 input out of range");
            value = name.empty() || name.back() != '%' ?
                quantizeNumeric(number, &at) : number;
        }
        if (!callFrames.empty() && callFrames.back().locals.count(name))
            callFrames.back().locals[name] = std::move(value);
        else variables_[name] = std::move(value);
    };
    auto writePrintText = [&](const std::string& text, const Instruction& at) {
        if (state.printChannel == 0) {
            output << text;
            output_column_ += utf8Length(text);
        } else if (state.printChannel == 1) {
            if (graphics_) graphics_->writeText(session_id_, text);
            else {
                output << text;
                output_column_ += utf8Length(text);
            }
            state.printChannelColumn += utf8Length(text);
        } else {
            if (!channels_) runtimeError(at, "channel output is unavailable");
            channels_->write(session_id_, state.printChannel, text);
        }
    };
    auto printColumn = [&]() -> std::size_t& {
        return state.printChannel == 0 ? output_column_ : state.printChannelColumn;
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
        if (instruction.name.empty() || instruction.name.back() != '%')
            value = quantizeNumeric(value, &instruction);
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
        std::size_t offset = 0;
        std::size_t stride = 1;
        for (std::size_t i = instruction.rank; i-- > 0;) {
            const double index = numeric(pop(), instruction);
            if (std::trunc(index) != index || index < 0 || index > 2147483647.0)
                runtimeError(instruction, "array index must be a nonnegative integer");
            const auto dimensionIndex = static_cast<std::size_t>(index);
            if (dimensionIndex >= array.lengths[i])
                runtimeError(instruction, "array index out of bounds");
            offset += dimensionIndex * stride;
            stride *= array.lengths[i];
        }
        return std::pair<RuntimeArray*, std::size_t>{&array, offset};
    };

    if (state.pendingUiRequest) {
        if (!ui_) throw std::runtime_error("UI service unavailable");
        const auto result = ui_->poll(*state.pendingUiRequest);
        if (!result) return ExecutionStatus::Suspended;
        if (state.pendingUiKind == ui::RequestKind::Confirm)
            stack.emplace_back(result->kind == ui::ResultKind::Confirmed ? 1.0 : 0.0);
        else if (state.pendingUiKind == ui::RequestKind::Dialog) {
            auto found = uiSession_->dialogs.find(state.pendingUiBuilderHandle);
            if (found == uiSession_->dialogs.end())
                throw std::runtime_error("pending dialog builder no longer exists");
            DialogBuilder& dialog = found->second;
            dialog.hasResults = result->kind == ui::ResultKind::Submitted;
            if (dialog.hasResults) {
                dialog.results = std::move(result->fields);
                for (std::size_t i = 0; i < dialog.fields.size(); ++i)
                    dialog.fields[i].initialValue = dialog.results[i];
            }
            stack.emplace_back(dialog.hasResults ? 1.0 : 0.0);
        } else if (state.pendingUiKind == ui::RequestKind::Menu) {
            auto found = uiSession_->menus.find(state.pendingUiBuilderHandle);
            if (found == uiSession_->menus.end())
                throw std::runtime_error("pending menu builder no longer exists");
            MenuBuilder& menu = found->second;
            menu.hasResult = result->kind == ui::ResultKind::Selected;
            menu.selectedCommand = menu.hasResult ? result->commandId : 0;
            pushValue(static_cast<double>(menu.selectedCommand));
        } else if (state.pendingUiKind == ui::RequestKind::Input) {
            if (result->kind != ui::ResultKind::Submitted || result->fields.empty() ||
                !std::holds_alternative<std::string>(result->fields.front()))
                throw std::runtime_error("input cancelled");
            storeInputValue(state.pendingInputVariable,
                            std::get<std::string>(result->fields.front()),
                            code.at(state.pc == 0 ? 0 : state.pc - 1));
            state.pendingInputVariable.clear();
        }
        state.pendingUiRequest.reset();
        state.pendingUiBuilderHandle = 0;
    }
    std::size_t instructionsExecuted = 0;
    while (pc < code.size()) {
        if (stop.stopRequested()) throw std::runtime_error("execution cancelled");
        if (instructionBudget != 0 && instructionsExecuted >= instructionBudget)
            return ExecutionStatus::Yielded;
        const Instruction& nextInstruction = code[pc];
        if (state.debuggerAttached && nextInstruction.statementStart) {
            if (state.skipBreakpointAddress == pc) {
                state.skipBreakpointAddress = static_cast<std::size_t>(-1);
            } else if (state.debugMode != runtime::DebugResumeMode::Continue &&
                       state.stepStarted &&
                       (state.debugMode == runtime::DebugResumeMode::StepInto ||
                        callFrames.size() <= state.stepCallDepth)) {
                state.debugPaused = true;
                return ExecutionStatus::DebugPaused;
            } else if (state.debugMode == runtime::DebugResumeMode::Continue &&
                       state.breakpoints.count(pc) != 0) {
                state.debugPaused = true;
                return ExecutionStatus::DebugPaused;
            }
        }
        const auto& instruction = code[pc++];
        activeInstruction = &instruction;
        ++instructionsExecuted;
        if (state.diagnosticsEnabled) ++state.bytecodes;
        if (state.traceEnabled) {
            state.trace.push_back(std::to_string(instruction.line) + ":" +
                std::to_string(instruction.column) + " " + opcodeName(instruction.op) +
                (instruction.name.empty() ? "" : " " + instruction.name));
            if (state.trace.size() > 32) state.trace.erase(state.trace.begin());
        }
        if (state.debugMode != runtime::DebugResumeMode::Continue &&
            !state.stepStarted) state.stepStarted = true;
        switch (instruction.op) {
        case Op::Call: {
            if (state.diagnosticsEnabled && !instruction.builtin) ++state.procedureCalls;
            std::vector<StackEntry> arguments(instruction.argCount);
            for (std::size_t i = instruction.argCount; i-- > 0;)
                arguments[i] = popEntry();
            std::optional<runtime::ChannelId> selectedChannel;
            if (instruction.channelArgument) {
                const auto* number = arguments.empty() ? nullptr :
                    std::get_if<double>(&arguments.front());
                if (!number || !std::isfinite(*number) || std::trunc(*number) != *number ||
                    *number < 0 || *number > static_cast<double>(
                        std::numeric_limits<std::uint64_t>::max()))
                    runtimeError(instruction, "channel handle must be a nonnegative integer");
                selectedChannel = resolveChannel(static_cast<std::uint64_t>(*number));
                arguments.erase(arguments.begin());
            }
            if (instruction.builtin) {
                if (instruction.name == "TASK") {
                    if (!storage_)
                        throw runtime::ServiceError(runtime::ServiceErrorCode::Unavailable,
                                                    "storage service unavailable");
                    if (!taskControl_)
                        throw runtime::ServiceError(runtime::ServiceErrorCode::Unavailable,
                                                    "session supervisor unavailable");
                    const auto* path = std::get_if<std::string>(&arguments[0]);
                    if (!path) runtimeError(instruction, "string path required");
                    std::uint32_t flags = runtime::TaskStartNone;
                    if (arguments.size() == 2) {
                        const auto* numericFlags = std::get_if<double>(&arguments[1]);
                        if (!numericFlags || !std::isfinite(*numericFlags) ||
                            std::trunc(*numericFlags) != *numericFlags ||
                            *numericFlags < 0 || *numericFlags > 1)
                            runtimeError(instruction, "TASK flags must be a nonnegative bit field");
                        flags = static_cast<std::uint32_t>(*numericFlags);
                    }
                    const std::string source = storage_->readText(*path);
                    const runtime::TaskId id = taskControl_->startTask(
                        source, output, task_id_, flags);
                    setErrorContext(runtime::ErrorCode::None);
                    if (instruction.expectsValue) {
                        pushValue(static_cast<double>(id));
                    } else {
                        const runtime::ChannelId previousChannel = state.printChannel;
                        state.printChannel = resolveChannel(0);
                        writePrintText("Started task " + std::to_string(id) + "\n",
                                       instruction);
                        if (state.printChannel == 0) output_column_ = 0;
                        else if (state.printChannel == 1) state.printChannelColumn = 0;
                        state.printChannel = previousChannel;
                    }
                    if ((flags & runtime::TaskStartWait) != 0)
                        return ExecutionStatus::WaitingTask;
                    break;
                }
                if (instruction.name == "TASKRESULT") {
                    if (!taskControl_)
                        throw runtime::ServiceError(runtime::ServiceErrorCode::Unavailable,
                                                    "task supervisor unavailable");
                    const auto* numericId = std::get_if<double>(&arguments[0]);
                    const auto* numericField = std::get_if<double>(&arguments[1]);
                    if (!numericId || !std::isfinite(*numericId) ||
                        std::trunc(*numericId) != *numericId || *numericId < 1 ||
                        *numericId >= std::ldexp(1.0, 64))
                        runtimeError(instruction, "TASKRESULT requires a positive task ID");
                    if (!numericField || !std::isfinite(*numericField) ||
                        std::trunc(*numericField) != *numericField ||
                        *numericField < 1 || *numericField > 7)
                        runtimeError(instruction, "invalid TASKRESULT selector");
                    const auto tasks = taskControl_->listTasks();
                    const auto found = std::find_if(tasks.begin(), tasks.end(),
                        [&](const runtime::ManagedTaskInfo& task) {
                            return task.id == static_cast<runtime::TaskId>(*numericId);
                        });
                    if (found == tasks.end())
                        throw runtime::Error(runtime::ErrorCode::InvalidTaskId,
                                             "no task exists with that ID");
                    setErrorContext(found->errorCode, found->errorDetail);
                    switch (static_cast<runtime::TaskResultField>(*numericField)) {
                    case runtime::TaskResultField::Status:
                        pushValue(std::string(found->paused ? "PAUSED" :
                            runtime::taskStateName(found->state)));
                        break;
                    case runtime::TaskResultField::Channels:
                        pushValue(static_cast<double>(found->openChannels));
                        break;
                    case runtime::TaskResultField::ErrorCode:
                        pushValue(static_cast<double>(found->errorCode));
                        break;
                    case runtime::TaskResultField::ErrorDetail:
                        pushValue(found->errorDetail);
                        break;
                    case runtime::TaskResultField::Kind:
                        pushValue(std::string(found->supervisor ? "SUPERVISOR" :
                            (found->interactive ? "WORKER" : "PROGRAM")));
                        break;
                    case runtime::TaskResultField::Priority:
                        pushValue(static_cast<double>(found->priority));
                        break;
                    case runtime::TaskResultField::ResumePriority:
                        pushValue(static_cast<double>(found->resumePriority));
                        break;
                    }
                    break;
                }
                if (instruction.name == "ERR") {
                    pushValue(static_cast<double>(errorCode_));
                    break;
                }
                if (instruction.name == "ERR$") {
                    const auto* numericCode = std::get_if<double>(&arguments[0]);
                    if (!numericCode || !std::isfinite(*numericCode) ||
                        std::trunc(*numericCode) != *numericCode)
                        runtimeError(instruction, "ERR$ requires an integer error code");
                    if (*numericCode < -std::ldexp(1.0, 63) ||
                        *numericCode >= std::ldexp(1.0, 63))
                        runtimeError(instruction, "ERR$ error code is out of range");
                    const auto value = static_cast<std::int64_t>(*numericCode);
                    if (value < 0 || value > 21)
                        pushValue("Unknown QL/32 error code " + std::to_string(value));
                    else
                        pushValue(std::string(runtime::errorText(
                            static_cast<runtime::ErrorCode>(value))));
                    break;
                }
                if (instruction.name == "ERRX$") {
                    pushValue(errorDetail_);
                    break;
                }
                if (instruction.name == "VAL") {
                    const auto* text = std::get_if<std::string>(&arguments[0]);
                    if (!text) runtimeError(instruction, "VAL requires a numeric string");
                    std::size_t first = 0;
                    std::size_t last = text->size();
                    while (first < last && std::isspace(static_cast<unsigned char>((*text)[first])))
                        ++first;
                    while (last > first && std::isspace(static_cast<unsigned char>((*text)[last - 1])))
                        --last;
                    std::size_t index = first;
                    if (index < last && ((*text)[index] == '+' || (*text)[index] == '-')) ++index;
                    bool digits = false;
                    while (index < last && std::isdigit(static_cast<unsigned char>((*text)[index]))) {
                        digits = true;
                        ++index;
                    }
                    if (index < last && (*text)[index] == '.') {
                        ++index;
                        while (index < last && std::isdigit(static_cast<unsigned char>((*text)[index]))) {
                            digits = true;
                            ++index;
                        }
                    }
                    if (index < last && ((*text)[index] == 'e' || (*text)[index] == 'E')) {
                        ++index;
                        if (index < last && ((*text)[index] == '+' || (*text)[index] == '-')) ++index;
                        const std::size_t exponentStart = index;
                        while (index < last && std::isdigit(static_cast<unsigned char>((*text)[index])))
                            ++index;
                        if (index == exponentStart) digits = false;
                    }
                    if (!digits || index != last)
                        runtimeError(instruction, "VAL requires one complete numeric value");
                    const std::string numericText = text->substr(first, last - first);
                    char* end = nullptr;
                    const double number = std::strtod(numericText.c_str(), &end);
                    if (end != numericText.c_str() + numericText.size() || !std::isfinite(number))
                        runtimeError(instruction, "VAL numeric value is invalid or out of range");
                    pushValue(number);
                    break;
                }
                if (instruction.name == "EVAL" || instruction.name == "EVAL$") {
                    const auto* expressionText = std::get_if<std::string>(&arguments[0]);
                    if (!expressionText) runtimeError(instruction, "EVAL requires an expression string");
                    if (callFrames.size() >= 256) runtimeError(instruction, "call stack limit exceeded");
                    detail::InstructionSequence expression;
                    try {
                        expression = detail::compileExpression(*expressionText, code);
                    } catch (const runtime::Error& error) {
                        runtimeError(instruction, error.what());
                    }
                    const std::size_t start = code.size();
                    code.insert(code.end(), expression.begin(), expression.end());
                    Instruction finish{Op::Return, 0.0, {}, instruction.line, instruction.column};
                    finish.expectsValue = true;
                    code.push_back(std::move(finish));
                    RuntimeFrame frame;
                    frame.returnAddress = pc;
                    frame.callInstruction = pc == 0 ? 0 : pc - 1;
                    frame.callSourceLine = instruction.line;
                    frame.callSourceColumn = instruction.column;
                    frame.callBasicLine = instruction.basicLine;
                    frame.function = true;
                    frame.dynamicEvaluation = true;
                    frame.stringifyResult = instruction.name == "EVAL$";
                    frame.dynamicCodeStart = start;
                    callFrames.push_back(std::move(frame));
                    pc = start;
                    break;
                }
                if (instruction.name == "SIN" || instruction.name == "COS" ||
                    instruction.name == "INT") {
                    const auto* number = std::get_if<double>(&arguments[0]);
                    if (!number) runtimeError(instruction, "numeric value required");
                    if (!std::isfinite(*number))
                        runtimeError(instruction, "argument must be finite");
                    double result = 0;
                    if (instruction.name == "SIN")
                        result = singlePrecision ?
                            static_cast<double>(std::sin(static_cast<float>(*number))) :
                            std::sin(*number);
                    else if (instruction.name == "COS")
                        result = singlePrecision ?
                            static_cast<double>(std::cos(static_cast<float>(*number))) :
                            std::cos(*number);
                    else
                        result = std::floor(*number);
                    pushValue(result);
                    break;
                }
                if (instruction.name == "BAND" || instruction.name == "BOR" ||
                    instruction.name == "BXOR" || instruction.name == "BNOT" ||
                    instruction.name == "ROL" || instruction.name == "ROR" ||
                    instruction.name == "HEX$" || instruction.name == "BIN$") {
                    constexpr std::uint64_t mask = 0xffffffffULL;
                    const auto requireWord = [&](std::size_t index) -> std::uint32_t {
                        const auto* number = std::get_if<double>(&arguments[index]);
                        if (!number) runtimeError(instruction, "32-bit unsigned integer required");
                        if (!std::isfinite(*number) || std::trunc(*number) != *number ||
                            *number < 0.0 || *number > static_cast<double>(mask))
                            runtimeError(instruction,
                                "value must be an integer from 0 through 4294967295");
                        return static_cast<std::uint32_t>(*number);
                    };
                    const std::uint32_t first = requireWord(0);
                    if (instruction.name == "BAND" || instruction.name == "BOR" ||
                        instruction.name == "BXOR") {
                        const std::uint32_t second = requireWord(1);
                        const std::uint32_t result = instruction.name == "BAND" ?
                            (first & second) : instruction.name == "BOR" ?
                            (first | second) : (first ^ second);
                        pushValue(static_cast<double>(result));
                    } else if (instruction.name == "BNOT") {
                        pushValue(static_cast<double>((~first) & mask));
                    } else if (instruction.name == "ROL" || instruction.name == "ROR") {
                        const auto* countValue = std::get_if<double>(&arguments[1]);
                        if (!countValue) runtimeError(instruction, "rotation count must be an integer");
                        if (!std::isfinite(*countValue) || std::trunc(*countValue) != *countValue)
                            runtimeError(instruction, "rotation count must be an integer");
                        double reduced = std::fmod(*countValue, 32.0);
                        if (reduced < 0.0) reduced += 32.0;
                        const unsigned count = static_cast<unsigned>(reduced);
                        const std::uint32_t result = count == 0 ? first :
                            instruction.name == "ROL" ?
                                static_cast<std::uint32_t>((first << count) | (first >> (32u - count))) :
                                static_cast<std::uint32_t>((first >> count) | (first << (32u - count)));
                        pushValue(static_cast<double>(result));
                    } else if (instruction.name == "HEX$") {
                        char buffer[9];
                        std::snprintf(buffer, sizeof(buffer), "%X", first);
                        pushValue(std::string(buffer));
                    } else {
                        std::string digits;
                        std::uint32_t value = first;
                        do {
                            digits.push_back((value & 1u) != 0 ? '1' : '0');
                            value >>= 1u;
                        } while (value != 0);
                        std::reverse(digits.begin(), digits.end());
                        pushValue(std::move(digits));
                    }
                    break;
                }
                if (instruction.name == "KILL") {
                    if (!taskControl_)
                        throw runtime::ServiceError(runtime::ServiceErrorCode::Unavailable,
                                                    "task supervisor unavailable");
                    const auto* numericId = std::get_if<double>(&arguments[0]);
                    if (!numericId || !std::isfinite(*numericId) ||
                        std::trunc(*numericId) != *numericId || *numericId < 1 ||
                        *numericId >= std::ldexp(1.0, 64))
                        runtimeError(instruction, "KILL requires a positive task ID");
                    const auto target = static_cast<runtime::TaskId>(*numericId);
                    if (target == task_id_)
                        throw runtime::Error(runtime::ErrorCode::InvalidTaskState,
                                             "a task cannot kill itself");
                    if (!taskControl_->killTask(target))
                        throw runtime::Error(runtime::ErrorCode::InvalidTaskId,
                                             "no active task exists with that ID");
                    setErrorContext(runtime::ErrorCode::None);
                    const auto previousChannel = state.printChannel;
                    state.printChannel = resolveChannel(default_output_channel_);
                    writePrintText("Killed task " + std::to_string(target) + "\n",
                                   instruction);
                    state.printChannel = previousChannel;
                    break;
                }
                if (instruction.name == "LEFT$" || instruction.name == "RIGHT$" ||
                    instruction.name == "MID$") {
                    const auto* text = std::get_if<std::string>(&arguments[0]);
                    if (!text) runtimeError(instruction, "string value required");
                    const auto requireIndex = [&](const StackEntry& value, const char* description,
                                                  bool allowZero) {
                        const auto* number = std::get_if<double>(&value);
                        if (!number) runtimeError(instruction,
                                                  "numeric value required for string index");
                        if (!std::isfinite(*number) || std::trunc(*number) != *number ||
                            *number < (allowZero ? 0.0 : 1.0))
                            runtimeError(instruction, description);
                        return *number;
                    };
                    const std::size_t length = utf8Length(*text);
                    const auto byteOffset = [&](std::size_t characterIndex) {
                        std::size_t seen = 0;
                        for (std::size_t i = 0; i < text->size(); ++i) {
                            const auto byte = static_cast<unsigned char>((*text)[i]);
                            if ((byte & 0xc0) == 0x80) continue;
                            if (seen == characterIndex) return i;
                            ++seen;
                        }
                        return text->size();
                    };
                    std::size_t first = 0;
                    std::size_t count = 0;
                    if (instruction.name == "LEFT$") {
                        const double requested = requireIndex(
                            arguments[1], "LEFT$ length must be a nonnegative integer", true);
                        count = requested >= static_cast<double>(length) ? length :
                            static_cast<std::size_t>(requested);
                    } else if (instruction.name == "RIGHT$") {
                        const double requested = requireIndex(
                            arguments[1], "RIGHT$ length must be a nonnegative integer", true);
                        count = requested >= static_cast<double>(length) ? length :
                            static_cast<std::size_t>(requested);
                        first = length - count;
                    } else {
                        const double requestedStart = requireIndex(
                            arguments[1], "MID$ start must be a positive integer", false);
                        if (requestedStart > static_cast<double>(length)) {
                            pushValue(std::string{});
                            break;
                        }
                        first = static_cast<std::size_t>(requestedStart) - 1;
                        const std::size_t remaining = length - first;
                        if (arguments.size() == 2) {
                            count = remaining;
                        } else {
                            const double requestedLength = requireIndex(
                                arguments[2], "MID$ length must be a nonnegative integer", true);
                            count = requestedLength >= static_cast<double>(remaining) ? remaining :
                                static_cast<std::size_t>(requestedLength);
                        }
                    }
                    const std::size_t firstByte = byteOffset(first);
                    const std::size_t lastByte = byteOffset(first + count);
                    pushValue(text->substr(firstByte, lastByte - firstByte));
                    break;
                }
                if (instruction.name == "LEN") {
                    const auto* text = std::get_if<std::string>(&arguments[0]);
                    if (!text) runtimeError(instruction, "string value required");
                    pushValue(static_cast<double>(utf8Length(*text)));
                    break;
                }
                if (instruction.name == "FREE") {
                    if (!memory_)
                        throw runtime::ServiceError(runtime::ServiceErrorCode::Unavailable,
                                                    "memory service unavailable");
                    std::uint64_t selector = 0;
                    if (!arguments.empty()) {
                        const auto* number = std::get_if<double>(&arguments[0]);
                        if (!number || !std::isfinite(*number) || std::trunc(*number) != *number ||
                            *number < 0 || *number > 2)
                            runtimeError(instruction, "FREE selector must be 0, 1, or 2");
                        selector = static_cast<std::uint64_t>(*number);
                    }
                    if (selector == 0) {
                        pushValue(static_cast<double>(memory_->freeBytes()));
                    } else {
                        const auto info = memory_->fragmentationInfo();
                        if (!info)
                            throw runtime::ServiceError(runtime::ServiceErrorCode::Unavailable,
                                "heap fragmentation metrics unavailable on this platform");
                        const auto value = selector == 1 ? info->largestFreeBlockBytes :
                                                           info->freeBlockCount;
                        pushValue(static_cast<double>(value));
                    }
                    break;
                }
                if (instruction.name == "GETCHANNEL") {
                    const auto integerArgument = [&](std::size_t index, double maximum,
                                                     const char* message) {
                        const auto* number = std::get_if<double>(&arguments[index]);
                        if (!number) runtimeError(instruction, "numeric value required");
                        if (!std::isfinite(*number) || std::trunc(*number) != *number ||
                            *number < 0 || *number > maximum)
                            runtimeError(instruction, message);
                        return static_cast<std::uint64_t>(*number);
                    };
                    const auto selector = integerArgument(0, 2, "invalid GETCHANNEL selector");
                    std::vector<runtime::ChannelInfo> openChannels{
                        {0, runtime::ChannelType::Console},
                        {1, runtime::ChannelType::Window}
                    };
                    if (channels_) {
                        const auto adapterChannels = channels_->listOpen(session_id_);
                        for (const auto& channel : adapterChannels) {
                            if (channel.handle < 2)
                                runtimeError(instruction, "channel handle conflicts with a reserved channel");
                            openChannels.push_back(channel);
                        }
                    }
                    std::sort(openChannels.begin(), openChannels.end(),
                        [](const auto& left, const auto& right) {
                            return left.handle < right.handle;
                        });
                    if (selector == 0) {
                        if (arguments.size() != 1)
                            runtimeError(instruction, "GETCHANNEL count takes one argument");
                        pushValue(static_cast<double>(openChannels.size()));
                    } else if (selector == 1) {
                        if (arguments.size() != 2)
                            runtimeError(instruction, "GETCHANNEL handle requires a position");
                        const auto position = integerArgument(1,
                            static_cast<double>(openChannels.size()),
                            "channel position out of range");
                        if (position >= openChannels.size())
                            runtimeError(instruction, "channel position out of range");
                        pushValue(static_cast<double>(openChannels[position].handle));
                    } else {
                        if (arguments.size() != 2)
                            runtimeError(instruction, "GETCHANNEL type requires a channel handle");
                        const auto handle = integerArgument(1, 9007199254740991.0,
                                                           "invalid channel handle");
                        const runtime::ChannelId activeEndpoint =
                            resolveChannel(static_cast<runtime::ChannelId>(handle));
                        const auto found = std::find_if(openChannels.begin(), openChannels.end(),
                            [activeEndpoint](const auto& channel) {
                                return channel.handle == activeEndpoint;
                            });
                        if (found == openChannels.end())
                            runtimeError(instruction, "channel handle is not open");
                        pushValue(std::string(runtime::channelTypeName(found->type)));
                    }
                    break;
                }
                if (instruction.name == "GWIDTH" || instruction.name == "GHEIGHT" ||
                    instruction.name == "GDEPTH" || instruction.name == "GORIGINX" ||
                    instruction.name == "GORIGINY" || instruction.name == "GROTATION" ||
                    instruction.name == "GPALETTESIZE" || instruction.name == "GPALETTE$" ||
                    instruction.name == "ROTATE") {
                    if (!graphics_)
                        throw runtime::ServiceError(runtime::ServiceErrorCode::Unavailable,
                                                    "graphics service unavailable");
                    const auto integerArgument = [&](std::size_t index) {
                        const auto* number = std::get_if<double>(&arguments[index]);
                        if (!number) runtimeError(instruction, "numeric value required");
                        if (!std::isfinite(*number) || std::trunc(*number) != *number ||
                            *number < 0 || *number > 65535)
                            runtimeError(instruction, "argument must be a nonnegative integer");
                        return static_cast<std::uint16_t>(*number);
                    };
                    if (instruction.name == "ROTATE") {
                        const auto degrees = integerArgument(0);
                        if (degrees != 0 && degrees != 90 && degrees != 180 && degrees != 270)
                            runtimeError(instruction, "rotation must be 0, 90, 180, or 270 degrees");
                        graphics_->setRotation(session_id_, degrees);
                    } else if (instruction.name == "GPALETTE$") {
                        const auto index = integerArgument(0);
                        if (index >= graphics_->windowInfo(session_id_).paletteSize)
                            runtimeError(instruction, "palette index out of range");
                        const auto color = graphics_->paletteColor(session_id_, index);
                        constexpr char hex[] = "0123456789ABCDEF";
                        std::string result = "#000000";
                        const std::uint8_t channels[] = {color.red, color.green, color.blue};
                        for (std::size_t i = 0; i < 3; ++i) {
                            result[1 + i * 2] = hex[channels[i] >> 4];
                            result[2 + i * 2] = hex[channels[i] & 0x0F];
                        }
                        pushValue(std::move(result));
                    } else {
                        const auto info = graphics_->windowInfo(session_id_);
                        if (instruction.name == "GWIDTH") pushValue(static_cast<double>(info.width));
                        else if (instruction.name == "GHEIGHT") pushValue(static_cast<double>(info.height));
                        else if (instruction.name == "GORIGINX") pushValue(static_cast<double>(info.originX));
                        else if (instruction.name == "GORIGINY") pushValue(static_cast<double>(info.originY));
                        else if (instruction.name == "GDEPTH") pushValue(static_cast<double>(info.colorDepth));
                        else if (instruction.name == "GROTATION") pushValue(static_cast<double>(info.rotationDegrees));
                        else pushValue(static_cast<double>(info.paletteSize));
                    }
                    break;
                }
                if (instruction.name == "GETNET") {
                    if (!network_)
                        throw runtime::ServiceError(runtime::ServiceErrorCode::Unavailable,
                                                    "network service unavailable");
                    const auto* ssid = std::get_if<std::string>(&arguments[0]);
                    const auto* password = std::get_if<std::string>(&arguments[1]);
                    if (!ssid || !password) runtimeError(instruction, "string values required");
                    network_->connectWifi(*ssid, *password);
                    break;
                }
                if (instruction.name == "DISNET") {
                    if (!network_)
                        throw runtime::ServiceError(runtime::ServiceErrorCode::Unavailable,
                                                    "network service unavailable");
                    network_->disconnectWifi();
                    break;
                }
                if (instruction.name == "NETSTAT$" || instruction.name == "NETADDR$" ||
                    instruction.name == "NETSIGNAL") {
                    if (!network_)
                        throw runtime::ServiceError(runtime::ServiceErrorCode::Unavailable,
                                                    "network service unavailable");
                    const runtime::WifiStatus status = network_->wifiStatus();
                    if (instruction.name == "NETADDR$") {
                        pushValue(status.ipv4Address);
                    } else if (instruction.name == "NETSIGNAL") {
                        pushValue(status.state == runtime::WifiState::Connected ?
                            static_cast<double>(status.signalDbm) : 0.0);
                    } else {
                        switch (status.state) {
                        case runtime::WifiState::Disconnected: pushValue(std::string("DISCONNECTED")); break;
                        case runtime::WifiState::Connecting: pushValue(std::string("CONNECTING")); break;
                        case runtime::WifiState::Connected: pushValue(std::string("CONNECTED")); break;
                        case runtime::WifiState::Failed: pushValue(std::string("FAILED")); break;
                        }
                    }
                    break;
                }
                if (instruction.name == "ALERT" || instruction.name == "CONFIRM") {
                    if (!ui_) throw runtime::ServiceError(runtime::ServiceErrorCode::Unavailable,
                                                           "UI service unavailable");
                    const auto textArgument = [&](std::size_t index) -> const std::string& {
                        if (const auto* text = std::get_if<std::string>(&arguments[index])) return *text;
                        runtimeError(instruction, "string value required");
                    };
                    const std::string title = textArgument(0);
                    const std::string message = textArgument(1);
                    state.pendingUiKind = instruction.name == "ALERT" ?
                        ui::RequestKind::Alert : ui::RequestKind::Confirm;
                    state.pendingUiRequest = instruction.name == "ALERT" ?
                        ui_->beginAlert(session_id_, title, message) :
                        ui_->beginConfirm(session_id_, title, message);
                    return ExecutionStatus::Suspended;
                }
                if (instruction.name == "DINIT" || instruction.name == "MINIT" ||
                    instruction.name == "DTEXT" || instruction.name == "DEDIT" ||
                    instruction.name == "DINT" || instruction.name == "DFLOAT" ||
                    instruction.name == "DCHOICE" || instruction.name == "DCHECK" ||
                    instruction.name == "DBUTTONS" || instruction.name == "DIALOG" ||
                    instruction.name == "DGET" || instruction.name == "DGET$" ||
                    instruction.name == "MCARD" || instruction.name == "MENU") {
                    const auto stringArgument = [&](std::size_t index) -> const std::string& {
                        if (const auto* value = std::get_if<std::string>(&arguments[index]))
                            return *value;
                        runtimeError(instruction, "string value required");
                    };
                    const auto numericArgument = [&](std::size_t index) {
                        if (const auto* value = std::get_if<double>(&arguments[index]))
                            return *value;
                        runtimeError(instruction, "numeric value required");
                    };
                    const auto handleArgument = [&](std::size_t index) {
                        const double value = numericArgument(index);
                        if (!std::isfinite(value) || std::trunc(value) != value || value < 1 ||
                            value > 9007199254740991.0)
                            runtimeError(instruction, "invalid UI builder handle");
                        return static_cast<std::uint64_t>(value);
                    };
                    const auto fieldPosition = [&](std::size_t index) {
                        const double value = numericArgument(index);
                        if (!std::isfinite(value) || std::trunc(value) != value || value < 1 ||
                            value > 9007199254740991.0)
                            runtimeError(instruction, "field number must be a positive integer");
                        return static_cast<std::size_t>(value - 1);
                    };
                    if (instruction.name == "DINIT" || instruction.name == "MINIT") {
                        if (uiSession_->nextUiBuilderHandle > 9007199254740991ULL)
                            runtimeError(instruction, "UI builder handle limit exceeded");
                        const std::uint64_t handle = uiSession_->nextUiBuilderHandle++;
                        if (instruction.name == "DINIT")
                            uiSession_->dialogs.emplace(handle, DialogBuilder{stringArgument(0)});
                        else
                            uiSession_->menus.emplace(handle, MenuBuilder{stringArgument(0)});
                        pushValue(static_cast<double>(handle));
                    } else if (instruction.name == "DTEXT" || instruction.name == "DEDIT" ||
                               instruction.name == "DINT" || instruction.name == "DFLOAT" ||
                               instruction.name == "DCHOICE" || instruction.name == "DCHECK" ||
                               instruction.name == "DBUTTONS" || instruction.name == "DIALOG" ||
                               instruction.name == "DGET" || instruction.name == "DGET$") {
                        const std::uint64_t handle = handleArgument(0);
                        const auto found = uiSession_->dialogs.find(handle);
                        if (found == uiSession_->dialogs.end())
                            runtimeError(instruction, "unknown dialog handle");
                        DialogBuilder& dialog = found->second;
                        if (instruction.name == "DTEXT") {
                            dialog.fields.push_back({ui::FieldKind::Text, stringArgument(1),
                                                     std::monostate{}});
                        } else if (instruction.name == "DEDIT") {
                            dialog.fields.push_back({ui::FieldKind::Edit, stringArgument(1),
                                                     stringArgument(2)});
                        } else if (instruction.name == "DINT" || instruction.name == "DFLOAT") {
                            const double initial = numericArgument(2);
                            const double minimum = numericArgument(3);
                            const double maximum = numericArgument(4);
                            if (!std::isfinite(minimum) || !std::isfinite(maximum) ||
                                minimum > maximum ||
                                (instruction.name == "DINT" && std::trunc(initial) != initial))
                                runtimeError(instruction, "invalid dialog numeric range or initial value");
                            dialog.fields.push_back({
                                instruction.name == "DINT" ? ui::FieldKind::Integer : ui::FieldKind::Float,
                                stringArgument(1), initial, {}, minimum, maximum});
                        } else if (instruction.name == "DCHOICE") {
                            const double initial = numericArgument(2);
                            if (!std::isfinite(initial) || std::trunc(initial) != initial || initial < 1)
                                runtimeError(instruction, "choice number must be a positive integer");
                            std::vector<std::string> choices;
                            const std::string& encoded = stringArgument(3);
                            std::size_t start = 0;
                            while (start <= encoded.size()) {
                                const std::size_t separator = encoded.find('|', start);
                                choices.push_back(encoded.substr(start, separator == std::string::npos ?
                                    std::string::npos : separator - start));
                                if (separator == std::string::npos) break;
                                start = separator + 1;
                            }
                            if (choices.empty() || initial > static_cast<double>(choices.size()))
                                runtimeError(instruction, "initial choice is outside the option list");
                            dialog.fields.push_back({ui::FieldKind::Choice, stringArgument(1),
                                                     initial - 1, std::move(choices)});
                        } else if (instruction.name == "DCHECK") {
                            const double initial = numericArgument(2);
                            if (initial != 0 && initial != 1)
                                runtimeError(instruction, "check initial value must be 0 or 1");
                            dialog.fields.push_back({ui::FieldKind::Check, stringArgument(1),
                                                     initial == 1});
                        } else if (instruction.name == "DBUTTONS") {
                            const std::string& accept = stringArgument(1);
                            const std::string& cancel = stringArgument(2);
                            if (accept.empty() || cancel.empty())
                                runtimeError(instruction, "dialog button labels cannot be empty");
                            dialog.acceptLabel = accept;
                            dialog.cancelLabel = cancel;
                        } else if (instruction.name == "DGET" || instruction.name == "DGET$") {
                            if (!dialog.hasResults)
                                runtimeError(instruction, "dialog has no submitted values");
                            const std::size_t position = fieldPosition(1);
                            if (position >= dialog.results.size())
                                runtimeError(instruction, "dialog field number out of range");
                            const ui::FieldValue& value = dialog.results[position];
                            if (instruction.name == "DGET$") {
                                const auto* text = std::get_if<std::string>(&value);
                                if (!text) runtimeError(instruction, "dialog field is not a string");
                                pushValue(*text);
                            } else if (const auto* number = std::get_if<double>(&value)) {
                                const double result = dialog.fields[position].kind == ui::FieldKind::Choice ?
                                    *number + 1 : *number;
                                pushValue(result);
                            } else if (const auto* checked = std::get_if<bool>(&value)) {
                                pushValue(*checked ? 1.0 : 0.0);
                            } else {
                                runtimeError(instruction, "dialog field has no value");
                            }
                        } else {
                            if (!ui_) throw runtime::ServiceError(
                                runtime::ServiceErrorCode::Unavailable, "UI service unavailable");
                            dialog.hasResults = false;
                            state.pendingUiKind = ui::RequestKind::Dialog;
                            state.pendingUiBuilderHandle = handle;
                            state.pendingUiRequest = ui_->beginDialog(
                                session_id_, dialog.title, dialog.fields,
                                dialog.acceptLabel, dialog.cancelLabel);
                            return ExecutionStatus::Suspended;
                        }
                    } else if (instruction.name == "MCARD") {
                        const std::uint64_t handle = handleArgument(0);
                        const auto found = uiSession_->menus.find(handle);
                        if (found == uiSession_->menus.end())
                            runtimeError(instruction, "unknown menu handle");
                        const double rawId = numericArgument(1);
                        if (!std::isfinite(rawId) || std::trunc(rawId) != rawId || rawId < 1 ||
                            rawId > static_cast<double>(std::numeric_limits<std::uint32_t>::max()))
                            runtimeError(instruction, "menu command ID must be a positive 32-bit integer");
                        const auto id = static_cast<std::uint32_t>(rawId);
                        for (const auto& item : found->second.items)
                            if (item.commandId == id)
                                runtimeError(instruction, "duplicate menu command ID");
                        found->second.items.push_back({id, stringArgument(2), stringArgument(3), true});
                    } else if (instruction.name == "MENU") {
                        if (!ui_) throw runtime::ServiceError(
                            runtime::ServiceErrorCode::Unavailable, "UI service unavailable");
                        const std::uint64_t handle = handleArgument(0);
                        const auto found = uiSession_->menus.find(handle);
                        if (found == uiSession_->menus.end())
                            runtimeError(instruction, "unknown menu handle");
                        found->second.hasResult = false;
                        found->second.selectedCommand = 0;
                        state.pendingUiKind = ui::RequestKind::Menu;
                        state.pendingUiBuilderHandle = handle;
                        state.pendingUiRequest = ui_->beginMenu(
                            session_id_, found->second.title, found->second.items);
                        return ExecutionStatus::Suspended;
                    }
                    break;
                }
                if (instruction.name == "GUPDATE") {
                    if (!graphics_)
                        throw runtime::ServiceError(runtime::ServiceErrorCode::Unavailable,
                                                    "graphics service unavailable");
                    if (arguments.empty()) {
                        graphics_->update(session_id_);
                    } else {
                        const auto* mode = std::get_if<double>(&arguments[0]);
                        if (!mode || !std::isfinite(*mode) || std::trunc(*mode) != *mode)
                            runtimeError(instruction, "GUPDATE mode must be an integer");
                        if (*mode == 0.0)
                            graphics_->setUpdateMode(session_id_, graphics::UpdateMode::Off);
                        else if (*mode == 1.0)
                            graphics_->setUpdateMode(session_id_, graphics::UpdateMode::Auto);
                        else
                            throw runtime::Error(runtime::ErrorCode::InvalidArgument,
                                                 "GUPDATE mode must be 0 (off) or 1 (automatic)");
                    }
                    break;
                }
                if (instruction.name == "INK" || instruction.name == "PAPER" ||
                    instruction.name == "PLOT" || instruction.name == "LINE" ||
                    instruction.name == "RECT" ||
                    instruction.name == "CIRCLE" || instruction.name == "TEXT" ||
                    instruction.name == "CLS" || instruction.name == "WINDOW" ||
                    instruction.name == "CLOSEWINDOW") {
                    const auto channel = selectedChannel.value_or(
                        resolveChannel(default_output_channel_));
                    bool nullSink = false;
                    bool windowEndpoint = channel == 0 || channel == 1;
                    if (channel > 1 && channels_) {
                        const auto endpoints = channels_->listOpen(session_id_);
                        const auto endpoint = std::find_if(endpoints.begin(), endpoints.end(),
                            [&](const runtime::ChannelInfo& candidate) {
                                return candidate.handle == channel;
                            });
                        if (endpoint != endpoints.end()) {
                            windowEndpoint = endpoint->type == runtime::ChannelType::Window;
                            nullSink = endpoint->type == runtime::ChannelType::Null;
                        }
                    }
                    if (!windowEndpoint && !nullSink)
                        runtimeError(instruction, "selected channel is not a graphics window");
                    if (!nullSink && !graphics_)
                        throw runtime::ServiceError(runtime::ServiceErrorCode::Unavailable,
                                                    "graphics service unavailable");
                    if (!nullSink) {
                    const auto integerArgument = [&](std::size_t index,
                                                     const char* description) {
                        const auto* number = std::get_if<double>(&arguments[index]);
                        if (!number) runtimeError(instruction, "numeric value required");
                        const double value = *number;
                        if (!std::isfinite(value) || std::trunc(value) != value ||
                            value < std::numeric_limits<std::int32_t>::min() ||
                            value > std::numeric_limits<std::int32_t>::max())
                            runtimeError(instruction, description);
                        return static_cast<std::int32_t>(value);
                    };
                    const auto pointArguments = [&]() {
                        return graphics::Point{integerArgument(0, "pixel coordinate out of range"),
                                               integerArgument(1, "pixel coordinate out of range")};
                    };
                    const auto fillRequested = [&](std::size_t index) {
                        const auto fill = integerArgument(index, "fill flag must be 0 or 1");
                        if (fill != 0 && fill != 1)
                            runtimeError(instruction, "fill flag must be 0 or 1");
                        return fill == 1;
                    };
                    if (instruction.name == "CLOSEWINDOW") {
                        graphics_->closeWindow(session_id_, channel);
                    } else if (instruction.name == "INK" || instruction.name == "PAPER") {
                        const auto color = integerArgument(0, "color index must be a nonnegative integer");
                        const auto paletteSize = graphics_->windowInfo(session_id_, channel).paletteSize;
                        if (color < 0 || static_cast<std::uint32_t>(color) >= paletteSize)
                            runtimeError(instruction, "color index exceeds the current window palette");
                        if (instruction.name == "INK")
                            graphics_->setInk(session_id_, channel,
                                static_cast<graphics::ColorIndex>(color));
                        else
                            graphics_->setPaper(session_id_, channel,
                                static_cast<graphics::ColorIndex>(color));
                    } else if (instruction.name == "WINDOW") {
                        const auto x = integerArgument(0, "window position out of range");
                        const auto y = integerArgument(1, "window position out of range");
                        const auto width = integerArgument(2, "window size must be positive");
                        const auto height = integerArgument(3, "window size must be positive");
                        if (width <= 0 || height <= 0)
                            runtimeError(instruction, "window size must be positive");
                        graphics_->setWindowBounds(session_id_, channel,
                            {x, y, static_cast<std::uint32_t>(width),
                             static_cast<std::uint32_t>(height)});
                    } else if (instruction.name == "PLOT") {
                        graphics_->pixel(session_id_, channel, pointArguments());
                    } else if (instruction.name == "LINE") {
                        const auto from = pointArguments();
                        const auto to = graphics::Point{
                            integerArgument(2, "pixel coordinate out of range"),
                            integerArgument(3, "pixel coordinate out of range")};
                        graphics_->line(session_id_, channel, from, to);
                    } else if (instruction.name == "RECT") {
                        const auto topLeft = pointArguments();
                        const auto width = integerArgument(2, "rectangle size must be positive");
                        const auto height = integerArgument(3, "rectangle size must be positive");
                        if (width <= 0 || height <= 0)
                            runtimeError(instruction, "rectangle size must be positive");
                        if (arguments.size() > 4 && fillRequested(4))
                            graphics_->fillRectangle(session_id_, channel,
                                                     topLeft, width, height);
                        else
                            graphics_->rectangle(session_id_, channel,
                                                 topLeft, width, height);
                    } else if (instruction.name == "CIRCLE") {
                        const auto center = pointArguments();
                        const auto radius = integerArgument(2, "circle radius must be nonnegative");
                        if (radius < 0) runtimeError(instruction, "circle radius must be nonnegative");
                        if (arguments.size() > 3 && fillRequested(3))
                            graphics_->fillCircle(session_id_, channel, center, radius);
                        else
                            graphics_->circle(session_id_, channel, center, radius);
                    } else if (instruction.name == "TEXT") {
                        const auto position = pointArguments();
                        const auto* value = std::get_if<std::string>(&arguments[2]);
                        if (!value) runtimeError(instruction, "string value required");
                        graphics_->text(session_id_, channel, position, *value);
                    } else if (instruction.name == "CLS") {
                        if (!arguments.empty()) {
                            const auto ink = integerArgument(0,
                                "ink color must be an integer in the active window palette");
                            const auto paper = integerArgument(1,
                                "paper color must be an integer in the active window palette");
                            const auto paletteSize = graphics_->windowInfo(
                                session_id_, channel).paletteSize;
                            if (ink < 0 || paper < 0 ||
                                static_cast<std::uint32_t>(ink) >= paletteSize ||
                                static_cast<std::uint32_t>(paper) >= paletteSize)
                                runtimeError(instruction,
                                    "CLS color index exceeds the current window palette");
                            graphics_->setInk(session_id_, channel,
                                static_cast<graphics::ColorIndex>(ink));
                            graphics_->setPaper(session_id_, channel,
                                static_cast<graphics::ColorIndex>(paper));
                        }
                        graphics_->clear(session_id_, channel);
                    } else {
                        graphics_->clear(session_id_, channel);
                    }
                    }
                    break;
                }
                const auto textArgument = [&](std::size_t index) -> const std::string& {
                    if (const auto* text = std::get_if<std::string>(&arguments[index])) return *text;
                    runtimeError(instruction, "string value required");
                };
                const auto channelArgument = [&](std::size_t index) {
                    const auto* number = std::get_if<double>(&arguments[index]);
                    if (!number) runtimeError(instruction, "numeric value required");
                    const double value = *number;
                    if (!std::isfinite(value) || std::trunc(value) != value || value < 0 ||
                        value > 9007199254740991.0)
                        runtimeError(instruction, "invalid channel number");
                    return static_cast<runtime::ChannelId>(value);
                };
                const auto integerArgument = [&](std::size_t index, const char* label,
                                                 double minimum, double maximum) {
                    const auto* number = std::get_if<double>(&arguments[index]);
                    if (!number || !std::isfinite(*number) || std::trunc(*number) != *number ||
                        *number < minimum || *number > maximum)
                        runtimeError(instruction, label);
                    return static_cast<std::uint64_t>(*number);
                };
                if (instruction.name == "OPEN") {
                    if (!channels_)
                        throw runtime::ServiceError(runtime::ServiceErrorCode::Unavailable,
                                                    "channel service unavailable");
                    runtime::ChannelDevice device = runtime::ChannelDevice::File;
                    std::vector<runtime::DeviceArgument> parameters;
                    if (const auto* deviceNumber = std::get_if<double>(&arguments[0])) {
                        if (!std::isfinite(*deviceNumber) ||
                            std::trunc(*deviceNumber) != *deviceNumber ||
                            *deviceNumber < static_cast<double>(runtime::ChannelDevice::File) ||
                            *deviceNumber > static_cast<double>(runtime::ChannelDevice::Timer))
                            runtimeError(instruction, "invalid OPEN device kind");
                        device = static_cast<runtime::ChannelDevice>(
                            static_cast<std::uint64_t>(*deviceNumber));
                        for (std::size_t index = 1; index < arguments.size(); ++index) {
                            if (const auto* text = std::get_if<std::string>(&arguments[index]))
                                parameters.emplace_back(*text);
                            else parameters.emplace_back(std::get<double>(arguments[index]));
                        }
                    } else {
                        if (arguments.size() != 2)
                            runtimeError(instruction, "legacy OPEN requires a path and mode");
                        parameters.emplace_back(textArgument(0));
                        parameters.emplace_back(textArgument(1));
                    }
                    const auto id = channels_->openTypedDevice(session_id_, device, parameters);
                    pushValue(static_cast<double>(id));
                } else if (instruction.name == "IN") {
                    if (!channels_) throw runtime::ServiceError(
                        runtime::ServiceErrorCode::Unavailable, "channel service unavailable");
                    const auto channel = resolveChannel(channelArgument(0));
                    std::optional<double> reg;
                    if (arguments.size() == 2) reg = std::get<double>(arguments[1]);
                    pushValue(channels_->readValue(session_id_, channel, reg));
                } else if (instruction.name == "ALLOC") {
                    const auto size = integerArgument(0, "ALLOC size must be a positive integer",
                                                      1, 16 * 1024 * 1024);
                    pushValue(static_cast<double>(buffers_.allocate(static_cast<std::size_t>(size))));
                } else if (instruction.name == "PEEK" || instruction.name == "PEEK_W" ||
                           instruction.name == "PEEK_L") {
                    const auto address = integerArgument(0, "invalid buffer address", 0,
                        9007199254740991.0);
                    const std::size_t width = instruction.name == "PEEK" ? 1 :
                        instruction.name == "PEEK_W" ? 2 : 4;
                    const auto value = width == 1 ? buffers_.peek(address) :
                        buffers_.peekWord(address, width);
                    pushValue(static_cast<double>(value));
                } else if (instruction.name == "IOREAD" || instruction.name == "IOWRITE") {
                    if (!channels_) throw runtime::ServiceError(
                        runtime::ServiceErrorCode::Unavailable, "channel service unavailable");
                    try {
                        const auto channel = resolveChannel(channelArgument(0));
                        const bool hasRegister = arguments.size() == 3;
                        const std::optional<double> reg = hasRegister ?
                            std::optional<double>(std::get<double>(arguments[1])) : std::nullopt;
                        const std::size_t addressIndex = hasRegister ? 2 : 1;
                        const auto address = integerArgument(addressIndex,
                            "invalid buffer address", 0, 9007199254740991.0);
                        const auto size = buffers_.allocationSize(address);
                        std::vector<std::uint8_t> bytes(size);
                        if (instruction.name == "IOWRITE") {
                            buffers_.read(address, bytes.data(), bytes.size());
                            channels_->writeDeviceBytes(session_id_, channel, reg,
                                bytes.data(), bytes.size());
                            pushValue(0.0);
                        } else {
                            const auto count = channels_->readDeviceBytes(session_id_, channel,
                                reg, bytes.data(), bytes.size());
                            if (count > bytes.size()) runtimeError(instruction,
                                "device returned more bytes than the buffer can hold");
                            buffers_.write(address, bytes.data(), count);
                            pushValue(static_cast<double>(count));
                        }
                    } catch (const runtime::ServiceError& error) {
                        pushValue(-static_cast<double>(error.languageCode()));
                    }
                } else if (instruction.name == "READ$") {
                    if (!channels_)
                        throw runtime::ServiceError(runtime::ServiceErrorCode::Unavailable,
                                                    "channel service unavailable");
                    const auto channel = channelArgument(0);
                    const auto inputChannel = resolveChannel(channel);
                    if (inputChannel <= 1)
                        runtimeError(instruction, "selected channel does not support line input");
                    const auto line = channels_->readLine(session_id_, inputChannel);
                    if (!line && channels_->isHardwareChannel(session_id_, inputChannel)) {
                        state.pendingChannelRead = inputChannel;
                        stack.emplace_back(static_cast<double>(inputChannel));
                        --pc;
                        return ExecutionStatus::WaitingChannel;
                    }
                    state.pendingChannelRead.reset();
                    pushValue(line ? Value(*line) : Value(std::string{}));
                } else if (instruction.name == "EOF") {
                    if (!channels_)
                        throw runtime::ServiceError(runtime::ServiceErrorCode::Unavailable,
                                                    "channel service unavailable");
                    const auto channel = channelArgument(0);
                    const auto inputChannel = resolveChannel(channel);
                    if (inputChannel <= 1)
                        runtimeError(instruction, "selected channel does not support EOF");
                    pushValue(channels_->atEnd(session_id_, inputChannel) ? 1.0 : 0.0);
                } else if (instruction.name == "WRITE") {
                    const auto channel = resolveChannel(channelArgument(0));
                    const std::string text = textArgument(1) + "\n";
                    if (channel == 0) {
                        output << text;
                        output_column_ = 0;
                    } else if (channel == 1) {
                        if (graphics_) graphics_->writeText(session_id_, text);
                        else output << text;
                    } else {
                        if (!channels_)
                            throw runtime::ServiceError(runtime::ServiceErrorCode::Unavailable,
                                                        "channel service unavailable");
                        channels_->write(session_id_, channel, text);
                    }
                } else if (instruction.name == "CLOSE") {
                    closeChannel(channelArgument(0));
                } else if (instruction.name == "DEALLOC") {
                    buffers_.deallocate(integerArgument(0, "invalid buffer address", 0,
                        9007199254740991.0));
                } else if (instruction.name == "POKE" || instruction.name == "POKE_W" ||
                           instruction.name == "POKE_L") {
                    const auto address = integerArgument(0, "invalid buffer address", 0,
                        9007199254740991.0);
                    const std::size_t width = instruction.name == "POKE" ? 1 :
                        instruction.name == "POKE_W" ? 2 : 4;
                    if (width > 1) {
                        const auto value = integerArgument(1, "POKE value is out of range", 0,
                            width == 2 ? 65535 : 4294967295.0);
                        buffers_.pokeWord(address, width, value);
                    } else {
                        for (std::size_t index = 1; index < arguments.size(); ++index) {
                            const auto value = integerArgument(index,
                                "POKE byte must be an integer from 0 to 255", 0, 255);
                            buffers_.poke(address + index - 1, static_cast<std::uint8_t>(value));
                        }
                    }
                } else if (instruction.name == "OUT") {
                    if (!channels_) throw runtime::ServiceError(
                        runtime::ServiceErrorCode::Unavailable, "channel service unavailable");
                    const auto channel = resolveChannel(channelArgument(0));
                    const bool hasRegister = arguments.size() == 3;
                    const std::optional<double> reg = hasRegister ?
                        std::optional<double>(std::get<double>(arguments[1])) : std::nullopt;
                    const double value = std::get<double>(arguments[hasRegister ? 2 : 1]);
                    channels_->writeValue(session_id_, channel, value, reg);
                } else if (instruction.name == "XFER") {
                    if (!channels_ || !selectedChannel)
                        throw runtime::ServiceError(runtime::ServiceErrorCode::Unavailable,
                                                    "hardware channel service unavailable");
                    const auto channel = resolveChannel(*selectedChannel);
                    const auto txAddress = integerArgument(0, "invalid transmit buffer address", 0,
                        9007199254740991.0);
                    const auto txLength = integerArgument(1, "invalid transmit length", 0,
                        16 * 1024 * 1024);
                    const auto rxAddress = integerArgument(2, "invalid receive buffer address", 0,
                        9007199254740991.0);
                    const auto rxLength = integerArgument(3, "invalid receive length", 0,
                        16 * 1024 * 1024);
                    std::vector<std::uint8_t> tx(static_cast<std::size_t>(txLength));
                    std::vector<std::uint8_t> rx(static_cast<std::size_t>(rxLength));
                    if (txLength != 0) buffers_.read(txAddress, tx.data(), tx.size());
                    if (rxLength != 0) buffers_.write(rxAddress, rx.data(), rx.size());
                    channels_->transfer(session_id_, channel, tx.data(), tx.size(),
                                        rx.data(), rx.size());
                    if (rxLength != 0) buffers_.write(rxAddress, rx.data(), rx.size());
                }
                break;
            }
            if (callFrames.size() >= 256) runtimeError(instruction, "call stack limit exceeded");
            RuntimeFrame frame;
            frame.returnAddress = pc;
            frame.callInstruction = pc == 0 ? 0 : pc - 1;
            frame.callSourceLine = instruction.line;
            frame.callSourceColumn = instruction.column;
            frame.callBasicLine = instruction.basicLine;
            frame.function = instruction.expectsValue;
            frame.name = instruction.name;
            for (std::size_t i = 0; i < instruction.parameters.size(); ++i) {
                const std::string& parameter = instruction.parameters[i];
                if (const auto* array = std::get_if<ArrayReference>(&arguments[i])) {
                    std::string actualName = array->name;
                    if (!callFrames.empty()) {
                        const auto alias = callFrames.back().arrayAliases.find(actualName);
                        if (alias != callFrames.back().arrayAliases.end()) actualName = alias->second;
                    }
                    if (arrays.find(actualName) == arrays.end())
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
                if (frame.locals.find(local) == frame.locals.end()) {
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
            const bool dynamicEvaluation = frame.dynamicEvaluation;
            const bool stringifyResult = frame.stringifyResult;
            const std::size_t dynamicCodeStart = frame.dynamicCodeStart;
            if (dynamicEvaluation) {
                if (stringifyResult) {
                    if (const auto* number = std::get_if<double>(&result))
                        result = formatNumber(*number);
                } else if (!std::holds_alternative<double>(result)) {
                    runtimeError(instruction, "EVAL expression must return a number");
                }
            }
            callFrames.pop_back();
            if (dynamicEvaluation)
                code.erase(code.begin() + static_cast<std::ptrdiff_t>(dynamicCodeStart),
                           code.end());
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
            pushValue(array->values[offset],
                      !instruction.name.empty() && instruction.name.back() == '%');
            break;
        }
        case Op::StringSlice: {
            double endValue = 0;
            double startValue = 1;
            if (instruction.sliceHasEnd) endValue = numeric(pop(), instruction);
            if (instruction.sliceHasStart) startValue = numeric(pop(), instruction);
            Value source = pop();
            const auto* text = std::get_if<std::string>(&source);
            if (!text) runtimeError(instruction, "string value required for slice");

            std::vector<std::size_t> offsets;
            offsets.reserve(text->size() + 1);
            for (std::size_t i = 0; i < text->size(); ++i) {
                const auto byte = static_cast<unsigned char>((*text)[i]);
                if ((byte & 0xc0) != 0x80) offsets.push_back(i);
            }
            offsets.push_back(text->size());
            const std::size_t length = offsets.size() - 1;
            if (instruction.sliceSingle) endValue = startValue;
            else if (!instruction.sliceHasEnd) endValue = static_cast<double>(length);
            else if (endValue > static_cast<double>(length))
                endValue = static_cast<double>(length);
            const auto validIndex = [&](double value) {
                return std::isfinite(value) && std::trunc(value) == value && value >= 1 &&
                       value <= static_cast<double>(length);
            };
            if (length == 0 && !instruction.sliceHasStart && !instruction.sliceHasEnd &&
                !instruction.sliceSingle) {
                stack.emplace_back(std::string{});
                break;
            }
            if (length == 0 && !instruction.sliceHasStart && instruction.sliceHasEnd &&
                !instruction.sliceSingle && endValue == 0) {
                stack.emplace_back(std::string{});
                break;
            }
            if (!validIndex(startValue) || !validIndex(endValue) || endValue < startValue)
                runtimeError(instruction, "string slice index out of bounds");
            const std::size_t first = static_cast<std::size_t>(startValue) - 1;
            const std::size_t last = static_cast<std::size_t>(endValue);
            stack.emplace_back(text->substr(offsets[first], offsets[last] - offsets[first]));
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
            const double next = range.single ? current :
                singlePrecision &&
                        (instruction.name.empty() || instruction.name.back() != '%') ?
                static_cast<double>(static_cast<float>(current) +
                                    static_cast<float>(range.step)) :
                current + range.step;
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
            if (numeric(pop(), instruction) == 0) {
                if (state.diagnosticsEnabled) ++state.ifFalse;
                pc = instruction.target;
            } else if (state.diagnosticsEnabled) ++state.ifTrue;
            break;
        case Op::Jump:
            pc = instruction.target;
            break;
        case Op::Push: pushValue(instruction.value); break;
        case Op::PushArrayRef: stack.emplace_back(ArrayReference{instruction.name}); break;
        case Op::Load: {
            bool foundLocal = false;
            for (auto frame = callFrames.rbegin(); frame != callFrames.rend(); ++frame) {
                if (frame->dynamicEvaluation) continue;
                const auto local = frame->locals.find(instruction.name);
                if (local != frame->locals.end()) {
                    pushValue(local->second,
                              !instruction.name.empty() && instruction.name.back() == '%');
                    foundLocal = true;
                }
                break;
            }
            if (foundLocal) break;
            const auto found = variables_.find(instruction.name);
            if (found != variables_.end()) {
                pushValue(found->second,
                          !instruction.name.empty() && instruction.name.back() == '%');
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
            if (!callFrames.empty() &&
                callFrames.back().locals.find(instruction.name) != callFrames.back().locals.end())
                callFrames.back().locals[instruction.name] = std::move(value);
            else
                variables_[instruction.name] = std::move(value);
            break;
        }
        case Op::Negate: {
            const double operand = numeric(pop(), instruction);
            const double value = singlePrecision ?
                static_cast<double>(-static_cast<float>(operand)) : -operand;
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
            if (instruction.op == Op::IntegerDivide || instruction.op == Op::Modulo) {
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
            } else if (singlePrecision) {
                const float lhs = static_cast<float>(a);
                const float rhs = static_cast<float>(b);
                if (instruction.op == Op::Add) result = static_cast<double>(lhs + rhs);
                else if (instruction.op == Op::Subtract) result = static_cast<double>(lhs - rhs);
                else if (instruction.op == Op::Multiply) result = static_cast<double>(lhs * rhs);
                else if (instruction.op == Op::Divide) result = static_cast<double>(lhs / rhs);
                else if (instruction.op == Op::Power)
                    result = static_cast<double>(std::pow(lhs, rhs));
            } else if (instruction.op == Op::Add) result = a + b;
            else if (instruction.op == Op::Subtract) result = a - b;
            else if (instruction.op == Op::Multiply) result = a * b;
            else if (instruction.op == Op::Divide) result = a / b;
            else if (instruction.op == Op::Power) result = std::pow(a, b);
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
        {
            std::ostringstream formatted;
            std::size_t column = printColumn();
            printSeparator(instruction.separator, formatted, column);
            const std::string prefix = formatted.str();
            if (!prefix.empty()) writePrintText(prefix, instruction);
            printColumn() = column;
            std::ostringstream valueText;
            std::size_t valueColumn = 0;
            printValue(pop(), valueText, valueColumn);
            const std::string value = valueText.str();
            if (!value.empty()) writePrintText(value, instruction);
            break;
        }
        case Op::PrintSeparator:
        {
            std::ostringstream formatted;
            std::size_t column = printColumn();
            printSeparator(instruction.separator, formatted, column);
            const std::string separator = formatted.str();
            if (!separator.empty()) writePrintText(separator, instruction);
            printColumn() = column;
            break;
        }
        case Op::PrintEnd:
            writePrintText("\n", instruction);
            if (state.printChannel == 0) output_column_ = 0;
            else if (state.printChannel == 1) state.printChannelColumn = 0;
            state.printChannel = 1;
            break;
        case Op::PrintBegin:
            state.printChannel = resolveChannel(default_output_channel_);
            break;
        case Op::PrintChannel: {
            const double channel = numeric(pop(), instruction);
            if (!std::isfinite(channel) || std::trunc(channel) != channel || channel < 0 ||
                channel > static_cast<double>(std::numeric_limits<std::uint64_t>::max()))
                runtimeError(instruction, "channel handle must be a nonnegative integer");
            state.printChannel = resolveChannel(static_cast<std::uint64_t>(channel));
            break;
        }
        case Op::ChannelDefault: {
            const double channel = numeric(pop(), instruction);
            if (!std::isfinite(channel) || std::trunc(channel) != channel || channel < 0 ||
                channel > static_cast<double>(std::numeric_limits<std::uint64_t>::max()))
                runtimeError(instruction, "channel handle must be a nonnegative integer");
            setDefaultOutputChannel(static_cast<std::uint64_t>(channel));
            break;
        }
        case Op::ChannelRoute: {
            const double destination = numeric(pop(), instruction);
            const double source = numeric(pop(), instruction);
            const auto validHandle = [](double handle) {
                return std::isfinite(handle) && std::trunc(handle) == handle && handle >= 0 &&
                    handle <= static_cast<double>(std::numeric_limits<std::uint64_t>::max());
            };
            if (!validHandle(source) || !validHandle(destination))
                runtimeError(instruction, "channel handles must be nonnegative integers");
            bindChannel(static_cast<std::uint64_t>(source),
                        static_cast<std::uint64_t>(destination));
            break;
        }
        case Op::ChannelSeek: {
            if (!channels_) throw runtime::ServiceError(runtime::ServiceErrorCode::Unavailable,
                                                        "channel service unavailable");
            const double offset = numeric(pop(), instruction);
            const double channel = numeric(pop(), instruction);
            if (!std::isfinite(channel) || std::trunc(channel) != channel || channel < 0 ||
                channel > 9007199254740991.0 || !std::isfinite(offset) ||
                std::trunc(offset) != offset || offset < 0 || offset > 9007199254740991.0)
                runtimeError(instruction, "channel and offset must be nonnegative integers");
            channels_->seek(session_id_, resolveChannel(static_cast<runtime::ChannelId>(channel)),
                            static_cast<std::uint64_t>(offset));
            break;
        }
        case Op::ChannelRead: {
            if (!channels_) throw runtime::ServiceError(runtime::ServiceErrorCode::Unavailable,
                                                        "channel service unavailable");
            const double count = numeric(pop(), instruction);
            const double channel = numeric(pop(), instruction);
            if (!std::isfinite(channel) || std::trunc(channel) != channel || channel < 0 ||
                channel > 9007199254740991.0 || !std::isfinite(count) ||
                std::trunc(count) != count || count < 0 || count > 8192)
                runtimeError(instruction, "invalid channel or byte count (maximum is 8192)");
            Value value(channels_->readBytes(session_id_,
                resolveChannel(static_cast<runtime::ChannelId>(channel)),
                static_cast<std::size_t>(count)));
            if (instruction.targetVariable.empty() || instruction.targetVariable.back() != '$')
                runtimeError(instruction, "binary IN target must be a string variable");
            if (!callFrames.empty() && callFrames.back().locals.count(instruction.targetVariable.get()))
                callFrames.back().locals[instruction.targetVariable.get()] = std::move(value);
            else variables_[instruction.targetVariable.get()] = std::move(value);
            break;
        }
        case Op::ChannelWrite: {
            if (!channels_) throw runtime::ServiceError(runtime::ServiceErrorCode::Unavailable,
                                                        "channel service unavailable");
            const StackEntry entry = popEntry();
            const auto* bytes = std::get_if<std::string>(&entry);
            if (!bytes) runtimeError(instruction, "string byte buffer required");
            const double count = numeric(pop(), instruction);
            const double channel = numeric(pop(), instruction);
            if (!std::isfinite(channel) || std::trunc(channel) != channel || channel < 0 ||
                channel > 9007199254740991.0 || !std::isfinite(count) ||
                std::trunc(count) != count || count < 0 || count > 8192 ||
                count > static_cast<double>(bytes->size()))
                runtimeError(instruction, "invalid channel or byte count");
            channels_->writeBytes(session_id_, resolveChannel(static_cast<runtime::ChannelId>(channel)),
                                  std::string_view(*bytes).substr(0, static_cast<std::size_t>(count)));
            break;
        }
        case Op::Input: {
            std::string prompt;
            runtime::ChannelId promptChannel = instruction.expectsValue ?
                static_cast<runtime::ChannelId>(instruction.target) : 1;
            runtime::ChannelId inputChannel = 0;
            if (instruction.channelArgument) {
                const double channel = numeric(pop(), instruction);
                if (!std::isfinite(channel) || std::trunc(channel) != channel || channel < 0 ||
                    channel > 9007199254740991.0)
                    runtimeError(instruction, "invalid INPUT channel");
                inputChannel = static_cast<runtime::ChannelId>(channel);
            }
            if (instruction.expectsValue) {
                const Value value = pop();
                const auto* text = std::get_if<std::string>(&value);
                if (!text) runtimeError(instruction, "INPUT prompt must be a string");
                prompt = *text;
            }
            if (instruction.clearTarget) {
                const double channel = numeric(pop(), instruction);
                if (!std::isfinite(channel) || std::trunc(channel) != channel || channel < 0 ||
                    channel > 9007199254740991.0)
                    runtimeError(instruction, "invalid prompt channel");
                promptChannel = static_cast<runtime::ChannelId>(channel);
            }
            inputChannel = resolveChannel(inputChannel);
            if (instruction.expectsValue) {
                state.printChannel = resolveChannel(promptChannel);
                writePrintText(prompt, instruction);
            }
            if (inputChannel == 0) {
                if (!ui_) throw runtime::ServiceError(runtime::ServiceErrorCode::Unavailable,
                                                       "keyboard input service unavailable");
                state.pendingInputVariable = instruction.name;
                state.pendingUiKind = ui::RequestKind::Input;
                state.pendingUiRequest = ui_->beginInput(session_id_, {});
                return ExecutionStatus::Suspended;
            }
            if (inputChannel == 1)
                runtimeError(instruction, "selected channel does not support input");
            if (!channels_)
                throw runtime::ServiceError(runtime::ServiceErrorCode::Unavailable,
                                            "channel service unavailable");
            const auto line = channels_->readLine(session_id_, inputChannel);
            if (!line) runtimeError(instruction, "end of file during INPUT");
            storeInputValue(instruction.name, *line, instruction);
            break;
        }
        }
        if (state.diagnosticsEnabled) {
            state.stackHighWater = std::max(state.stackHighWater, stack.size());
            state.frameHighWater = std::max(state.frameHighWater, callFrames.size());
        }
    }
    return ExecutionStatus::Completed;
}

bool Interpreter::debugAttach() noexcept {
    debuggerAttached_ = true;
    if (execution_) {
        execution_->debuggerAttached = true;
        execution_->debugPaused = true;
    } else {
        debugPauseRequested_ = true;
    }
    return true;
}

bool Interpreter::debugSetBreakpoint(int line, bool enabled) {
    if (line < 1 || line > 32767) return false;
    if (!execution_) {
        auto found = std::find_if(pendingBreakpoints_.begin(), pendingBreakpoints_.end(),
            [line](const auto& item) { return item.first == line; });
        if (found == pendingBreakpoints_.end()) pendingBreakpoints_.emplace_back(line, enabled);
        else found->second = enabled;
        if (enabled) debugPauseRequested_ = false;
        return true;
    }
    const bool numbered = std::any_of(execution_->code.begin(), execution_->code.end(),
        [](const Instruction& instruction) {
            return instruction.statementStart && instruction.basicLine != 0;
        });
    bool matched = false;
    for (std::size_t address = 0; address < execution_->code.size(); ++address) {
        const Instruction& instruction = execution_->code[address];
        if (!instruction.statementStart ||
            (numbered ? instruction.basicLine != line : instruction.line != line)) continue;
        matched = true;
        if (enabled) execution_->breakpoints.insert(address);
        else execution_->breakpoints.erase(address);
        break;
    }
    return matched;
}

bool Interpreter::debugResume(runtime::DebugResumeMode mode) {
    if (!execution_ || !execution_->debuggerAttached || !execution_->debugPaused)
        return false;
    execution_->debugPaused = false;
    debugPauseRequested_ = false;
    execution_->debugMode = mode;
    execution_->stepCallDepth = execution_->callFrames.size();
    execution_->stepStarted = false;
    execution_->skipBreakpointAddress = execution_->pc;
    return true;
}

bool Interpreter::debugPause() noexcept {
    if (!execution_ || !debuggerAttached_) return false;
    execution_->debugPaused = true;
    return true;
}

void Interpreter::debugSetDiagnostics(bool enabled, bool trace) noexcept {
    diagnosticsEnabled_ = enabled;
    traceEnabled_ = enabled && trace;
    if (execution_) {
        execution_->diagnosticsEnabled = diagnosticsEnabled_;
        execution_->traceEnabled = traceEnabled_;
    }
}

void Interpreter::debugResetDiagnostics() noexcept {
    if (!execution_) return;
    execution_->bytecodes = 0;
    execution_->procedureCalls = 0;
    execution_->ifTrue = 0;
    execution_->ifFalse = 0;
    execution_->stackHighWater = 0;
    execution_->frameHighWater = 0;
    execution_->trace.clear();
}

bool Interpreter::channelReadReady() const {
    if (!execution_ || !execution_->pendingChannelRead || !channels_) return false;
    try {
        return channels_->lineAvailable(session_id_, *execution_->pendingChannelRead);
    } catch (...) {
        return true;
    }
}

std::optional<runtime::DebugSnapshot> Interpreter::debugInspect() const {
    if (!debuggerAttached_) return std::nullopt;
    runtime::DebugSnapshot result;
    result.attached = true;
    result.paused = execution_ && execution_->debugPaused;
    for (const auto& [name, value] : variables_) result.globals.emplace(name, value);
    if (!execution_) return result;
    const detail::VMExecutionState& state = *execution_;
    result.bytecodes = state.bytecodes;
    result.userCalls = state.procedureCalls;
    result.ifTrue = state.ifTrue;
    result.ifFalse = state.ifFalse;
    result.trace = state.trace;
    if (memory_) result.freeMemory = memory_->freeBytes();
    result.bytecodeStorageBytes = detail::estimateInstructionSequenceBytes(state.code);
    result.stackHighWater = state.stackHighWater;
    result.frameHighWater = state.frameHighWater;
    const auto valueStorageBytes = [](const Value& value) {
        const auto* text = std::get_if<std::string>(&value);
        return text ? text->capacity() + 1 : std::size_t{0};
    };
    result.globalStorageBytes = sizeof(variables_) +
        variables_.bucket_count() * sizeof(void*);
    for (const auto& entry : variables_)
        result.globalStorageBytes += sizeof(decltype(variables_)::value_type) +
            entry.first.capacity() + 1 + valueStorageBytes(entry.second);
    for (const auto& frame : state.callFrames) {
        result.localStorageBytes += sizeof(frame.locals) +
            frame.locals.bucket_count() * sizeof(void*);
        for (const auto& entry : frame.locals)
            result.localStorageBytes += sizeof(decltype(frame.locals)::value_type) +
                entry.first.capacity() + 1 + valueStorageBytes(entry.second);
    }
    result.arrayStorageBytes = sizeof(state.arrays) +
        state.arrays.bucket_count() * sizeof(void*);
    for (const auto& [name, array] : state.arrays) {
        result.arrayStorageBytes += sizeof(decltype(state.arrays)::value_type) +
            name.capacity() + 1 + array.lengths.capacity() * sizeof(std::size_t) +
            array.values.capacity() * sizeof(Value);
        for (const auto& value : array.values)
            result.arrayStorageBytes += valueStorageBytes(value);
    }
    const std::size_t address = std::min(state.pc, state.code.size());
    result.location.instruction = address;
    if (!state.callFrames.empty())
        result.location.procedure = state.callFrames.back().name;
    if (address < state.code.size()) {
        const detail::Instruction& at = state.code[address];
        result.location.sourceLine = at.line;
        result.location.column = at.column;
        result.location.basicLine = at.basicLine;
    } else if (address != 0 && address <= state.code.size()) {
        const detail::Instruction& at = state.code[address - 1];
        result.location.sourceLine = at.line;
        result.location.column = at.column;
        result.location.basicLine = at.basicLine;
    }
    for (const auto& frame : state.callFrames) {
        runtime::DebugFrameInfo view;
        view.name = frame.name;
        view.function = frame.function;
        for (const auto& [name, value] : frame.locals) view.locals.emplace(name, value);
        for (const auto& [localName, arrayName] : frame.arrayAliases) {
            const auto array = state.arrays.find(arrayName);
            if (array != state.arrays.end())
                view.arrays.emplace_back(localName, array->second.lengths);
        }
        if (frame.returnAddress > 0 && frame.returnAddress <= state.code.size()) {
            view.returnLocation.instruction = frame.callInstruction;
            view.returnLocation.sourceLine = frame.callSourceLine;
            view.returnLocation.column = frame.callSourceColumn;
            view.returnLocation.basicLine = frame.callBasicLine;
            view.returnLocation.procedure = frame.name;
        }
        result.frames.push_back(std::move(view));
    }
    for (const auto& [name, array] : state.arrays)
        result.arrays.emplace_back(name, array.lengths);
    for (const std::string& expression : debugWatches_) {
        runtime::DebugWatchInfo watch;
        watch.expression = expression;
        try {
            watch.value = debugEvaluate(expression);
            if (!watch.value) watch.error = "task is not paused";
        } catch (const std::exception& error) {
            watch.error = error.what();
        }
        result.watches.push_back(std::move(watch));
    }
    for (std::size_t address : state.breakpoints) {
        if (address >= state.code.size()) continue;
        const auto& at = state.code[address];
        runtime::DebugBreakpointInfo breakpoint;
        breakpoint.requestedLine = at.basicLine != 0 ? at.basicLine : at.line;
        breakpoint.location = runtime::DebugLocation{address, at.line, at.column,
                                                       at.basicLine, {}};
        result.breakpoints.push_back(std::move(breakpoint));
    }
    return result;
}

std::vector<runtime::DebugBreakpointInfo> Interpreter::debugBreakpoints() const {
    std::vector<runtime::DebugBreakpointInfo> result;
    if (!execution_) {
        result.reserve(pendingBreakpoints_.size());
        for (const auto& [line, enabled] : pendingBreakpoints_)
            result.push_back({line, enabled, std::nullopt});
        return result;
    }
    result.reserve(execution_->breakpoints.size());
    for (std::size_t address : execution_->breakpoints) {
        if (address >= execution_->code.size()) continue;
        const auto& at = execution_->code[address];
        result.push_back({at.basicLine != 0 ? at.basicLine : at.line, true,
                          runtime::DebugLocation{address, at.line, at.column,
                                                at.basicLine, {}}});
    }
    return result;
}

std::optional<runtime::DebugValue> Interpreter::debugEvaluate(
    const std::string& expression) const {
    if (!execution_ || !execution_->debuggerAttached || !execution_->debugPaused)
        return std::nullopt;
    const auto code = detail::compileExpression(expression, execution_->code);
    std::vector<runtime::DebugValue> values;
    const auto popValue = [&]() -> runtime::DebugValue {
        if (values.empty()) throw std::runtime_error("debug EVAL stack underflow");
        auto value = std::move(values.back());
        values.pop_back();
        return value;
    };
    const auto number = [](const runtime::DebugValue& value) {
        const auto* result = std::get_if<double>(&value);
        if (!result) throw std::runtime_error("debug EVAL requires a numeric value");
        return *result;
    };
    for (const detail::Instruction& instruction : code) {
        using detail::Op;
        switch (instruction.op) {
        case Op::Push:
            if (const auto* literal = std::get_if<double>(&instruction.value)) {
                const double rounded = execution_->code.numericPrecision ==
                    detail::NumericPrecision::Single ?
                    static_cast<double>(static_cast<float>(*literal)) : *literal;
                values.emplace_back(rounded);
            } else values.push_back(instruction.value);
            break;
        case Op::Load: {
            bool found = false;
            for (auto frame = execution_->callFrames.rbegin();
                 frame != execution_->callFrames.rend(); ++frame) {
                if (frame->dynamicEvaluation) continue;
                const auto local = frame->locals.find(instruction.name);
                if (local != frame->locals.end()) {
                    values.push_back(local->second);
                    found = true;
                    break;
                }
            }
            if (!found) {
                const auto global = variables_.find(instruction.name);
                if (global != variables_.end()) values.push_back(global->second);
                else {
                    const auto constant = execution_->code.constants.find(instruction.name);
                    if (constant == execution_->code.constants.end())
                        throw std::runtime_error("unknown variable in debug EVAL: " + instruction.name);
                    values.push_back(constant->second);
                }
            }
            break;
        }
        case Op::ArrayGet: {
            std::string name = instruction.name;
            for (auto frame = execution_->callFrames.rbegin();
                 frame != execution_->callFrames.rend(); ++frame) {
                const auto alias = frame->arrayAliases.find(name);
                if (alias != frame->arrayAliases.end()) { name = alias->second; break; }
            }
            const auto array = execution_->arrays.find(name);
            if (array == execution_->arrays.end())
                throw std::runtime_error("array is not available in debug EVAL");
            std::size_t offset = 0, stride = 1;
            std::vector<std::size_t> indexes(instruction.rank);
            for (std::size_t i = instruction.rank; i-- > 0;) {
                const double index = number(popValue());
                if (!std::isfinite(index) || std::trunc(index) != index || index < 0 ||
                    index >= static_cast<double>(array->second.lengths.at(i)))
                    throw std::runtime_error("debug EVAL array index out of bounds");
                indexes[i] = static_cast<std::size_t>(index);
            }
            for (std::size_t i = instruction.rank; i-- > 0;) {
                offset += indexes[i] * stride;
                stride *= array->second.lengths[i];
            }
            values.push_back(array->second.values.at(offset));
            break;
        }
        case Op::Negate: values.emplace_back(-number(popValue())); break;
        case Op::Not: values.emplace_back(number(popValue()) == 0 ? 1.0 : 0.0); break;
        case Op::Add: case Op::Subtract: case Op::Multiply: case Op::Divide:
        case Op::IntegerDivide: case Op::Modulo: case Op::Power:
        case Op::Equal: case Op::NotEqual: case Op::Less: case Op::LessEqual:
        case Op::Greater: case Op::GreaterEqual: case Op::And: case Op::Or: case Op::Xor: {
            const auto right = popValue();
            const auto left = popValue();
            if (instruction.op == Op::Add && std::holds_alternative<std::string>(left) &&
                std::holds_alternative<std::string>(right)) {
                values.emplace_back(std::get<std::string>(left) + std::get<std::string>(right));
                break;
            }
            if (instruction.op == Op::Equal || instruction.op == Op::NotEqual) {
                if (left.index() != right.index()) throw std::runtime_error("debug EVAL type mismatch");
                bool equal = left == right;
                values.emplace_back((instruction.op == Op::Equal ? equal : !equal) ? 1.0 : 0.0);
                break;
            }
            if (instruction.op == Op::Less || instruction.op == Op::LessEqual ||
                instruction.op == Op::Greater || instruction.op == Op::GreaterEqual) {
                if (std::holds_alternative<std::string>(left) ||
                    std::holds_alternative<std::string>(right))
                    throw std::runtime_error("debug EVAL relational comparison requires numbers");
                const double a = number(left), b = number(right);
                const bool result = instruction.op == Op::Less ? a < b :
                    instruction.op == Op::LessEqual ? a <= b :
                    instruction.op == Op::Greater ? a > b : a >= b;
                values.emplace_back(result ? 1.0 : 0.0);
                break;
            }
            const double a = number(left), b = number(right);
            if (instruction.op == Op::Power && a == 0 && b < 0)
                throw std::runtime_error("debug EVAL division by zero");
            if (instruction.op == Op::Power && a < 0 && std::trunc(b) != b)
                throw std::runtime_error("debug EVAL invalid exponent");
            double result = 0;
            switch (instruction.op) {
            case Op::Add: result = a + b; break;
            case Op::Subtract: result = a - b; break;
            case Op::Multiply: result = a * b; break;
            case Op::Divide: if (b == 0) throw std::runtime_error("debug EVAL division by zero"); result = a / b; break;
            case Op::IntegerDivide: case Op::Modulo: {
                if (b == 0) throw std::runtime_error("debug EVAL division by zero");
                if (std::trunc(a) != a || std::trunc(b) != b ||
                    a < static_cast<double>(std::numeric_limits<std::int32_t>::min()) ||
                    a > static_cast<double>(std::numeric_limits<std::int32_t>::max()) ||
                    b < static_cast<double>(std::numeric_limits<std::int32_t>::min()) ||
                    b > static_cast<double>(std::numeric_limits<std::int32_t>::max()))
                    throw std::runtime_error("debug EVAL integer operation requires Int32 operands");
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
                        throw std::runtime_error("debug EVAL integer result out of range");
                    result = static_cast<double>(quotient);
                } else result = static_cast<double>(remainder);
                break;
            }
            case Op::Power: result = std::pow(a, b); break;
            case Op::And: result = (a != 0 && b != 0) ? 1 : 0; break;
            case Op::Or: result = (a != 0 || b != 0) ? 1 : 0; break;
            case Op::Xor: result = ((a != 0) != (b != 0)) ? 1 : 0; break;
            default: break;
            }
            if (!std::isfinite(result)) throw std::runtime_error("debug EVAL numeric overflow");
            if (execution_->code.numericPrecision == detail::NumericPrecision::Single &&
                instruction.op != Op::IntegerDivide && instruction.op != Op::Modulo)
                result = static_cast<double>(static_cast<float>(result));
            values.emplace_back(result);
            break;
        }
        default:
            throw std::runtime_error("debug EVAL only permits literals, reads, and pure operators");
        }
    }
    if (values.size() != 1) throw std::runtime_error("debug EVAL did not produce one value");
    return values.back();
}

bool Interpreter::debugAddWatch(std::string expression) {
    if (!execution_ || !execution_->debugPaused || expression.empty()) return false;
    (void)debugEvaluate(expression);
    if (std::find(debugWatches_.begin(), debugWatches_.end(), expression) == debugWatches_.end())
        debugWatches_.push_back(std::move(expression));
    return true;
}

bool Interpreter::debugRemoveWatch(const std::string& expression) {
    const auto found = std::find(debugWatches_.begin(), debugWatches_.end(), expression);
    if (found == debugWatches_.end()) return false;
    debugWatches_.erase(found);
    return true;
}


} // namespace ql32
