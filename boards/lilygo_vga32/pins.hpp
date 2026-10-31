#pragma once

namespace ql32::boards::lilygo_vga32 {

inline constexpr int vgaRed = 21;
inline constexpr int vgaGreen = 18;
inline constexpr int vgaBlue = 5;
inline constexpr int vgaHSync = 23;
inline constexpr int vgaVSync = 15;

inline constexpr int keyboardData = 32;
inline constexpr int keyboardClock = 33;
inline constexpr int mouseData = 27;
inline constexpr int mouseClock = 26;

inline constexpr int audio = 25;
inline constexpr int sdMosi = 12;
inline constexpr int sdMiso = 2;
inline constexpr int sdClock = 14;
inline constexpr int sdChipSelect = 13;

} // namespace ql32::boards::lilygo_vga32
