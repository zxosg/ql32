#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
target=${1:-}

case "$target" in
  linux)
    set -- cmake --preset linux
    cd "$repo_dir"
    "$@"
    exec cmake --build --preset linux
    ;;
  mac)
    set -- cmake --preset macos
    cd "$repo_dir"
    "$@"
    exec cmake --build --preset macos
    ;;
  vga32)
    cd "$repo_dir"
    exec pio run -e lilygo_vga32
    ;;
  olimex)
    cd "$repo_dir"
    exec pio run -e olimex_esp32_sbc_fabgl
    ;;
  cyd)
    cd "$repo_dir"
    exec pio run -e esp32_2432s028
    ;;
  *)
    printf '%s\n' \
      'Usage: tools/build-target.sh {linux|mac|vga32|olimex|cyd}' \
      '' \
      'Build one named target:' \
      '  linux  CMake host executable and CTest binaries (does not run tests)' \
      '  mac    macOS host executable and CTest binaries (does not run tests)' \
      '  vga32  LILYGO FabGL VGA32 firmware' \
      '  olimex Olimex ESP32-SBC-FabGL firmware' \
      '  cyd    ESP32-2432S028 CYD firmware' >&2
    exit 2
    ;;
esac
