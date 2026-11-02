#pragma once

namespace ql32::runtime {

// Platform-neutral system actions requested by the interactive supervisor.
class SystemControlService {
public:
    virtual ~SystemControlService() = default;
    virtual void reboot() = 0;
};

} // namespace ql32::runtime
