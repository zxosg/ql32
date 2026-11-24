#pragma once

#include <cstdint>

namespace ql32::runtime {

// Optional platform service. Host targets leave it unset; embedded adapters
// may apply a module's requested global CPU frequency.
class CpuFrequencyService {
public:
    virtual ~CpuFrequencyService() = default;
    virtual bool setFrequencyMHz(std::uint16_t mhz) = 0;
    // Returns the measured hardware clock, or zero when the platform cannot
    // report it.
    virtual std::uint16_t currentFrequencyMHz() const { return 0; }
};

} // namespace ql32::runtime
