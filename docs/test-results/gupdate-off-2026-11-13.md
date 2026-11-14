# GUPDATE 0 regression test — 2026-11-13

## Symptom

On the Olimex ESP32-SBC-FabGL PSRAM double-buffer profile, an application
changed its canvas after `GUPDATE 0`, but the new frame appeared before the
explicit bare `GUPDATE` command.

## Root cause and correction

`WindowedService::invalidateSystemRegion()` temporarily changed the global
update mode to automatic while redrawing system UI regions. The Olimex
double-buffer presenter cannot present only a partial region, so it fell back
to presenting a complete frame. That full-frame copy exposed pending canvas
changes despite `GUPDATE 0`.

System-region invalidation now uses the current update mode and no longer
overrides it. UI redraws are therefore deferred together with canvas redraws
while updates are disabled.

## Verification

- Linux `ql32_graphics_tests` passed, including VM-level coverage for
  `GUPDATE 0`, drawing, bare `GUPDATE`, and restoring `GUPDATE 1`.
- Linux `ql32_graphic_ui_tests` passed, including checks that UI invalidation
  is held while updates are off and appears after an explicit update.
- Olimex ESP32-SBC-FabGL PSRAM double-buffer firmware built and flashed
  successfully over `/dev/ttyUSB0`.
- On the device, the diagnostic displayed Screen A during the `GUPDATE 0`
  interval; Screen B appeared only after bare `GUPDATE`.

## Retest on final packed-span firmware — 2026-11-14

The saved fixture was transferred to `FFS:` over YMODEM and run on the final
normal Olimex profile. Serial markers confirmed the five-second update-off
interval and that execution reached the explicit `GUPDATE` and automatic-mode
restoration. The owner confirmed Screen A remained visible throughout the hold
and Screen B appeared only after bare `GUPDATE`. The program completed without
a VM error.
