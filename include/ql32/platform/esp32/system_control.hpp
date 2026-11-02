#pragma once

#include "ql32/runtime/system_control.hpp"

namespace ql32::platform::esp32 {

class SystemControl final : public runtime::SystemControlService {
public:
    void reboot() override;
};

} // namespace ql32::platform::esp32
