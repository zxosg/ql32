#include "ql32/vm/scheduler.hpp"

#include <stdexcept>
#include <utility>

namespace ql32::vm {

namespace {

runtime::TaskState taskState(TaskStatus status) {
    switch (status) {
    case TaskStatus::Runnable: return runtime::TaskState::Runnable;
    case TaskStatus::WaitingUi: return runtime::TaskState::WaitingUi;
    case TaskStatus::WaitingInput: return runtime::TaskState::WaitingInput;
    case TaskStatus::WaitingChannel: return runtime::TaskState::WaitingChannel;
    case TaskStatus::WaitingTask: return runtime::TaskState::WaitingTask;
    case TaskStatus::DebugPaused: return runtime::TaskState::DebugPaused;
    case TaskStatus::Completed: return runtime::TaskState::Completed;
    case TaskStatus::Failed: return runtime::TaskState::Failed;
    case TaskStatus::Cancelled: return runtime::TaskState::Cancelled;
    }
    return runtime::TaskState::Failed;
}

class SchedulerTaskControl final : public runtime::TaskControlService {
public:
    explicit SchedulerTaskControl(SessionScheduler& scheduler) : scheduler_(scheduler) {}

    runtime::TaskId startTask(std::string source, std::ostream& output,
                              runtime::TaskId caller, std::uint32_t flags) override {
        if ((flags & ~static_cast<std::uint32_t>(runtime::TaskStartWait)) != 0)
            throw runtime::Error(runtime::ErrorCode::InvalidArgument,
                                 "unknown TASK option bits");
        if ((flags & runtime::TaskStartWait) != 0)
            return scheduler_.launchAndWait(std::move(source), output, caller);
        return scheduler_.launchWorker(std::move(source), output);
    }

    bool killTask(runtime::TaskId id) override {
        return scheduler_.kill(id);
    }

    bool logon(runtime::TaskId id) override { return scheduler_.logon(id); }
    runtime::TaskId logoff() override { return scheduler_.logoff(); }
    runtime::TaskId focusedTask() const override { return scheduler_.focusedTask(); }
    bool suspendTask(runtime::TaskId id) override {
        return scheduler_.suspendTask(id);
    }
    bool resumeTask(runtime::TaskId id) override {
        return scheduler_.resumeTask(id);
    }
    std::size_t instructionBudget() const override {
        return scheduler_.instructionBudget();
    }
    bool setInstructionBudget(std::size_t budget) override {
        return scheduler_.setInstructionBudget(budget);
    }
    bool setTaskPriority(runtime::TaskId id, std::uint32_t priority) override {
        return scheduler_.setTaskPriority(id, priority);
    }
    bool setTaskViewport(runtime::TaskId id, std::int32_t x, std::int32_t y,
                         std::uint32_t width, std::uint32_t height) override {
        return scheduler_.setTaskViewport(id, {x, y, width, height});
    }

    std::vector<runtime::ChannelInfo> listChannels(runtime::TaskId id) const override {
        return scheduler_.taskChannels(id);
    }
    bool debugAttach(runtime::TaskId id) override { return scheduler_.debugAttach(id); }
    bool debugSetBreakpoint(runtime::TaskId id, int line, bool enabled) override {
        return scheduler_.debugSetBreakpoint(id, line, enabled);
    }
    bool debugResume(runtime::TaskId id, runtime::DebugResumeMode mode) override {
        return scheduler_.debugResume(id, mode);
    }
    bool debugPause(runtime::TaskId id) override { return scheduler_.debugPause(id); }
    bool debugSetDiagnostics(runtime::TaskId id, bool enabled, bool trace) override {
        return scheduler_.debugSetDiagnostics(id, enabled, trace);
    }
    bool debugResetDiagnostics(runtime::TaskId id) override {
        return scheduler_.debugResetDiagnostics(id);
    }
    std::optional<runtime::DebugSnapshot> debugInspect(runtime::TaskId id) const override {
        return scheduler_.debugInspect(id);
    }
    std::vector<runtime::DebugBreakpointInfo> debugBreakpoints(runtime::TaskId id) const override {
        return scheduler_.debugBreakpoints(id);
    }
    std::optional<runtime::DebugValue> debugEvaluate(
        runtime::TaskId id, const std::string& expression) const override {
        return scheduler_.debugEvaluate(id, expression);
    }
    bool debugAddWatch(runtime::TaskId id, std::string expression) override {
        return scheduler_.debugAddWatch(id, std::move(expression));
    }
    bool debugRemoveWatch(runtime::TaskId id, const std::string& expression) override {
        return scheduler_.debugRemoveWatch(id, expression);
    }

