#pragma once

#include <cstdint>

namespace ql32::runtime {

// Optional platform service. Host targets leave it unset; embedded adapters
// may apply a module's requested global CPU frequency.
class CpuFrequencyService {
public:
    virtual ~CpuFrequencyService() = default;
    virtual bool setFrequencyMHz(std::uint16_t mhz) = 0;
};

} // namespace ql32::runtime
