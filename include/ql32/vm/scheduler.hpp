#pragma once

#include "ql32/interpreter.hpp"
#include "ql32/runtime/error.hpp"
#include "ql32/vm/smooth_weighted_round_robin.hpp"

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ql32::vm {

using TaskId = std::uint64_t;

enum class TaskStatus {
    Runnable,
    WaitingUi,
    WaitingInput,
    WaitingTask,
    Completed,
    Failed,
    Cancelled
};

struct TaskInfo {
    TaskId id = 0;
    TaskStatus status = TaskStatus::Runnable;
    std::string error;
    runtime::ErrorCode errorCode = runtime::ErrorCode::None;
    std::string errorDetail;
    bool interactive = false;
    bool supervisor = false;
    std::uint32_t priority = 4;
    std::uint32_t resumePriority = 4;
    std::size_t openChannels = 0;
};

struct Services {
    std::shared_ptr<runtime::StorageService> storage;
    std::shared_ptr<runtime::ChannelService> channels;
    std::shared_ptr<ui::Service> ui;
    std::shared_ptr<graphics::Service> graphics;
    std::shared_ptr<runtime::NetworkService> network;
    std::shared_ptr<runtime::MemoryService> memory;
};

// Cooperative, single-threaded weighted scheduler. Each task owns one
// Interpreter/session; UI presenters and hardware adapters are driven by the
// host between tick() calls. Output streams must outlive their tasks.
class SessionScheduler {
public:
    static constexpr std::uint32_t MaximumPriority = runtime::maximumTaskPriority;
    static constexpr std::size_t MaximumInstructionBudget =
        runtime::maximumInstructionBudget;

    explicit SessionScheduler(Services services = {},
                              std::size_t instructionBudget = 128);
    ~SessionScheduler();
    SessionScheduler(const SessionScheduler&) = delete;
    SessionScheduler& operator=(const SessionScheduler&) = delete;

    TaskId launch(std::string source, std::ostream& output);
    TaskId launchWorker(std::string source, std::ostream& output);
    TaskId launchAndWait(std::string source, std::ostream& output, TaskId parent);
    // Creates a persistent interactive session. An optional initial program
    // runs as source before the session begins accepting REPL input.
    TaskId launchInteractive(std::ostream& output, std::string initialProgram = {});
    bool submit(TaskId id, std::string source);
    bool logon(TaskId id);
    TaskId logoff();
    TaskId focusedTask() const noexcept;
    bool suspendTask(TaskId id) noexcept;
    bool resumeTask(TaskId id) noexcept;
    std::size_t instructionBudget() const noexcept { return instructionBudget_; }
    bool setInstructionBudget(std::size_t budget) noexcept;
    bool setTaskPriority(TaskId id, std::uint32_t priority) noexcept;
    std::vector<runtime::ChannelInfo> taskChannels(TaskId id) const;
    std::string takeError(TaskId id);
    bool tick();
    bool cancel(TaskId id);
    bool kill(TaskId id);
    std::optional<TaskInfo> task(TaskId id) const;
    std::vector<TaskInfo> tasks() const;

private:
    struct Task;
    Services services_;
    std::shared_ptr<runtime::TaskControlService> taskControl_;
    std::size_t instructionBudget_;
    SmoothWeightedRoundRobin policy_;
    std::vector<SmoothWeightedRoundRobin::Task> eligibleTasks_;
    std::vector<Task*> candidateTasks_;
    TaskId nextId_ = 1;
    TaskId supervisorId_ = 0;
    TaskId focusedTaskId_ = 0;
    std::vector<std::unique_ptr<Task>> tasks_;
};

} // namespace ql32::vm
