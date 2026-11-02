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
    case TaskStatus::WaitingTask: return runtime::TaskState::WaitingTask;
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

    std::vector<runtime::ChannelInfo> listChannels(runtime::TaskId id) const override {
        return scheduler_.taskChannels(id);
    }

    std::vector<runtime::ManagedTaskInfo> listTasks() const override {
        std::vector<runtime::ManagedTaskInfo> result;
        for (const auto& info : scheduler_.tasks()) {
            result.push_back({info.id, taskState(info.status),
                              info.supervisor, info.interactive,
                              info.priority, info.resumePriority,
                              info.paused,
                              info.error, info.errorCode, info.errorDetail,
                              info.openChannels});
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
    std::string errorDetail;
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
};

SessionScheduler::SessionScheduler(Services services, std::size_t instructionBudget)
    : services_(std::move(services)), instructionBudget_(instructionBudget) {
    if (instructionBudget_ == 0 || instructionBudget_ > MaximumInstructionBudget)
        throw std::invalid_argument("scheduler instruction budget must be in 1..65536");
    taskControl_ = std::make_shared<SchedulerTaskControl>(*this);
}

SessionScheduler::~SessionScheduler() = default;

TaskId SessionScheduler::launch(std::string source, std::ostream& output) {
    if (nextId_ == 0) throw std::overflow_error("scheduler task IDs exhausted");
    auto task = std::make_unique<Task>();
    task->id = nextId_++;
    task->source = std::move(source);
    task->output = &output;
    task->interpreter = std::make_unique<Interpreter>(
        services_.storage, services_.channels, services_.ui, services_.graphics,
        services_.network, services_.memory, taskControl_, task->id,
        services_.systemControl);
    const TaskId id = task->id;
    eligibleTasks_.reserve(tasks_.size() + 1);
    candidateTasks_.reserve(tasks_.size() + 1);
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
        services_.systemControl);
    task->interactive = true;
    task->hasPendingSource = true;
    const TaskId id = task->id;
    eligibleTasks_.reserve(tasks_.size() + 1);
    candidateTasks_.reserve(tasks_.size() + 1);
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
        services_.systemControl);
    task->interactive = true;
    task->supervisor = true;
    task->hasPendingSource = !task->source.empty();
    task->interactiveSubmission = false;
    task->status = task->hasPendingSource ? TaskStatus::Runnable : TaskStatus::WaitingInput;
    const TaskId id = task->id;
    supervisorId_ = id;
    if (focusedTaskId_ == 0) focusedTaskId_ = id;
    eligibleTasks_.reserve(tasks_.size() + 1);
    candidateTasks_.reserve(tasks_.size() + 1);
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
            policy_.resetCredits();
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
        policy_.resetCredits();
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
        policy_.remove(current->id);
        current->source.clear();
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
            current->source.clear();
            current->hasPendingSource = false;
            current->interactiveSubmission = false;
            current->started = false;
            current->paused = false;
            current->status = TaskStatus::WaitingInput;
            current->error.clear();
            current->errorCode = runtime::ErrorCode::None;
            current->errorDetail.clear();
            policy_.remove(current->id);
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
        policy_.resetCredits();
        return true;
    }
    return false;
}

std::vector<runtime::ChannelInfo> SessionScheduler::taskChannels(TaskId id) const {
    for (const auto& current : tasks_)
        if (current->id == id && current->interpreter)
            return current->interpreter->channelInventory();
    return {};
}

