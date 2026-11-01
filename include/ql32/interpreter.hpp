#pragma once

#include <iosfwd>
#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include "ql32/runtime/cancellation.hpp"
#include "ql32/runtime/channel.hpp"
#include "ql32/runtime/storage.hpp"
#include "ql32/runtime/network.hpp"
#include "ql32/runtime/memory.hpp"
#include "ql32/runtime/task_control.hpp"
#include "ql32/graphics/service.hpp"
#include "ql32/ui/service.hpp"

namespace ql32 {

namespace detail { struct VMExecutionState; struct UISessionState; }

using Value = std::variant<double, std::string>;
enum class ExecutionStatus { Completed, Suspended, Yielded };

class Interpreter {
public:
    explicit Interpreter(std::shared_ptr<runtime::StorageService> storage = {},
                         std::shared_ptr<runtime::ChannelService> channels = {},
                         std::shared_ptr<ui::Service> ui = {},
                         std::shared_ptr<graphics::Service> graphics = {},
                         std::shared_ptr<runtime::NetworkService> network = {},
                         std::shared_ptr<runtime::MemoryService> memory = {},
                         std::shared_ptr<runtime::TaskControlService> taskControl = {});
    ~Interpreter();
    Interpreter(const Interpreter&) = delete;
    Interpreter& operator=(const Interpreter&) = delete;
    void execute(const std::string& source, std::ostream& output,
                 runtime::CancellationToken stop = {});
    ExecutionStatus executeResumable(const std::string& source, std::ostream& output,
                                     runtime::CancellationToken stop = {});
    ExecutionStatus executeSlice(const std::string& source, std::ostream& output,
                                 std::size_t instructionBudget,
                                 runtime::CancellationToken stop = {});
    ExecutionStatus resume(std::ostream& output, runtime::CancellationToken stop = {});
    ExecutionStatus resumeSlice(std::ostream& output, std::size_t instructionBudget,
                                runtime::CancellationToken stop = {});
    void executeInteractive(const std::string& source, std::ostream& output);
    ExecutionStatus executeInteractiveResumable(
        const std::string& source, std::ostream& output,
        runtime::CancellationToken stop = {});
    ExecutionStatus executeInteractiveSlice(
        const std::string& source, std::ostream& output,
        std::size_t instructionBudget, runtime::CancellationToken stop = {});
    void runRepl(std::istream& input, std::ostream& output, std::ostream& diagnostics,
                 bool showPrompt = false,
                 std::function<bool()> dispatchUiRequest = {});
    std::vector<runtime::ChannelInfo> channelInventory() const;
    void clear();

private:
    ExecutionStatus runExecution(std::ostream& output, runtime::CancellationToken stop,
                                 std::size_t instructionBudget = 0);
    runtime::ChannelId resolveChannel(runtime::ChannelId channel) const;
    void setDefaultOutputChannel(runtime::ChannelId channel);
    void bindChannel(runtime::ChannelId source, runtime::ChannelId destination);
    void closeChannel(runtime::ChannelId channel);
    bool channelIsOpen(runtime::ChannelId channel) const;
    std::unordered_map<std::string, Value> variables_;
    std::size_t output_column_ = 0;
    runtime::ChannelId default_output_channel_ = 1;
    // Logical channels point directly at the endpoint handle they currently
    // represent. No channel-to-channel route chains are retained.
    std::unordered_map<runtime::ChannelId, runtime::ChannelId> channel_bindings_;
    std::map<int, std::string> program_;
    std::unique_ptr<detail::UISessionState> uiSession_;
    std::shared_ptr<runtime::StorageService> storage_;
    std::shared_ptr<runtime::ChannelService> channels_;
    std::shared_ptr<ui::Service> ui_;
    std::shared_ptr<graphics::Service> graphics_;
    std::shared_ptr<runtime::NetworkService> network_;
    std::shared_ptr<runtime::MemoryService> memory_;
    std::shared_ptr<runtime::TaskControlService> taskControl_;
    runtime::SessionId session_id_ = 0;
    std::unique_ptr<detail::VMExecutionState> execution_;
};

} // namespace ql32
