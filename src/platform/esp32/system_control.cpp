#include "ql32/platform/esp32/system_control.hpp"

#include <Arduino.h>

namespace ql32::platform::esp32 {

void SystemControl::reboot() {
    Serial.flush();
    ESP.restart();
}

} // namespace ql32::platform::esp32