    std::vector<runtime::ManagedTaskInfo> listTasks() const override {
        std::vector<runtime::ManagedTaskInfo> result;
        for (const auto& info : scheduler_.tasks()) {
            result.push_back({info.id, taskState(info.status),
                              info.supervisor, info.interactive,
                              info.priority, info.resumePriority,
                              info.paused,
                              info.error, info.errorCode, info.errorDetail,
                              info.openChannels, info.viewport.originX,
                              info.viewport.originY, info.viewport.canvasWidth,
                              info.viewport.canvasHeight});
        }
        return result;
    }

private:
    SessionScheduler& scheduler_;
};

} // namespace

struct SessionScheduler::Task {
    TaskId id = 0;
    std::string source;
    std::ostream* output = nullptr;
    std::unique_ptr<Interpreter> interpreter;
    TaskStatus status = TaskStatus::Runnable;
    std::string error;
    runtime::ErrorCode errorCode = runtime::ErrorCode::None;
    bool errorPending = false;
    std::string errorDetail;
    std::string pendingInteractiveTail;
    TaskId waitingChildId = 0;
    TaskId waitingParentId = 0;
    bool started = false;
    bool interactive = false;
    bool supervisor = false;
    bool interactiveSubmission = false;
    bool hasPendingSource = true;
    bool paused = false;
    std::uint32_t priority = 4;
    std::uint32_t resumePriority = 4;
    std::int64_t schedulerCredit = 0;
    std::uint32_t schedulerLastPriority = 0;
    bool schedulerWasEligible = false;
    bool schedulerEligibleThisTick = false;

