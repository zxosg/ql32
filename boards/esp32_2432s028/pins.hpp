#pragma once

namespace ql32::boards::esp32_2432s028 {

// Common ESP32-2432S028R / CYD (yellow PCB) pin assignment.
constexpr int tftSck = 14;
constexpr int tftMiso = 12;
constexpr int tftMosi = 13;
constexpr int tftChipSelect = 15;
constexpr int tftDataCommand = 2;
constexpr int tftBacklight = 21;
// Resistive XPT2046 touch controller on the common 2.8-inch CYD revision.
constexpr int touchSck = 25;
constexpr int touchMiso = 39;
constexpr int touchMosi = 32;
constexpr int touchChipSelect = 33;
constexpr int touchInterrupt = 36;

constexpr int sdSck = 18;
constexpr int sdMiso = 19;
constexpr int sdMosi = 23;
constexpr int sdChipSelect = 5;

} // namespace ql32::boards::esp32_2432s028
