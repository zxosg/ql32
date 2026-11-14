#include "ql32/platform/esp32/cpu_frequency.hpp"

#include <Arduino.h>

namespace ql32::platform::esp32 {

bool CpuFrequency::setFrequencyMHz(std::uint16_t mhz) {
    if (mhz != 80 && mhz != 160 && mhz != 240) return false;
    // Avoid reconfiguring the ESP32 clock tree when the requested rate is
    // already active. Reapplying the same rate can disturb FabGL VGA timing.
    if (getCpuFrequencyMhz() == mhz) return true;
    return setCpuFrequencyMhz(mhz);
}

std::uint16_t CpuFrequency::currentFrequencyMHz() const {
    return static_cast<std::uint16_t>(getCpuFrequencyMhz());
}

} // namespace ql32::platform::esp32
