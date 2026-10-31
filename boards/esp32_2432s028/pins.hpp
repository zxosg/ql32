#pragma once

namespace ql32::boards::esp32_2432s028 {

// Common ESP32-2432S028R / CYD (yellow PCB) pin assignment.
constexpr int tftSck = 14;
constexpr int tftMiso = 12;
constexpr int tftMosi = 13;
constexpr int tftChipSelect = 15;
constexpr int tftDataCommand = 2;
constexpr int tftBacklight = 21;

constexpr int sdSck = 18;
constexpr int sdMiso = 19;
constexpr int sdMosi = 23;
constexpr int sdChipSelect = 5;

} // namespace ql32::boards::esp32_2432s028
