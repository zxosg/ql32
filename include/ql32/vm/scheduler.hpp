#pragma once

#include "ql32/interpreter.hpp"

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ql32::vm {

using TaskId = std::uint64_t;

enum class TaskStatus { Runnable, WaitingUi, Completed, Failed, Cancelled };

struct TaskInfo {
    TaskId id = 0;
    TaskStatus status = TaskStatus::Runnable;
    std::string error;
};

struct Services {
    std::shared_ptr<runtime::StorageService> storage;
    std::shared_ptr<runtime::ChannelService> channels;
    std::shared_ptr<ui::Service> ui;
    std::shared_ptr<graphics::Service> graphics;
    std::shared_ptr<runtime::NetworkService> network;
};

// Cooperative, single-threaded round-robin scheduler. Each task owns one
// Interpreter/session; UI presenters and hardware adapters are driven by the
// host between tick() calls. Output streams must outlive their tasks.
class SessionScheduler {
public:
    explicit SessionScheduler(Services services = {},
                              std::size_t instructionBudget = 128);
    ~SessionScheduler();
    SessionScheduler(const SessionScheduler&) = delete;
    SessionScheduler& operator=(const SessionScheduler&) = delete;

    TaskId launch(std::string source, std::ostream& output);
    bool tick();
    bool cancel(TaskId id);
    std::optional<TaskInfo> task(TaskId id) const;
    std::vector<TaskInfo> tasks() const;

private:
    struct Task;
    Services services_;
    std::size_t instructionBudget_;
    TaskId nextId_ = 1;
    std::size_t cursor_ = 0;
    std::vector<std::unique_ptr<Task>> tasks_;
};

} // namespace ql32::vm
