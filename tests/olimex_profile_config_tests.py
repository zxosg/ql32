#!/usr/bin/env python3
"""Validate the Olimex Rev B GPIO and internal FFS build configuration."""

import configparser
import csv
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def check(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def parse_int(value: str) -> int:
    return int(value.strip(), 0)


def main() -> None:
    partition_path = ROOT / "boards/esp32_4mb_olimex_ffs.csv"
    partitions = []
    with partition_path.open(newline="", encoding="utf-8") as source:
        for row in csv.reader(line for line in source if not line.lstrip().startswith("#")):
            if not row or not row[0].strip():
                continue
            fields = [field.strip() for field in row]
            check(len(fields) >= 5, f"invalid partition row: {row!r}")
            name, kind, subtype, offset, size = fields[:5]
            partitions.append((name, kind, subtype, parse_int(offset), parse_int(size)))

    spiffs = [item for item in partitions if item[0] == "spiffs"]
    check(len(spiffs) == 1, "partition table must contain exactly one spiffs partition")
    _, kind, subtype, offset, size = spiffs[0]
    check(kind == "data" and subtype == "spiffs", "spiffs partition type/subtype mismatch")
    check(offset == 0x370000 and size == 512 * 1024,
          "Olimex FFS must occupy 512 KiB at offset 0x370000")

    ordered = sorted((offset, offset + size, name) for name, _, _, offset, size in partitions)
    for previous, current in zip(ordered, ordered[1:]):
        check(previous[1] <= current[0],
              f"partition overlap: {previous[2]} overlaps {current[2]}")
    check(max(end for _, end, _ in ordered) <= 4 * 1024 * 1024,
          "partition table exceeds 4 MiB flash")

    config = configparser.ConfigParser(interpolation=None, inline_comment_prefixes=(";", "#"))
    config.read(ROOT / "platformio.ini", encoding="utf-8")
    section = "env:olimex_esp32_sbc_fabgl"
    check(config.has_section(section), f"missing [{section}] in platformio.ini")
    env = config[section]
    check(env.get("board_build.partitions") == "boards/esp32_4mb_olimex_ffs.csv",
          "Olimex environment must use its 4 MiB FFS partition table")
    check(env.get("board_build.filesystem") == "spiffs",
          "Olimex environment must build a SPIFFS filesystem image")
    flags = env.get("build_flags", "")
    check("-DQL32_BOARD_OLIMEX_ESP32_SBC_FABGL" in flags,
          "Olimex board define missing")
    check("-DQL32_OLIMEX_USE_SPIFFS" in flags,
          "Olimex FFS mount define missing")

    runtime_env = "env:olimex_esp32_sbc_fabgl_psram_dma_display"
    check(config.has_section(runtime_env), f"missing [{runtime_env}]")
    check("QL32_OLIMEX_DMA_STATUS_TEST" not in
          config[runtime_env].get("build_flags", ""),
          "periodic DMA telemetry must remain opt-in for the runtime profile")
    for width in ("640x400", "640x480"):
        profile = f"env:olimex_esp32_sbc_fabgl_psram_dma_display_{width}_profile"
        check(config.has_section(profile), f"missing [{profile}]")
        profile_flags = config[profile].get("build_flags", "")
        check("QL32_OLIMEX_DMA_REFILL_PROFILE" in profile_flags,
              f"{profile} must collect detailed refill timing")
        check("QL32_OLIMEX_DMA_STATUS_TEST" in profile_flags,
              f"{profile} must enable deferred DMA status")

    split_profile = "env:olimex_esp32_sbc_fabgl_psram_dma_display_640x480_split_cores_test"
    check(config.has_section(split_profile), f"missing [{split_profile}]")
    split_flags = config[split_profile].get("build_flags", "")
    check("ARDUINO_RUNNING_CORE=0" in split_flags,
          "split-core 640x480 profile must run the QL/32 loop on core 0")
    check("QL32_OLIMEX_DMA_REFILL_CORE1_TEST" in split_flags,
          "split-core 640x480 profile must keep VGA refill on core 1")
    check("QL32_OLIMEX_DMA_INTERRUPT_CORE1_TEST" in split_flags,
          "split-core 640x480 profile must keep the VGA EOF interrupt on core 1")

    deeper_split_profile = (
        "env:olimex_esp32_sbc_fabgl_psram_dma_display_640x480_"
        "split_cores_ring160_test"
    )
    check(config.has_section(deeper_split_profile),
          f"missing [{deeper_split_profile}]")
    deeper_split_flags = config[deeper_split_profile].get("build_flags", "")
    check("QL32_OLIMEX_DMA_RING160_TEST" in deeper_split_flags,
          "deeper split-core profile must select the 160-row DMA line ring")
    check("olimex_esp32_sbc_fabgl_psram_dma_display_640x480_split_cores_test"
          in config[deeper_split_profile].get("extends", ""),
          "deeper split-core profile must preserve the baseline core placement")

    print("Olimex PlatformIO and FFS partition configuration is valid (512 KiB SPIFFS).")


if __name__ == "__main__":
    main()
