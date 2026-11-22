#pragma once

#include <iosfwd>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>
#include "ql32/runtime/cancellation.hpp"
#include "ql32/runtime/channel.hpp"
#include "ql32/runtime/buffer.hpp"
#include "ql32/runtime/storage.hpp"
#include "ql32/runtime/file_ops.hpp"
#include "ql32/runtime/network.hpp"
#include "ql32/runtime/memory.hpp"
#include "ql32/runtime/system_control.hpp"
#include "ql32/runtime/system_info.hpp"
#include "ql32/runtime/cpu_frequency.hpp"
#include "ql32/runtime/display_mode.hpp"
#include "ql32/runtime/music.hpp"
#include "ql32/runtime/task_control.hpp"
#include "ql32/runtime/error.hpp"
#include "ql32/graphics/service.hpp"
#include "ql32/graphics/bitmap.hpp"
#include "ql32/ui/service.hpp"

namespace ql32 {

namespace detail { struct VMExecutionState; struct UISessionState; }

using Value = std::variant<double, std::string>;
enum class ExecutionStatus {
    Completed, Suspended, WaitingChannel, WaitingTask, WaitingTime, Yielded, DebugPaused,
    RunPending, InteractivePending, ProgramStopped
};

struct InteractiveLineEdit {
    std::string text;
    int lineNumber = 0;
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
                         std::shared_ptr<runtime::CpuFrequencyService> cpuFrequency = {},
                         std::shared_ptr<runtime::DisplayModeService> displayModes = {},
                         std::shared_ptr<runtime::SystemInfoService> systemInfo = {},
                         std::shared_ptr<runtime::MusicService> music = {});
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
    // Keeps channels opened by programs in a long-lived interactive session
    // until explicitly closed or until the session itself ends.
    void setPersistentSessionResources(bool persistent) noexcept {
        persistentSessionResources_ = persistent;
    }
    // Release retained display memory while keeping this interpreter session
    // available for the next interactive command.
    void releaseGraphicsResources() noexcept;
    // Drop graphics allocations immediately; used for cancellation and
    // terminal failures where preserving the last image would waste memory.
    void discardGraphicsResources() noexcept;
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
    runtime::SessionId graphicsSessionId() const noexcept { return session_id_; }
    std::string takePendingRunSource() noexcept;
    std::string takePendingInteractiveTail() noexcept;
    std::optional<InteractiveLineEdit> takePendingLineEdit() noexcept;
    std::optional<InteractiveLineEdit> navigateProgramEdit(int currentLine,
                                                            int direction);
    bool hasPendingInteractivePager() const noexcept;
    ExecutionStatus resumeInteractivePager(std::ostream& output);
    bool hasPendingFileJob() const noexcept { return pendingFileJob_ != nullptr; }
    ExecutionStatus resumeFileJob(std::ostream& output);
    runtime::ErrorCode errorCode() const noexcept { return errorCode_; }
    const std::string& errorDetail() const noexcept { return errorDetail_; }
    void clear();
    std::uint32_t requestedPauseDurationMs() const noexcept {
        return requestedPauseDurationMs_;
    }

private:
    ExecutionStatus runExecution(std::ostream& output, runtime::CancellationToken stop,
                                 std::size_t instructionBudget = 0);
    void writePagerPage(std::ostream& output, std::size_t lineCount);
    void clearPager() noexcept;
    void clearFileJob() noexcept;
    ExecutionStatus startFileJob(runtime::fileops::Plan plan, std::ostream& output,
                                 const char* commandName);
    ExecutionStatus runFileJob(std::ostream& output);
    void writeTextToChannel(std::ostream& output, runtime::ChannelId channel,
                            std::string_view text);
    // Single text-output choke point for a resolved endpoint; fans out to mirrors.
    void emitText(std::ostream& output, runtime::ChannelId endpoint,
                  const std::string& text, bool mirror = true);
    void writeEndpoint(std::ostream& output, runtime::ChannelId endpoint,
                       const std::string& text);
    void mirrorText(std::ostream& output, runtime::ChannelId source,
                    const std::string& text);
    // Live graphics-capable mirror destinations of a source; closed ones are disabled.
    std::vector<runtime::ChannelId> graphicsMirrorTargets(runtime::ChannelId source);
    void failMirror(runtime::ChannelId source, runtime::ChannelId destination,
                    const std::string& reason);
    void addMirror(runtime::ChannelId source, runtime::ChannelId destination);
    void deleteMirrors(runtime::ChannelId destination);
    void removeMirrorsFor(runtime::ChannelId channel);
    std::string mirrorListing() const;
    runtime::ChannelId resolveChannel(runtime::ChannelId channel) const;
    void setDefaultOutputChannel(runtime::ChannelId channel);
    void bindChannel(runtime::ChannelId source, runtime::ChannelId destination);
    void closeChannel(runtime::ChannelId channel);
    void closeExecutionChannels() noexcept;
    bool channelIsOpen(runtime::ChannelId channel) const;
    std::unordered_map<std::string, Value> variables_;
    std::size_t output_column_ = 0;
    runtime::ChannelId default_output_channel_ = 1;
    std::string current_directory_;
    std::string pendingRunSource_;
    std::string pendingInteractiveTail_;
    std::optional<InteractiveLineEdit> pendingLineEdit_;
    bool pagerEnabled_ = false;
    std::string pendingPagerText_;
    std::size_t pendingPagerOffset_ = 0;
    std::size_t pendingPagerPageRows_ = 1;
    runtime::ChannelId pendingPagerChannel_ = 0;
    std::optional<ui::RequestId> pendingPagerRequest_;
    std::unique_ptr<runtime::fileops::Job> pendingFileJob_;
    std::optional<ui::RequestId> pendingFileConfirm_;
    std::ostream* fileJobOutput_ = nullptr;
    // Logical channels point directly at the endpoint handle they currently
    // represent. No channel-to-channel route chains are retained.
    std::unordered_map<runtime::ChannelId, runtime::ChannelId> channel_bindings_;
    struct Mirror {
        runtime::ChannelId destination = 0;
        bool disabled = false;
        std::uint64_t dropped = 0;
        std::string failure;
    };
    // Session-local text mirrors keyed by resolved source endpoint.
    std::map<runtime::ChannelId, std::vector<Mirror>> mirrors_;
    bool emittingMirror_ = false;
    std::vector<runtime::ChannelId> executionChannels_;
    bool interactiveCommandExecution_ = false;
    bool persistentSessionResources_ = false;
    std::map<int, std::string> program_;
    std::optional<int> lastEditedLine_;
    std::unique_ptr<detail::UISessionState> uiSession_;
    std::shared_ptr<runtime::StorageService> storage_;
    graphics::FontRegistry fontRegistry_;
    graphics::BitmapRegistry bitmapRegistry_;
    std::shared_ptr<runtime::ChannelService> channels_;
    runtime::BufferManager buffers_;
    std::shared_ptr<ui::Service> ui_;
    std::shared_ptr<graphics::Service> graphics_;
    std::shared_ptr<runtime::NetworkService> network_;
    std::shared_ptr<runtime::MemoryService> memory_;
    std::shared_ptr<runtime::CpuFrequencyService> cpuFrequency_;
    std::shared_ptr<runtime::DisplayModeService> displayModes_;
    std::shared_ptr<runtime::SystemInfoService> systemInfo_;
    std::shared_ptr<runtime::MusicService> music_;
    std::shared_ptr<runtime::TaskControlService> taskControl_;
    std::shared_ptr<runtime::SystemControlService> systemControl_;
    runtime::SessionId session_id_ = 0;
    runtime::TaskId task_id_ = 0;
    std::uint32_t requestedPauseDurationMs_ = 0;
    std::uint64_t randomState_ = 0;
    runtime::ErrorCode errorCode_ = runtime::ErrorCode::None;
    std::string errorDetail_;
    std::unique_ptr<detail::VMExecutionState> execution_;
    std::unique_ptr<detail::VMExecutionState> stoppedExecution_;
    bool debuggerAttached_ = false;
    bool diagnosticsEnabled_ = false;
    bool traceEnabled_ = false;
    bool debugPauseRequested_ = false;
    std::vector<std::pair<int, bool>> pendingBreakpoints_;
    runtime::TaskId debugTaskId_ = 0;
    std::vector<std::string> debugWatches_;
};

} // namespace ql32