std::string SessionScheduler::takeError(TaskId id) {
    for (auto& current : tasks_)
        if (current->id == id) return std::exchange(current->error, {});
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

    eligibleTasks_.clear();
    candidateTasks_.clear();
    for (auto& candidatePtr : tasks_) {
        Task& candidate = *candidatePtr;
        if (candidate.status == TaskStatus::Completed ||
            candidate.status == TaskStatus::Failed ||
            candidate.status == TaskStatus::Cancelled ||
            candidate.status == TaskStatus::WaitingTask || candidate.paused)
            continue;
        if (candidate.status == TaskStatus::WaitingInput &&
            !candidate.hasPendingSource)
            continue;
        eligibleTasks_.push_back({candidate.id, candidate.priority});
        candidateTasks_.push_back(&candidate);
    }
    const auto selectedIndex = policy_.select(eligibleTasks_);
    if (!selectedIndex) return false;

    Task& current = *candidateTasks_[*selectedIndex];
    const bool submittedInteractiveCommand =
        current.interactive && current.interactiveSubmission;
    const auto handleFailure = [&](std::string message, runtime::ErrorCode code,
                                   std::string detail) {
        current.error = std::move(message);
        current.errorCode = code;
        current.errorDetail = std::move(detail);
        if (current.interpreter)
            current.interpreter->setErrorContext(current.errorCode, current.errorDetail);
        current.hasPendingSource = false;
        current.interactiveSubmission = false;
        current.source.clear();
        current.started = false;
        if (current.interactive) {
            if (!submittedInteractiveCommand && current.interpreter)
                current.interpreter->releaseGraphicsResources();
            policy_.remove(current.id);
            current.status = TaskStatus::WaitingInput;
        } else {
            policy_.remove(current.id);
            current.status = TaskStatus::Failed;
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
        if (result == ExecutionStatus::Completed) {
            current.started = false;
            policy_.remove(current.id);
            if (current.interactive) {
                if (!submittedInteractiveCommand)
                    current.interpreter->releaseGraphicsResources();
                current.status = TaskStatus::WaitingInput;
            } else {
                current.status = TaskStatus::Completed;
                current.interpreter.reset();
            }
            current.source.clear();
            wakeParent(current);
        } else if (result == ExecutionStatus::WaitingTask) {
            current.started = true;
            current.status = TaskStatus::WaitingTask;
        } else if (result == ExecutionStatus::Suspended) {
            current.started = true;
            current.status = TaskStatus::WaitingUi;
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
    return true;
}

bool SessionScheduler::cancel(TaskId id) {
    for (auto& task : tasks_) {
        if (task->id != id || task->status == TaskStatus::Completed ||
            task->status == TaskStatus::Failed || task->status == TaskStatus::Cancelled)
            continue;
        task->interpreter.reset();
        policy_.remove(task->id);
        task->source.clear();
        task->hasPendingSource = false;
        task->interactiveSubmission = false;
        task->paused = false;
        task->status = TaskStatus::Cancelled;
        task->errorCode = runtime::ErrorCode::TaskCancelled;
        task->error = runtime::errorText(task->errorCode);
        task->errorDetail = "task " + std::to_string(id) + " was killed";
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
                parent->error = runtime::errorText(parent->errorCode);
                parent->errorDetail = "waited task " + std::to_string(id) + " was killed";
                parent->interpreter->setErrorContext(parent->errorCode, parent->errorDetail);
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
    for (auto& task : tasks_)
        if (task->id == id && task->supervisor) return false;
    return cancel(id);
}

std::optional<TaskInfo> SessionScheduler::task(TaskId id) const {
    for (const auto& current : tasks_)
        if (current->id == id)
            return TaskInfo{current->id, current->status, current->error,
                            current->errorCode, current->errorDetail,
                            current->interactive, current->supervisor,
                            current->priority, current->resumePriority,
                            current->paused,
                            current->interpreter ? current->interpreter->openChannelCount() : 0};
    return std::nullopt;
}

std::vector<TaskInfo> SessionScheduler::tasks() const {
    std::vector<TaskInfo> result;
    result.reserve(tasks_.size());
    for (const auto& current : tasks_)
        result.push_back({current->id, current->status, current->error,
                          current->errorCode, current->errorDetail,
                          current->interactive, current->supervisor,
                          current->priority, current->resumePriority,
                          current->paused,
                          current->interpreter ? current->interpreter->openChannelCount() : 0});
    return result;
}

} // namespace ql32::vm
