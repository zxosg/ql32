#pragma once

namespace ql32::boards::tenstar_esp32s3 {

// TENSTAR TS-ESP32-S3 wiring; validate these values against the board revision.
constexpr int tftSck = 36;
constexpr int tftMiso = 37;
constexpr int tftMosi = 35;
constexpr int tftChipSelect = 7;
constexpr int tftDataCommand = 39;
constexpr int tftReset = 40;
constexpr int tftBacklight = 45;
constexpr int i2cSda = 42;
constexpr int i2cScl = 41;

} // namespace ql32::boards::tenstar_esp32s3
