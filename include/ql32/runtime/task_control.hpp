#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string>
#include <utility>
#include <unordered_map>
#include <variant>
#include <vector>
#include "ql32/runtime/channel.hpp"
#include "ql32/runtime/error.hpp"

namespace ql32::runtime {

using TaskId = std::uint64_t;
inline constexpr std::uint32_t maximumTaskPriority = 2147483647U;
inline constexpr std::size_t maximumInstructionBudget = 65536;

enum class TaskState {
    Runnable,
    WaitingUi,
    WaitingInput,
    WaitingChannel,
    WaitingTask,
    WaitingTime,
    DebugPaused,
    Completed,
    Failed,
    Cancelled
};

enum TaskStartFlag : std::uint32_t {
    TaskStartNone = 0,
    TaskStartWait = 1U << 0
};

enum class TaskResultField : std::uint32_t {
    Status = 1,
    Channels = 2,
    ErrorCode = 3,
    ErrorDetail = 4,
    Kind = 5,
    Priority = 6,
    ResumePriority = 7,
    CanvasOriginX = 8,
    CanvasOriginY = 9,
    CanvasWidth = 10,
    CanvasHeight = 11
};

enum class TaskListField : std::uint32_t {
    Count = 0,
    Handle = 1
};

enum class DebugResumeMode { Continue, StepInto, StepOver };
using DebugValue = std::variant<double, std::string>;

struct DebugLocation {
    std::size_t instruction = 0;
    int sourceLine = 0;
    int column = 0;
    int basicLine = 0;
    std::string procedure;
};

struct DebugFrameInfo {
    std::string name;
    bool function = false;
    DebugLocation returnLocation;
    std::unordered_map<std::string, DebugValue> locals;
    std::vector<std::pair<std::string, std::vector<std::size_t>>> arrays;
};

struct DebugBreakpointInfo {
    int requestedLine = 0;
    bool enabled = true;
    std::optional<DebugLocation> location;
};

struct DebugWatchInfo {
    std::string expression;
    std::optional<DebugValue> value;
    std::string error;
};

struct DebugSnapshot {
    bool attached = false;
    bool paused = false;
    DebugLocation location;
    std::vector<DebugFrameInfo> frames;
    std::unordered_map<std::string, DebugValue> globals;
    std::uint64_t bytecodes = 0;
    std::uint64_t userCalls = 0;
    std::uint64_t ifTrue = 0;
    std::uint64_t ifFalse = 0;
    std::optional<std::uint64_t> freeMemory;
    std::vector<std::string> trace;
    std::vector<DebugWatchInfo> watches;
    std::vector<std::pair<std::string, std::vector<std::size_t>>> arrays;
    std::vector<DebugBreakpointInfo> breakpoints;
};

struct ManagedTaskInfo {
    TaskId id = 0;
    TaskState state = TaskState::Runnable;
    bool supervisor = false;
    bool interactive = false;
    std::uint32_t priority = 4;
    std::uint32_t resumePriority = 4;
    bool paused = false;
    std::string error;
    ErrorCode errorCode = ErrorCode::None;
    std::string errorDetail;
    std::size_t openChannels = 0;
    std::int32_t canvasOriginX = 0;
    std::int32_t canvasOriginY = 0;
    std::uint32_t canvasWidth = 0;
    std::uint32_t canvasHeight = 0;
    std::int32_t canvasOffsetX = 0;
    std::int32_t canvasOffsetY = 0;
};

// Control surface available to the interactive BASIC supervisor.
class TaskControlService {
public:
    virtual ~TaskControlService() = default;
    virtual TaskId startTask(std::string source, std::ostream& output,
                             TaskId caller, std::uint32_t flags) = 0;
    virtual bool killTask(TaskId id) = 0;
    virtual bool logon(TaskId id) = 0;
    virtual TaskId logoff() = 0;
    virtual TaskId focusedTask() const = 0;
    virtual bool suspendTask(TaskId id) = 0;
    virtual bool resumeTask(TaskId id) = 0;
    virtual std::size_t instructionBudget() const = 0;
    virtual bool setInstructionBudget(std::size_t budget) = 0;
    virtual bool setTaskPriority(TaskId id, std::uint32_t priority) = 0;
    virtual std::vector<ManagedTaskInfo> listTasks() const = 0;
    virtual std::vector<ChannelInfo> listChannels(TaskId id) const = 0;
    virtual bool setTaskViewport(TaskId, std::int32_t, std::int32_t,
                                 std::uint32_t, std::uint32_t,
                                 std::int32_t = 0, std::int32_t = 0) { return false; }
    virtual bool debugAttach(TaskId) { return false; }
    virtual bool debugSetBreakpoint(TaskId, int, bool) { return false; }
    virtual bool debugResume(TaskId, DebugResumeMode) { return false; }
    virtual bool debugPause(TaskId) { return false; }
    virtual bool debugSetDiagnostics(TaskId, bool, bool) { return false; }
    virtual bool debugResetDiagnostics(TaskId) { return false; }
    virtual std::optional<DebugSnapshot> debugInspect(TaskId) const { return std::nullopt; }
    virtual std::vector<DebugBreakpointInfo> debugBreakpoints(TaskId) const { return {}; }
    virtual std::optional<DebugValue> debugEvaluate(TaskId, const std::string&) const {
        return std::nullopt;
    }
    virtual bool debugAddWatch(TaskId, std::string) { return false; }
    virtual bool debugRemoveWatch(TaskId, const std::string&) { return false; }
};

inline const char* taskStateName(TaskState state) noexcept {
    switch (state) {
    case TaskState::Runnable: return "RUNNABLE";
    case TaskState::WaitingUi: return "WAITING_UI";
    case TaskState::WaitingInput: return "WAITING_INPUT";
    case TaskState::WaitingChannel: return "WAITING_CHANNEL";
    case TaskState::WaitingTask: return "WAITING_TASK";
    case TaskState::WaitingTime: return "WAITING_TIME";
    case TaskState::DebugPaused: return "DEBUG_PAUSED";
    case TaskState::Completed: return "COMPLETED";
    case TaskState::Failed: return "FAILED";
    case TaskState::Cancelled: return "CANCELLED";
    }
    return "UNKNOWN";
}

} // namespace ql32::runtime
