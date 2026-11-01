#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>
#include "ql32/runtime/channel.hpp"

namespace ql32::runtime {

using TaskId = std::uint64_t;

enum class TaskState { Runnable, WaitingUi, WaitingInput, Completed, Failed, Cancelled };

struct ManagedTaskInfo {
    TaskId id = 0;
    TaskState state = TaskState::Runnable;
    bool supervisor = false;
    std::string error;
};

// Control surface available to the interactive BASIC supervisor.
class TaskControlService {
public:
    virtual ~TaskControlService() = default;
    virtual TaskId spawn(std::string source, std::ostream& output) = 0;
    virtual bool cancelWorker(TaskId id) = 0;
    virtual bool logon(TaskId id) = 0;
    virtual TaskId logoff() = 0;
    virtual TaskId focusedTask() const = 0;
    virtual std::vector<ManagedTaskInfo> listTasks() const = 0;
    virtual std::vector<ChannelInfo> listChannels(TaskId id) const = 0;
};

inline const char* taskStateName(TaskState state) noexcept {
    switch (state) {
    case TaskState::Runnable: return "RUNNABLE";
    case TaskState::WaitingUi: return "WAITING_UI";
    case TaskState::WaitingInput: return "WAITING_INPUT";
    case TaskState::Completed: return "COMPLETED";
    case TaskState::Failed: return "FAILED";
    case TaskState::Cancelled: return "CANCELLED";
    }
    return "UNKNOWN";
}

} // namespace ql32::runtime
