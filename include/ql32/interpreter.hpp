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
#include "ql32/graphics/service.hpp"
#include "ql32/ui/service.hpp"

namespace ql32 {

namespace detail { struct VMExecutionState; struct UISessionState; }

using Value = std::variant<double, std::string>;
enum class ExecutionStatus { Completed, Suspended };

class Interpreter {
public:
    explicit Interpreter(std::shared_ptr<runtime::StorageService> storage = {},
                         std::shared_ptr<runtime::ChannelService> channels = {},
                         std::shared_ptr<ui::Service> ui = {},
                         std::shared_ptr<graphics::Service> graphics = {},
                         std::shared_ptr<runtime::NetworkService> network = {});
    ~Interpreter();
    Interpreter(const Interpreter&) = delete;
    Interpreter& operator=(const Interpreter&) = delete;
    void execute(const std::string& source, std::ostream& output,
                 runtime::CancellationToken stop = {});
    ExecutionStatus executeResumable(const std::string& source, std::ostream& output,
                                     runtime::CancellationToken stop = {});
    ExecutionStatus resume(std::ostream& output, runtime::CancellationToken stop = {});
    void executeInteractive(const std::string& source, std::ostream& output);
    ExecutionStatus executeInteractiveResumable(
        const std::string& source, std::ostream& output,
        runtime::CancellationToken stop = {});
    void runRepl(std::istream& input, std::ostream& output, std::ostream& diagnostics,
                 bool showPrompt = false,
                 std::function<bool()> dispatchUiRequest = {});
    void clear();

private:
    ExecutionStatus runExecution(std::ostream& output, runtime::CancellationToken stop);
    std::unordered_map<std::string, Value> variables_;
    std::size_t output_column_ = 0;
    std::map<int, std::string> program_;
    std::unique_ptr<detail::UISessionState> uiSession_;
    std::shared_ptr<runtime::StorageService> storage_;
    std::shared_ptr<runtime::ChannelService> channels_;
    std::shared_ptr<ui::Service> ui_;
    std::shared_ptr<graphics::Service> graphics_;
    std::shared_ptr<runtime::NetworkService> network_;
    runtime::SessionId session_id_ = 0;
    std::unique_ptr<detail::VMExecutionState> execution_;
};

} // namespace ql32
