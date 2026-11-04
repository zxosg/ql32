#pragma once

#include "ql32/runtime/storage.hpp"
#include "ql32/vm/scheduler.hpp"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace ql32::platform::esp32 {

struct RemoteDebugCapabilities {
    std::string board;
    std::vector<std::string> storageRoots;
    bool screenCapture = false;
};

class RemoteDebugProtocol {
public:
    using Write = std::function<void(std::string_view)>;

    explicit RemoteDebugProtocol(RemoteDebugCapabilities capabilities)
        : capabilities_(std::move(capabilities)) {}

    bool handles(std::string_view line) const noexcept;
    void handle(std::string_view line, vm::SessionScheduler& scheduler,
                runtime::StorageService& storage, std::ostream& taskOutput,
                const Write& write) const;

private:
    RemoteDebugCapabilities capabilities_;
};

} // namespace ql32::platform::esp32
