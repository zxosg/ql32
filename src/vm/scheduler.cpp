#include "ql32/vm/scheduler.hpp"

#include <stdexcept>
#include <utility>

namespace ql32::vm {

struct SessionScheduler::Task {
    TaskId id = 0;
    std::string source;
    std::ostream* output = nullptr;
    std::unique_ptr<Interpreter> interpreter;
    TaskStatus status = TaskStatus::Runnable;
    std::string error;
    bool started = false;
};

SessionScheduler::SessionScheduler(Services services, std::size_t instructionBudget)
    : services_(std::move(services)), instructionBudget_(instructionBudget) {
    if (instructionBudget_ == 0)
        throw std::invalid_argument("scheduler instruction budget must be positive");
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
        services_.network);
    const TaskId id = task->id;
    tasks_.push_back(std::move(task));
    return id;
}

bool SessionScheduler::tick() {
    if (tasks_.empty()) return false;
    for (std::size_t checked = 0; checked < tasks_.size(); ++checked) {
        const std::size_t index = (cursor_ + checked) % tasks_.size();
        Task& current = *tasks_[index];
        if (current.status == TaskStatus::Completed ||
            current.status == TaskStatus::Failed ||
            current.status == TaskStatus::Cancelled) continue;

        cursor_ = (index + 1) % tasks_.size();
        try {
            const ExecutionStatus result = current.started ?
                current.interpreter->resumeSlice(*current.output, instructionBudget_) :
                current.interpreter->executeSlice(current.source, *current.output,
                                                  instructionBudget_);
            current.started = true;
            if (result == ExecutionStatus::Completed) {
                current.status = TaskStatus::Completed;
                current.interpreter.reset();
                current.source.clear();
            } else if (result == ExecutionStatus::Suspended) {
                current.status = TaskStatus::WaitingUi;
            } else {
                current.status = TaskStatus::Runnable;
            }
        } catch (const std::exception& error) {
            current.status = TaskStatus::Failed;
            current.error = error.what();
            current.interpreter.reset();
            current.source.clear();
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
        task->status = TaskStatus::Cancelled;
        return true;
    }
    return false;
}

std::optional<TaskInfo> SessionScheduler::task(TaskId id) const {
    for (const auto& current : tasks_)
        if (current->id == id) return TaskInfo{current->id, current->status, current->error};
    return std::nullopt;
}

std::vector<TaskInfo> SessionScheduler::tasks() const {
    std::vector<TaskInfo> result;
    result.reserve(tasks_.size());
    for (const auto& current : tasks_)
        result.push_back({current->id, current->status, current->error});
    return result;
}

} // namespace ql32::vm
