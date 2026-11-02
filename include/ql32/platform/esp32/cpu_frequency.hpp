#pragma once

#include "ql32/runtime/cpu_frequency.hpp"

namespace ql32::platform::esp32 {

class CpuFrequency final : public runtime::CpuFrequencyService {
public:
    bool setFrequencyMHz(std::uint16_t mhz) override;
};

} // namespace ql32::platform::esp32
