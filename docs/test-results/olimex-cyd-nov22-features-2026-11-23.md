# Nov 22 CYD feature set on Olimex and CYD (2026-11-23)

Branch contains origin/main b12d4f5 (PRs #1-#19) plus the local FFS hold commits.
Features exercised: MIRROR (text and graphics), COPY/MOVE/DELETE wildcard/RMDIR/MKDIR/CAT, YMODEM overwrite via temp file.
Ports: Olimex `/dev/ttyUSB0` (usb-0:10), CYD `/dev/ttyUSB1` (by-id usb-1a86_USB_Serial, usb-0:4.4.4).

| Feature | Built | Host-tested | Olimex (olimex_esp32_sbc_retro) | CYD (esp32_2432s028_spiffs) |
|---|---|---|---|---|
| Build / CTest 89/89 | yes | yes | n/a | n/a |
| MKDIR, COPY (refuse overwrite; FORCE), MOVE, CAT with sizes/space, wildcard DELETE (Y/N prompt, "no files match" error), RMDIR FORCE on FFS | yes | yes | verified | verified |
| COPY/MOVE FFS: <-> SDC:, MKDIR/RMDIR on SDC | yes | yes | verified (SD present) | CAT SDC: verified; copy not run |
| MIRROR #1 TO file: text mirrored, LIST, self-mirror rejected, DELETE stops mirroring | yes | yes | verified (file content read back) | verified |
| MIRROR graphics (RECT/LINE/CIRCLE) to a window | yes | yes | verified visually (owner) | verified visually (owner) |
| YMODEM overwrite of existing FFS file x3 (atomic temp+rename) | yes | yes | verified, 3/3 | verified, 3/3 |

Notes
- The serial REPL redirects `#1` to serial when `CHANNEL #1 TO #p` is used; graphics need `CHANNEL #1 TO #1` first ("selected channel is not a graphics window" otherwise). Not a bug.
- `rmdir is unnecessary in SPIFFS` is an ESP VFS log line printed on FFS RMDIR; harmless.
- Not tested: COPY from console/serial input (Ctrl-Z), heap admission/canvas-resize limits under OOM, tasks/worker canvases on hardware, ISOLATED boot default on Olimex.
- Earlier-noted intermittent "data block retry limit exceeded" on the first Olimex upload after reset persists.
