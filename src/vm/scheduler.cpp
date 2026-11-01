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
    case TaskStatus::Completed: return runtime::TaskState::Completed;
    case TaskStatus::Failed: return runtime::TaskState::Failed;
    case TaskStatus::Cancelled: return runtime::TaskState::Cancelled;
    }
    return runtime::TaskState::Failed;
}

class SchedulerTaskControl final : public runtime::TaskControlService {
public:
    explicit SchedulerTaskControl(SessionScheduler& scheduler) : scheduler_(scheduler) {}

    runtime::TaskId spawn(std::string source, std::ostream& output) override {
        return scheduler_.launchWorker(std::move(source), output);
    }

    bool cancelWorker(runtime::TaskId id) override {
        return scheduler_.cancelWorker(id);
    }

    bool logon(runtime::TaskId id) override { return scheduler_.logon(id); }
    runtime::TaskId logoff() override { return scheduler_.logoff(); }
    runtime::TaskId focusedTask() const override { return scheduler_.focusedTask(); }

    std::vector<runtime::ChannelInfo> listChannels(runtime::TaskId id) const override {
        return scheduler_.taskChannels(id);
    }

    std::vector<runtime::ManagedTaskInfo> listTasks() const override {
        std::vector<runtime::ManagedTaskInfo> result;
        for (const auto& info : scheduler_.tasks()) {
            result.push_back({info.id, taskState(info.status),
                              info.supervisor, info.error});
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
    bool started = false;
    bool interactive = false;
    bool supervisor = false;
    bool interactiveSubmission = false;
    bool hasPendingSource = true;
};

SessionScheduler::SessionScheduler(Services services, std::size_t instructionBudget)
    : services_(std::move(services)), instructionBudget_(instructionBudget) {
    if (instructionBudget_ == 0)
        throw std::invalid_argument("scheduler instruction budget must be positive");
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
        services_.network, services_.memory, taskControl_);
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
        services_.network, services_.memory, taskControl_);
    task->interactive = true;
    task->hasPendingSource = true;
    const TaskId id = task->id;
    tasks_.push_back(std::move(task));
    return id;
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
        services_.network, services_.memory, taskControl_);
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
    if (tasks_.empty()) return false;
    for (std::size_t checked = 0; checked < tasks_.size(); ++checked) {
        const std::size_t index = (cursor_ + checked) % tasks_.size();
        Task& current = *tasks_[index];
        if (current.status == TaskStatus::Completed ||
            current.status == TaskStatus::Failed ||
            current.status == TaskStatus::Cancelled) continue;
        if (current.status == TaskStatus::WaitingInput && !current.hasPendingSource)
            continue;

        cursor_ = (index + 1) % tasks_.size();
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
                if (current.interactive) {
                    current.status = TaskStatus::WaitingInput;
                } else {
                    current.status = TaskStatus::Completed;
                    current.interpreter.reset();
                }
                current.source.clear();
            } else if (result == ExecutionStatus::Suspended) {
                current.started = true;
                current.status = TaskStatus::WaitingUi;
            } else {
                current.started = true;
                current.status = TaskStatus::Runnable;
            }
        } catch (const std::exception& error) {
            current.error = error.what();
            current.hasPendingSource = false;
            current.interactiveSubmission = false;
            current.source.clear();
            current.started = false;
            if (current.interactive) {
                current.status = TaskStatus::WaitingInput;
            } else {
                current.status = TaskStatus::Failed;
                current.interpreter.reset();
            }
        }
        return true;
    }
    return false;
}

bool SessionScheduler::cancel(TaskId id) {
    for (auto& task : tasks_) {
        if (task->id != id || task->status == TaskStatus::Completed ||
            task->status == TaskStatus::Failed || task->status == TaskStatus::Cancelled)
            continue;
        task->interpreter.reset();
        task->source.clear();
        task->hasPendingSource = false;
        task->interactiveSubmission = false;
        task->status = TaskStatus::Cancelled;
        if (focusedTaskId_ == id) logoff();
        return true;
    }
    return false;
}

bool SessionScheduler::cancelWorker(TaskId id) {
    for (auto& task : tasks_) {
        if (task->id != id || task->supervisor || !task->interactive ||
            task->status == TaskStatus::Completed || task->status == TaskStatus::Failed ||
            task->status == TaskStatus::Cancelled)
            continue;
        task->interpreter.reset();
        task->source.clear();
        task->hasPendingSource = false;
        task->interactiveSubmission = false;
        task->status = TaskStatus::Cancelled;
        if (focusedTaskId_ == id) logoff();
        return true;
    }
    return false;
}

std::optional<TaskInfo> SessionScheduler::task(TaskId id) const {
    for (const auto& current : tasks_)
        if (current->id == id)
            return TaskInfo{current->id, current->status, current->error,
                            current->interactive, current->supervisor};
    return std::nullopt;
}

std::vector<TaskInfo> SessionScheduler::tasks() const {
    std::vector<TaskInfo> result;
    result.reserve(tasks_.size());
    for (const auto& current : tasks_)
        result.push_back({current->id, current->status, current->error,
                          current->interactive, current->supervisor});
    return result;
}

} // namespace ql32::vm