    void releaseSource() noexcept {
        std::string{}.swap(source);
    }
};

SessionScheduler::SessionScheduler(Services services, std::size_t instructionBudget)
    : services_(std::move(services)), instructionBudget_(instructionBudget) {
    if (instructionBudget_ == 0 || instructionBudget_ > MaximumInstructionBudget)
        throw std::invalid_argument("scheduler instruction budget must be in 1..65536");
    taskControl_ = std::make_shared<SchedulerTaskControl>(*this);
}

SessionScheduler::~SessionScheduler() = default;

void SessionScheduler::resetScheduleCredits() noexcept {
    for (auto& task : tasks_) task->schedulerCredit = 0;
}

void SessionScheduler::removeFromSchedule(Task& task) noexcept {
    if (!task.schedulerWasEligible) return;
    resetScheduleCredits();
    task.schedulerWasEligible = false;
    task.schedulerEligibleThisTick = false;
}

TaskId SessionScheduler::launch(std::string source, std::ostream& output) {
    if (nextId_ == 0) throw std::overflow_error("scheduler task IDs exhausted");
    auto task = std::make_unique<Task>();
    task->id = nextId_++;
    task->source = std::move(source);
    task->output = &output;
    task->interpreter = std::make_unique<Interpreter>(
        services_.storage, services_.channels, services_.ui, services_.graphics,
        services_.network, services_.memory, taskControl_, task->id,
        services_.systemControl, services_.cpuFrequency);
    const TaskId id = task->id;
    tasks_.push_back(std::move(task));
    return id;
}

TaskId SessionScheduler::launchWorker(std::string source, std::ostream& output) {
    if (nextId_ == 0) throw std::overflow_error("scheduler task IDs exhausted");
    auto task = std::make_unique<Task>();
    task->id = nextId_++;
    task->source = std::move(source);
    task->output = &output;
    task->interpreter = std::make_unique<Interpreter>(
        services_.storage, services_.channels, services_.ui, services_.graphics,
        services_.network, services_.memory, taskControl_, task->id,
        services_.systemControl, services_.cpuFrequency);
    task->interactive = true;
    task->hasPendingSource = true;
    const TaskId id = task->id;
    tasks_.push_back(std::move(task));
    return id;
}

TaskId SessionScheduler::launchAndWait(std::string source, std::ostream& output,
                                       TaskId parentId) {
    Task* parent = nullptr;
    for (auto& task : tasks_)
        if (task->id == parentId) parent = task.get();
    if (!parent || !parent->interpreter || parent->status == TaskStatus::Completed ||
        parent->status == TaskStatus::Failed || parent->status == TaskStatus::Cancelled)
        throw runtime::Error(runtime::ErrorCode::InvalidTaskState,
                             "caller cannot wait for a task");
    if (parent->waitingChildId != 0)
        throw runtime::Error(runtime::ErrorCode::InvalidTaskState,
                             "caller is already waiting for a child task");
    const TaskId childId = launch(std::move(source), output);
    Task* child = tasks_.back().get();
    parent->waitingChildId = childId;
    child->waitingParentId = parentId;
    child->priority = parent->priority;
    child->resumePriority = parent->resumePriority;
    focusedTaskId_ = childId;
    return childId;
}

TaskId SessionScheduler::launchInteractive(std::ostream& output,
                                           std::string initialProgram) {
    if (nextId_ == 0) throw std::overflow_error("scheduler task IDs exhausted");
    auto task = std::make_unique<Task>();
    task->id = nextId_++;
    task->source = std::move(initialProgram);
    task->output = &output;
    task->interpreter = std::make_unique<Interpreter>(
        services_.storage, services_.channels, services_.ui, services_.graphics,
        services_.network, services_.memory, taskControl_, task->id,
        services_.systemControl, services_.cpuFrequency);
    task->interactive = true;
    task->supervisor = true;
    task->hasPendingSource = !task->source.empty();
    task->interactiveSubmission = false;
    task->status = task->hasPendingSource ? TaskStatus::Runnable : TaskStatus::WaitingInput;
    const TaskId id = task->id;
    supervisorId_ = id;
    if (focusedTaskId_ == 0) focusedTaskId_ = id;
    tasks_.push_back(std::move(task));
    return id;
}

bool SessionScheduler::submit(TaskId id, std::string source) {
    for (auto& current : tasks_) {
        if (current->id != id || !current->interactive ||
            current->status != TaskStatus::WaitingInput || current->hasPendingSource)
            continue;
        current->source = std::move(source);
        current->interactiveSubmission = true;
        current->hasPendingSource = true;
        current->error.clear();
        current->status = TaskStatus::Runnable;
        return true;
    }
    return false;
}

bool SessionScheduler::logon(TaskId id) {
    for (const auto& current : tasks_) {
        if (current->id != id || current->supervisor || !current->interactive ||
            current->status != TaskStatus::WaitingInput || current->hasPendingSource)
            continue;
        focusedTaskId_ = id;
        return true;
    }
    return false;
}

TaskId SessionScheduler::logoff() {
    if (supervisorId_ != 0) focusedTaskId_ = supervisorId_;
    return focusedTaskId_;
}

TaskId SessionScheduler::focusedTask() const noexcept {
    return focusedTaskId_ != 0 ? focusedTaskId_ : supervisorId_;
}

bool SessionScheduler::suspendTask(TaskId id) noexcept {
    if (focusedTaskId_ == id) return false;
    for (auto& current : tasks_) {
        if (current->id != id || current->status == TaskStatus::Completed ||
            current->status == TaskStatus::Failed ||
            current->status == TaskStatus::Cancelled)
            continue;
        if (current->priority != 0) {
            current->resumePriority = current->priority;
            current->priority = 0;
            resetScheduleCredits();
        }
        return current->resumePriority != 0;
    }
    return false;
}

bool SessionScheduler::resumeTask(TaskId id) noexcept {
    for (auto& current : tasks_) {
        if (current->id != id || current->status == TaskStatus::Completed ||
            current->status == TaskStatus::Failed ||
            current->status == TaskStatus::Cancelled || current->priority != 0 ||
            current->resumePriority == 0)
            continue;
        current->priority = current->resumePriority;
        resetScheduleCredits();
        return true;
    }
    return false;
}

std::optional<bool> SessionScheduler::toggleTaskPaused(TaskId id) noexcept {
    for (auto& current : tasks_) {
        if (current->id != id || current->status == TaskStatus::Completed ||
            current->status == TaskStatus::Failed ||
            current->status == TaskStatus::Cancelled)
            continue;
        current->paused = !current->paused;
        return current->paused;
    }
    return std::nullopt;
}

bool SessionScheduler::taskPaused(TaskId id) const noexcept {
    for (const auto& current : tasks_)
        if (current->id == id) return current->paused;
    return false;
}

bool SessionScheduler::breakTaskExecution(TaskId id) {
    for (auto& current : tasks_) {
        if (current->id != id || !current->interpreter ||
            current->status == TaskStatus::Completed ||
            current->status == TaskStatus::Failed ||
            current->status == TaskStatus::Cancelled)
            continue;
        if (!current->interactive) return cancel(id);
        const TaskId childId = current->waitingChildId;
        current->waitingChildId = 0;
        if (childId != 0) (void)cancel(childId);
        current->interpreter->abortExecution();
        current->interpreter->setErrorContext(runtime::ErrorCode::None);
        removeFromSchedule(*current);
        current->releaseSource();
        current->pendingInteractiveTail.clear();
        current->hasPendingSource = false;
        current->interactiveSubmission = false;
        current->started = false;
        current->paused = false;
        current->error.clear();
        current->errorCode = runtime::ErrorCode::None;
        current->errorDetail.clear();
        current->status = TaskStatus::WaitingInput;
        focusedTaskId_ = current->id;
        return true;
    }
    return false;
}

void SessionScheduler::stopWorkersAndReturnToSupervisor() {
    executionPaused_ = false;
    for (const auto& current : tasks_) {
        if (current->supervisor || current->status == TaskStatus::Completed ||
            current->status == TaskStatus::Failed ||
            current->status == TaskStatus::Cancelled)
            continue;
        (void)cancel(current->id);
    }
    if (supervisorId_ != 0) {
        for (auto& current : tasks_) {
            if (current->id != supervisorId_) continue;
            if (current->interpreter) current->interpreter->abortExecution();
            if (current->interpreter)
                current->interpreter->setErrorContext(runtime::ErrorCode::None);
            current->releaseSource();
            current->pendingInteractiveTail.clear();
            current->hasPendingSource = false;
            current->interactiveSubmission = false;
            current->started = false;
            current->paused = false;
            current->status = TaskStatus::WaitingInput;
            current->error.clear();
            current->errorCode = runtime::ErrorCode::None;
            current->errorDetail.clear();
            removeFromSchedule(*current);
            break;
        }
        focusedTaskId_ = supervisorId_;
    }
}

bool SessionScheduler::setInstructionBudget(std::size_t budget) noexcept {
    if (budget == 0 || budget > MaximumInstructionBudget) return false;
    instructionBudget_ = budget;
    return true;
}

bool SessionScheduler::setTaskPriority(TaskId id,
                                      std::uint32_t priority) noexcept {
    if (priority > MaximumPriority) return false;
    if (priority == 0 && focusedTaskId_ == id) return false;
    for (auto& current : tasks_) {
        if (current->id != id || current->status == TaskStatus::Completed ||
            current->status == TaskStatus::Failed ||
            current->status == TaskStatus::Cancelled)
            continue;
        // PRIORITY sets the baseline directly. Unlike SUSPEND, it does not
        // preserve the current weight for a later RESUME.
        current->resumePriority = priority;
        current->priority = priority;
        resetScheduleCredits();
        return true;
    }
    return false;
}

bool SessionScheduler::debugAttach(TaskId id) {
    for (auto& current : tasks_) {
        if (current->id != id || !current->interpreter ||
            current->status == TaskStatus::Completed || current->status == TaskStatus::Failed ||
            current->status == TaskStatus::Cancelled) continue;
        current->interpreter->debugAttach();
        const auto snapshot = current->interpreter->debugInspect();
        if (current->started && snapshot && snapshot->paused)
            current->status = TaskStatus::DebugPaused;
        return true;
    }
    return false;
}

bool SessionScheduler::debugSetBreakpoint(TaskId id, int line, bool enabled) {
    for (auto& current : tasks_) {
        if (current->id != id || !current->interpreter) continue;
        return current->interpreter->debugSetBreakpoint(line, enabled);
    }
    return false;
}

bool SessionScheduler::debugResume(TaskId id, runtime::DebugResumeMode mode) {
    for (auto& current : tasks_) {
        if (current->id != id || !current->interpreter ||
            current->status != TaskStatus::DebugPaused) continue;
        if (!current->interpreter->debugResume(mode)) return false;
        current->status = TaskStatus::Runnable;
        resetScheduleCredits();
        return true;
    }
    return false;
}

bool SessionScheduler::debugPause(TaskId id) {
    for (auto& current : tasks_) {
        if (current->id != id || !current->interpreter || !current->started ||
            (current->status != TaskStatus::Runnable && current->status != TaskStatus::WaitingUi))
            continue;
        if (!current->interpreter->debugPause()) return false;
        current->status = TaskStatus::DebugPaused;
        return true;
    }
    return false;
}

bool SessionScheduler::debugSetDiagnostics(TaskId id, bool enabled, bool trace) {
    for (auto& current : tasks_) {
        if (current->id != id || !current->interpreter) continue;
        current->interpreter->debugSetDiagnostics(enabled, trace);
        return true;
    }
    return false;
}

bool SessionScheduler::debugResetDiagnostics(TaskId id) {
    for (auto& current : tasks_) {
        if (current->id != id || !current->interpreter) continue;
        current->interpreter->debugResetDiagnostics();
        return true;
    }
    return false;
}

std::optional<runtime::DebugSnapshot> SessionScheduler::debugInspect(TaskId id) const {
    for (const auto& current : tasks_)
        if (current->id == id && current->interpreter)
            return current->interpreter->debugInspect();
    return std::nullopt;
}

std::vector<runtime::DebugBreakpointInfo> SessionScheduler::debugBreakpoints(TaskId id) const {
    for (const auto& current : tasks_)
        if (current->id == id && current->interpreter)
            return current->interpreter->debugBreakpoints();
    return {};
}

std::optional<runtime::DebugValue> SessionScheduler::debugEvaluate(
    TaskId id, const std::string& expression) const {
    for (const auto& current : tasks_)
        if (current->id == id && current->interpreter)
            return current->interpreter->debugEvaluate(expression);
    return std::nullopt;
}

bool SessionScheduler::debugAddWatch(TaskId id, std::string expression) {
    for (auto& current : tasks_)
        if (current->id == id && current->interpreter)
            return current->interpreter->debugAddWatch(std::move(expression));
    return false;
}

bool SessionScheduler::debugRemoveWatch(TaskId id, const std::string& expression) {
    for (auto& current : tasks_)
        if (current->id == id && current->interpreter)
            return current->interpreter->debugRemoveWatch(expression);
    return false;
}

std::vector<runtime::ChannelInfo> SessionScheduler::taskChannels(TaskId id) const {
    for (const auto& current : tasks_)
        if (current->id == id && current->interpreter)
            return current->interpreter->channelInventory();
    return {};
}

bool SessionScheduler::setTaskViewport(TaskId id, graphics::TaskViewport viewport) {
    if (!services_.graphics) return false;
    for (auto& current : tasks_) {
        if (current->id != id || !current->interpreter) continue;
        try {
            services_.graphics->setTaskViewport(
                current->interpreter->graphicsSessionId(), viewport);
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }
    return false;
}

std::string SessionScheduler::takeError(TaskId id) {
    for (auto& current : tasks_)
        if (current->id == id) {
            current->errorPending = false;
            if (current->error.empty() &&
                current->errorCode == runtime::ErrorCode::OutOfMemory)
                return "out of memory";
            return std::exchange(current->error, {});
        }
    return {};
}

bool SessionScheduler::tick() {
    if (executionPaused_) return false;
    const auto wakeParent = [&](Task& child) {
        if (child.waitingParentId == 0) return;
        for (auto& parentPtr : tasks_) {
            Task& parent = *parentPtr;
            if (parent.id != child.waitingParentId) continue;
            parent.waitingChildId = 0;
            parent.errorCode = child.errorCode;
            parent.errorDetail = child.errorDetail;
            parent.error = child.error;
            if (parent.interpreter)
                parent.interpreter->setErrorContext(child.errorCode, child.errorDetail);
            if (parent.status != TaskStatus::Completed &&
                parent.status != TaskStatus::Failed &&
                parent.status != TaskStatus::Cancelled) {
                parent.started = true;
                parent.status = TaskStatus::Runnable;
                focusedTaskId_ = parent.id;
            }
            break;
        }
        child.waitingParentId = 0;
    };

    bool removedEligibleTask = false;
    bool changedPriority = false;
    for (auto& candidatePtr : tasks_) {
        Task& candidate = *candidatePtr;
        candidate.schedulerEligibleThisTick = false;
        if (candidate.status == TaskStatus::Completed ||
            candidate.status == TaskStatus::Failed ||
            candidate.status == TaskStatus::Cancelled ||
            candidate.status == TaskStatus::WaitingTask ||
            candidate.status == TaskStatus::DebugPaused || candidate.paused)
            continue;
        if (candidate.status == TaskStatus::WaitingChannel) {
            if (!candidate.interpreter || !candidate.interpreter->channelReadReady()) continue;
            candidate.status = TaskStatus::Runnable;
        }
        if (candidate.status == TaskStatus::WaitingInput &&
            !candidate.hasPendingSource)
            continue;
        candidate.schedulerEligibleThisTick = true;
        if (candidate.schedulerWasEligible &&
            candidate.schedulerLastPriority != candidate.priority)
            changedPriority = true;
    }
    for (auto& candidatePtr : tasks_) {
        Task& candidate = *candidatePtr;
        if (candidate.schedulerWasEligible &&
            !candidate.schedulerEligibleThisTick)
            removedEligibleTask = true;
    }
    if (removedEligibleTask || changedPriority) resetScheduleCredits();

    Task* selectedTask = nullptr;
    std::int64_t totalPriority = 0;
    for (auto& candidatePtr : tasks_) {
        Task& candidate = *candidatePtr;
        if (!candidate.schedulerEligibleThisTick) {
            candidate.schedulerWasEligible = false;
            continue;
        }
        candidate.schedulerWasEligible = true;
        candidate.schedulerLastPriority = candidate.priority;
        candidate.schedulerCredit += static_cast<std::int64_t>(candidate.priority);
        totalPriority += static_cast<std::int64_t>(candidate.priority);
        if (candidate.priority > 0 &&
            (!selectedTask || candidate.schedulerCredit > selectedTask->schedulerCredit))
            selectedTask = &candidate;
    }
    if (!selectedTask || totalPriority == 0) return false;
    selectedTask->schedulerCredit -= totalPriority;

    Task& current = *selectedTask;
    const bool submittedInteractiveCommand =
        current.interactive && current.interactiveSubmission;
    const auto handleFailure = [&](std::string message, runtime::ErrorCode code,
                                   std::string detail) {
        const bool outOfMemory = code == runtime::ErrorCode::OutOfMemory;
        if (outOfMemory) {
            if (services_.memory) (void)services_.memory->releaseEmergencyReserve();
            std::string{}.swap(current.error);
            std::string{}.swap(detail);
        } else {
            current.error = std::move(message);
        }
        current.errorCode = code;
        current.errorPending = true;
        current.errorDetail = std::move(detail);
        if (current.interpreter)
            current.interpreter->setErrorContext(current.errorCode, current.errorDetail);
        current.hasPendingSource = false;
        current.interactiveSubmission = false;
        current.pendingInteractiveTail.clear();
        current.releaseSource();
        current.started = false;
        if (current.interactive && (current.supervisor || !outOfMemory)) {
            if (!submittedInteractiveCommand && current.interpreter) {
                if (outOfMemory) current.interpreter->discardGraphicsResources();
                else current.interpreter->releaseGraphicsResources();
            }
            removeFromSchedule(current);
            current.status = TaskStatus::WaitingInput;
        } else {
            removeFromSchedule(current);
            current.status = TaskStatus::Failed;
            if (current.interpreter)
                current.interpreter->discardGraphicsResources();
            current.interpreter.reset();
        }
        wakeParent(current);
    };

    try {
        ExecutionStatus result;
        if (current.started) {
            result = current.interpreter->resumeSlice(*current.output,
                                                      instructionBudget_);
        } else if (current.interactive && current.interactiveSubmission) {
            result = current.interpreter->executeInteractiveSlice(
                current.source, *current.output, instructionBudget_);
        } else {
            result = current.interpreter->executeSlice(
                current.source, *current.output, instructionBudget_);
        }
        current.hasPendingSource = false;
        current.interactiveSubmission = false;
        if (result == ExecutionStatus::InteractivePending) {
            if (!current.interactive)
                throw std::logic_error(
                    "interactive continuation requested outside the REPL session");
            current.releaseSource();
            current.source = current.interpreter->takePendingInteractiveTail();
            current.hasPendingSource = !current.source.empty();
            current.interactiveSubmission = current.hasPendingSource;
            current.started = false;
            current.status = current.hasPendingSource ? TaskStatus::Runnable :
                                                        TaskStatus::WaitingInput;
            if (!current.hasPendingSource) current.releaseSource();
            removeFromSchedule(current);
            return true;
        }
        if (result == ExecutionStatus::RunPending) {
            if (!current.interactive || !submittedInteractiveCommand)
                throw std::logic_error("deferred RUN requested outside an interactive command");
            current.releaseSource();
            current.source = current.interpreter->takePendingRunSource();
            current.pendingInteractiveTail =
                current.interpreter->takePendingInteractiveTail();
            current.hasPendingSource = !current.source.empty();
            current.started = false;
            current.status = current.hasPendingSource ? TaskStatus::Runnable :
                                                        TaskStatus::WaitingInput;
            return true;
        }
        if (result == ExecutionStatus::Completed) {
            current.started = false;
            removeFromSchedule(current);
            if (current.interactive) {
                if (!current.pendingInteractiveTail.empty()) {
                    current.releaseSource();
                    current.source = std::move(current.pendingInteractiveTail);
                    current.hasPendingSource = true;
                    current.interactiveSubmission = true;
                    current.status = TaskStatus::Runnable;
                } else {
                    // A worker remains a live interactive session after its
                    // startup source completes. Keep its windows and channels
                    // available while it waits for LOGON. The supervisor's
                    // one-shot startup program can release graphics resources;
                    // submitted REPL commands already keep the session alive.
                    if (current.supervisor && !submittedInteractiveCommand)
                        current.interpreter->releaseGraphicsResources();
                    current.status = TaskStatus::WaitingInput;
                    current.releaseSource();
                }
            } else {
                current.status = TaskStatus::Completed;
                current.releaseSource();
                current.interpreter.reset();
            }
            wakeParent(current);
        } else if (result == ExecutionStatus::WaitingTask) {
            current.started = true;
            current.status = TaskStatus::WaitingTask;
        } else if (result == ExecutionStatus::Suspended) {
            current.started = true;
            current.status = TaskStatus::WaitingUi;
        } else if (result == ExecutionStatus::WaitingChannel) {
            current.started = true;
            current.status = TaskStatus::WaitingChannel;
        } else if (result == ExecutionStatus::DebugPaused) {
            current.started = true;
            current.status = TaskStatus::DebugPaused;
        } else {
            current.started = true;
            current.status = TaskStatus::Runnable;
        }
    } catch (const runtime::Error& error) {
        handleFailure(error.what(), error.code(), error.detail());
    } catch (const runtime::ServiceError& error) {
        handleFailure(error.what(), error.languageCode(), error.detail());
    } catch (const std::bad_alloc& error) {
        handleFailure(error.what(), runtime::ErrorCode::OutOfMemory, error.what());
    } catch (const std::invalid_argument& error) {
        handleFailure(error.what(), runtime::ErrorCode::InvalidArgument, error.what());
    } catch (const std::out_of_range& error) {
        handleFailure(error.what(), runtime::ErrorCode::OutOfRange, error.what());
    } catch (const std::overflow_error& error) {
        handleFailure(error.what(), runtime::ErrorCode::NumericOverflow, error.what());
    } catch (const std::exception& error) {
        handleFailure(error.what(), runtime::errorCodeFor(error),
                      runtime::extendedErrorText(error));
    } catch (...) {
        handleFailure("unknown scheduler failure", runtime::ErrorCode::GeneralFailure,
                      "unknown scheduler failure");
    }
    if (services_.memory && !services_.memory->emergencyReserveHeld())
        (void)services_.memory->restoreEmergencyReserve();
    return true;
}

bool SessionScheduler::cancel(TaskId id) {
    for (auto& task : tasks_) {
        if (task->id != id || task->status == TaskStatus::Completed ||
            task->status == TaskStatus::Failed || task->status == TaskStatus::Cancelled)
            continue;
        if (task->interpreter) task->interpreter->discardGraphicsResources();
        task->interpreter.reset();
        removeFromSchedule(*task);
        task->releaseSource();
        task->pendingInteractiveTail.clear();
        task->hasPendingSource = false;
        task->interactiveSubmission = false;
        task->paused = false;
        task->status = TaskStatus::Cancelled;
        task->errorCode = runtime::ErrorCode::TaskCancelled;
        std::string{}.swap(task->error);
        std::string{}.swap(task->errorDetail);
        const TaskId parentId = task->waitingParentId;
        const TaskId childId = task->waitingChildId;
        task->waitingParentId = 0;
        task->waitingChildId = 0;
        if (focusedTaskId_ == id) logoff();
        if (childId != 0) (void)cancel(childId);
        if (parentId != 0) {
            for (auto& parent : tasks_) {
                if (parent->id != parentId || !parent->interpreter) continue;
                parent->waitingChildId = 0;
                parent->errorCode = runtime::ErrorCode::TaskCancelled;
                std::string{}.swap(parent->error);
                std::string{}.swap(parent->errorDetail);
                parent->interpreter->setErrorContext(parent->errorCode);
                parent->started = true;
                if (parent->status != TaskStatus::Cancelled &&
                    parent->status != TaskStatus::Completed &&
                    parent->status != TaskStatus::Failed) {
                    parent->status = TaskStatus::Runnable;
                    focusedTaskId_ = parentId;
                }
                break;
            }
        }
        return true;
    }
    return false;
}

bool SessionScheduler::kill(TaskId id) {
    for (auto task = tasks_.begin(); task != tasks_.end(); ++task) {
        if ((*task)->id != id) continue;
        if ((*task)->supervisor) return false;
        runtime::EmergencyReserveLease emergency(services_.memory.get());
        if ((*task)->status == TaskStatus::Completed ||
            (*task)->status == TaskStatus::Failed ||
            (*task)->status == TaskStatus::Cancelled) {
            if (focusedTaskId_ == id) logoff();
            tasks_.erase(task);
            return true;
        }
        return cancel(id);
    }
    return false;
}

bool SessionScheduler::reap(TaskId id) {
    for (auto it = tasks_.begin(); it != tasks_.end(); ++it) {
        const Task& current = **it;
        if (current.id != id || current.supervisor ||
            (current.status != TaskStatus::Completed &&
             current.status != TaskStatus::Failed &&
             current.status != TaskStatus::Cancelled) ||
            current.waitingParentId != 0 || current.waitingChildId != 0)
            continue;
        tasks_.erase(it);
        return true;
    }
    return false;
}

std::optional<TaskInfo> SessionScheduler::task(TaskId id) const {
    for (const auto& current : tasks_)
        if (current->id == id) {
            TaskInfo info{current->id, current->status, current->error,
                            current->errorCode, current->errorDetail,
                            current->interactive, current->supervisor,
                            current->priority, current->resumePriority,
                            current->paused,
                            current->interpreter ? current->interpreter->openChannelCount() : 0};
            if (services_.graphics && current->interpreter)
                info.viewport = services_.graphics->taskViewport(
                    current->interpreter->graphicsSessionId());
            return info;
        }
    return std::nullopt;
}

std::vector<TaskInfo> SessionScheduler::tasks() const {
    std::vector<TaskInfo> result;
    result.reserve(tasks_.size());
    for (const auto& current : tasks_) {
        TaskInfo info{current->id, current->status, current->error,
                          current->errorCode, current->errorDetail,
                          current->interactive, current->supervisor,
                          current->priority, current->resumePriority,
                          current->paused,
                          current->interpreter ? current->interpreter->openChannelCount() : 0};
        if (services_.graphics && current->interpreter)
            info.viewport = services_.graphics->taskViewport(
                current->interpreter->graphicsSessionId());
        result.push_back(std::move(info));
    }
    return result;
}

std::optional<TaskStatus> SessionScheduler::taskStatus(TaskId id) const noexcept {
    for (const auto& current : tasks_)
        if (current->id == id) return current->status;
    return std::nullopt;
}

std::size_t SessionScheduler::activeTaskCount() const noexcept {
    std::size_t count = 0;
    for (const auto& current : tasks_)
        if (current->status != TaskStatus::Completed &&
            current->status != TaskStatus::Failed &&
            current->status != TaskStatus::Cancelled)
            ++count;
    return count;
}

bool SessionScheduler::hasWaitingUiTask() const noexcept {
    for (const auto& current : tasks_)
        if (current->status == TaskStatus::WaitingUi) return true;
    return false;
}

void SessionScheduler::visitTaskErrors(
        void (*visitor)(TaskId, const char*, void*), void* context) {
    if (!visitor) return;
    for (auto& current : tasks_) {
        if (!current->errorPending) continue;
        const char* message = current->error.empty() &&
            current->errorCode == runtime::ErrorCode::OutOfMemory
                ? "out of memory" : current->error.c_str();
        visitor(current->id, message, context);
        current->errorPending = false;
        std::string{}.swap(current->error);
    }
}

} // namespace ql32::vm
