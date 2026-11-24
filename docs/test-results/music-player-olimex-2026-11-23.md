# Music player v1 on Olimex (2026-11-23)

Board: olimex_esp32_sbc_retro, MODE 2. CTest 89/89. Built OK: retro, fabgl_psram, esp32_2432s028_spiffs, ql32_tenstar_esp32s3, lilygo_vga32 (the VM FOR/LOCal fix touches all).

- Host-tested: player logic with stubbed audio/graphics; FOR/LOCal golden.
- Hardware (me): golden 16/16 after reflash; `tools/ql32-music-scan.py` wrote `FFS:/music.lst` (21 SDC files + FFS); upload via YMODEM; player started as a task and a PCM/I2S playback was logged.
- Owner: "it works" overall (GUI dialogs), then "ok, player works using gui dialogs". One report of "music stopped" (not elaborated).

## Not individually confirmed by ear/eye (owner checklist)
- [ ] Pause / Resume audible
- [ ] Volume up / down audible
- [ ] Stop
- [ ] YM, PT3, VTX, WAV each play (SQT too)
- [ ] Page next/prev, Change disk, FFS: list
- [ ] Quit returns cleanly
Note: typing on the serial console while the player runs feeds its menus.
