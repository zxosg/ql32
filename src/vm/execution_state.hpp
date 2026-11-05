#pragma once

#include "../language/bytecode.hpp"
#include "ql32/ui/service.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace ql32::detail {

struct ForRange { double start = 0; double end = 0; double step = 1; bool single = false; };
struct ForState { std::vector<ForRange> ranges; std::size_t item = 0; };
struct RuntimeFrame {
    std::size_t returnAddress = 0;
    std::size_t callInstruction = 0;
    int callSourceLine = 0;
    int callSourceColumn = 0;
    int callBasicLine = 0;
    bool function = false;
    std::string name;
    bool dynamicEvaluation = false;
    bool stringifyResult = false;
    std::size_t dynamicCodeStart = 0;
    std::unordered_map<std::string, Value> locals;
    std::unordered_map<std::string, std::string> arrayAliases;
};
struct ArrayReference { std::string name; };
using StackEntry = std::variant<double, std::string, ArrayReference>;
struct RuntimeArray { std::vector<std::size_t> lengths; std::vector<Value> values; };
struct SelectState { double value = 0; bool matched = false; };
struct DialogBuilder {
    std::string title;
    std::vector<ui::DialogField> fields;
    std::vector<ui::FieldValue> results;
    std::string acceptLabel = "OK";
    std::string cancelLabel = "Cancel";
    bool hasResults = false;
};
struct MenuBuilder {
    std::string title;
    std::vector<ui::MenuItem> items;
    std::uint32_t selectedCommand = 0;
    bool hasResult = false;
};

struct VMExecutionState {
    InstructionSequence code;
    std::vector<StackEntry> stack;
    std::unordered_map<std::size_t, ForState> forStates;
    std::vector<RuntimeFrame> callFrames;
    std::unordered_map<std::string, RuntimeArray> arrays;
    std::vector<SelectState> selectStates;
    std::size_t pc = 0;
    std::uint64_t printChannel = 1;
    std::size_t printChannelColumn = 0;
    std::optional<ui::RequestId> pendingUiRequest;
    std::optional<runtime::ChannelId> pendingChannelRead;
    ui::RequestKind pendingUiKind = ui::RequestKind::Alert;
    std::uint64_t pendingUiBuilderHandle = 0;
    std::string pendingInputVariable;
    bool debuggerAttached = false;
    bool debugPaused = false;
    runtime::DebugResumeMode debugMode = runtime::DebugResumeMode::Continue;
    std::set<std::size_t> breakpoints;
    std::size_t skipBreakpointAddress = static_cast<std::size_t>(-1);
    std::size_t stepCallDepth = 0;
    bool stepStarted = false;
    bool diagnosticsEnabled = false;
    bool traceEnabled = false;
    std::uint64_t bytecodes = 0;
    std::uint64_t procedureCalls = 0;
    std::uint64_t ifTrue = 0;
    std::uint64_t ifFalse = 0;
    std::size_t stackHighWater = 0;
    std::size_t frameHighWater = 0;
    std::vector<std::string> trace;
};

struct UISessionState {
    std::uint64_t nextUiBuilderHandle = 1;
    std::unordered_map<std::uint64_t, DialogBuilder> dialogs;
    std::unordered_map<std::uint64_t, MenuBuilder> menus;
};

} // namespace ql32::detail
