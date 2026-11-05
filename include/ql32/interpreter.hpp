#pragma once

#include <iosfwd>
#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>
#include "ql32/runtime/cancellation.hpp"
#include "ql32/runtime/channel.hpp"
#include "ql32/runtime/buffer.hpp"
#include "ql32/runtime/storage.hpp"
#include "ql32/runtime/network.hpp"
#include "ql32/runtime/memory.hpp"
#include "ql32/runtime/memory_budget.hpp"
#include "ql32/runtime/system_control.hpp"
#include "ql32/runtime/cpu_frequency.hpp"
#include "ql32/runtime/task_control.hpp"
#include "ql32/runtime/error.hpp"
#include "ql32/graphics/service.hpp"
#include "ql32/ui/service.hpp"

namespace ql32 {

namespace detail {
struct VMExecutionState;
struct UISessionState;
struct InstructionSequence;
class CompactProgram;
}

using Value = std::variant<double, std::string>;
enum class ExecutionStatus {
    Completed, Suspended, WaitingChannel, WaitingTask, Yielded, DebugPaused,
    RunPending, InteractivePending
};

class Interpreter {
public:
    explicit Interpreter(std::shared_ptr<runtime::StorageService> storage = {},
                         std::shared_ptr<runtime::ChannelService> channels = {},
                         std::shared_ptr<ui::Service> ui = {},
                         std::shared_ptr<graphics::Service> graphics = {},
                         std::shared_ptr<runtime::NetworkService> network = {},
                         std::shared_ptr<runtime::MemoryService> memory = {},
                         std::shared_ptr<runtime::TaskControlService> taskControl = {},
                         runtime::TaskId taskId = 0,
                         std::shared_ptr<runtime::SystemControlService> systemControl = {},
                         std::shared_ptr<runtime::CpuFrequencyService> cpuFrequency = {});
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
    bool channelReadReady() const;
    std::size_t openChannelCount() const;
    void setErrorContext(runtime::ErrorCode code, std::string detail = {});
    void abortExecution() noexcept;
    // Release retained display memory while keeping this interpreter session
    // available for the next interactive command.
    void releaseGraphicsResources() noexcept;
    bool debugAttach() noexcept;
    bool debugSetBreakpoint(int line, bool enabled = true);
    bool debugResume(runtime::DebugResumeMode mode);
    bool debugPause() noexcept;
    void debugSetDiagnostics(bool enabled, bool trace = false) noexcept;
    void debugResetDiagnostics() noexcept;
    std::optional<runtime::DebugSnapshot> debugInspect() const;
    std::vector<runtime::DebugBreakpointInfo> debugBreakpoints() const;
    std::optional<runtime::DebugValue> debugEvaluate(const std::string& expression) const;
    bool debugAddWatch(std::string expression);
    bool debugRemoveWatch(const std::string& expression);
    std::string takePendingRunSource() noexcept;
    std::string takePendingInteractiveTail() noexcept;
    bool hasPendingInteractivePager() const noexcept;
    ExecutionStatus resumeInteractivePager(std::ostream& output);
    runtime::ErrorCode errorCode() const noexcept { return errorCode_; }
    const std::string& errorDetail() const noexcept { return errorDetail_; }
    void clear();

private:
    ExecutionStatus runExecution(std::ostream& output, runtime::CancellationToken stop,
                                 std::size_t instructionBudget = 0);
    std::uint32_t ensureGlobalSymbol(const std::string& name, Value initial = 0.0);
    std::optional<std::uint32_t> findGlobalSymbol(const std::string& name) const;
    const Value* findGlobalValue(const std::string& name) const;
    const Value& globalValue(std::uint32_t slot) const;
    void setGlobalSlotValue(std::uint32_t slot, Value value);
    void setGlobalValue(const std::string& name, Value value);
    void bindProgramSymbols(detail::CompactProgram& code);
    void writePagerPage(std::ostream& output, std::size_t lineCount);
    void clearPager() noexcept;
    runtime::ChannelId resolveChannel(runtime::ChannelId channel) const;
    void setDefaultOutputChannel(runtime::ChannelId channel);
    void bindChannel(runtime::ChannelId source, runtime::ChannelId destination);
    void closeChannel(runtime::ChannelId channel);
    bool channelIsOpen(runtime::ChannelId channel) const;
    std::unordered_map<std::string, std::uint32_t> globalSymbolSlots_;
    std::vector<std::string> globalSymbolNames_;
    std::vector<Value> globalValues_;
    std::vector<bool> globalInitialized_;
    std::unordered_map<std::string, std::vector<std::string>> localSymbolTables_;
    std::size_t output_column_ = 0;
    runtime::ChannelId default_output_channel_ = 1;
    std::string current_directory_;
    std::string pendingRunSource_;
    std::string pendingInteractiveTail_;
    bool pagerEnabled_ = false;
    std::string pendingPagerText_;
    std::size_t pendingPagerOffset_ = 0;
    std::size_t pendingPagerPageRows_ = 1;
    runtime::ChannelId pendingPagerChannel_ = 0;
    std::optional<ui::RequestId> pendingPagerRequest_;
    // Logical channels point directly at the endpoint handle they currently
    // represent. No channel-to-channel route chains are retained.
    std::unordered_map<runtime::ChannelId, runtime::ChannelId> channel_bindings_;
    std::map<int, std::string> program_;
    std::unique_ptr<detail::UISessionState> uiSession_;
    std::shared_ptr<runtime::StorageService> storage_;
    std::shared_ptr<runtime::ChannelService> channels_;
    runtime::BufferManager buffers_;
    std::shared_ptr<ui::Service> ui_;
    std::shared_ptr<graphics::Service> graphics_;
    std::shared_ptr<runtime::NetworkService> network_;
    std::shared_ptr<runtime::MemoryService> memory_;
    runtime::MemoryBudget memoryBudget_;
    std::shared_ptr<runtime::CpuFrequencyService> cpuFrequency_;
    std::shared_ptr<runtime::TaskControlService> taskControl_;
    std::shared_ptr<runtime::SystemControlService> systemControl_;
    runtime::SessionId session_id_ = 0;
    runtime::TaskId task_id_ = 0;
    runtime::ErrorCode errorCode_ = runtime::ErrorCode::None;
    std::string errorDetail_;
    std::unique_ptr<detail::VMExecutionState> execution_;
    bool debuggerAttached_ = false;
    bool diagnosticsEnabled_ = false;
    bool traceEnabled_ = false;
    bool debugPauseRequested_ = false;
    std::vector<std::pair<int, bool>> pendingBreakpoints_;
    runtime::TaskId debugTaskId_ = 0;
    std::vector<std::string> debugWatches_;
};

} // namespace ql32
