# Release packaging notes

Release steps (v0.1.0 followed these):

1. Confirm `CHANGELOG.md` and the version in `CMakeLists.txt` (0.1.0).
2. Build the release envs: `esp32_2432s028_spiffs`, `olimex_esp32_sbc_fabgl`,
   `lilygo_vga32`, `ql32_tenstar_esp32s3`.
3. Collect from `.pio/build/<env>/`: `firmware.bin`, `bootloader.bin`,
   `partitions.bin`. Optionally merge:
   `esptool.py --chip esp32 merge_bin -o ql32-<env>-v0.1.0.bin 0x1000 bootloader.bin 0x8000 partitions.bin 0x10000 firmware.bin`
   (ESP32-S3: `--chip esp32s3`, bootloader at 0x0).
4. `shasum -a 256 ql32-*.bin > SHA256SUMS`.
5. Tag v0.1.0 on main and attach the binaries; label the Olimex, LILYGO and
   Tenstar binaries "experimental, not hardware-tested".
6. Never ship a startup file with WiFi credentials.
