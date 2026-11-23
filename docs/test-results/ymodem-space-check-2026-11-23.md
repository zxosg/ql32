# YMODEM receive: space pre-check and stored-size verification, 2026-11-23

## Problem
Uploading a 600 KB file into a partly full FFS reported "Transfer received successfully" but stored 240-252 KB. Nothing in the write path reported an error: SPIFFS can drop data near full/fragmented without a short `write()`. (BASIC `PRINT #f` writes did report "channel write failed" when filling an empty FFS, so the loss is alignment/timing dependent.) An attempt to catch it per write by comparing `File::size()` with `position()` produced false failures on a healthy volume (SPIFFS size lags until close) and was removed.

## Change
- `receiveProtocol` calls an admit callback with the declared size before the header ACK. `ymodemReceiveFile` uses `StorageService::space()` and rejects with `not enough space: need N, free M` (CAN CAN, temp file removed, original untouched). The existing file is not subtracted: the temp file coexists with it until commit.
- New `StorageSpace::reserve`; `SdStorage` sets `max(total/8, 16 KB)` for `FFS` (a SPIFFS volume refuses to fill the last ~49 KB of 475 KB; with many small files even more). SDC and unknown-space drives: no reserve.
- Before install, `storage.size(temp)` must equal the declared size, else `stored size X does not match declared N` and the original is kept. This is the real safety net; the pre-check is for fast, clear rejection.
- No sender change; the size field was already mandatory (`parseFileSize`).

## Host-tested
CTest 89/89. `ymodem_tests`: oversize rejected with the exact message, sender cancelled, original and temp files intact; exact fit (9000) accepted, +1 byte rejected; unknown space accepted; stored-size mismatch not installed. `DirectoryStorage` lost `final` so the test can override `space()`/`size()`.

## Built
olimex_esp32_sbc_retro (1,964,789 B flash / 57,260 B RAM), olimex_esp32_sbc_fabgl_psram, esp32_2432s028_spiffs (1,802,577 B / 52,172 B), ql32_tenstar_esp32s3, lilygo_vga32.

## Verified on hardware
| Board | Result |
|---|---|
| Tenstar, 146 KB stored | 614400/400000/300000/250000 rejected in ~2.5 s ("free 242623"), files unchanged; 200000 and 242000 accepted; 242600 passed the pre-check but the commit check caught the truncated store (237480 of 242600) and kept FFS clean; 230000 failed cleanly with "channel write failed". Empty FFS: 397000 and larger rejected ("free 390462") |
| Olimex retro FFS (82 small files, 241 KB "free") | 614400 rejected; 150000 passed the pre-check but only ~126-130 KB got stored: now reported as `stored size ... does not match declared 150000` with no file left (before: silent success); 50000 accepted. Small-file overhead makes the real capacity lower than free-reserve, so the commit check matters |
| Olimex retro SDC | 614400 and 300000 accepted (no change) |
| CYD spiffs | 614400 rejected ("free 608581"); 300000 and 150000 accepted with 146 KB stored |

Open: the reserve is a heuristic; an exact-fit upload near the limit can still fail late (cleanly, after the transfer time) or on Olimex with fragmented FFS. The Olimex first-upload-after-reset "data block retry limit" flake appeared once more (undiagnosed). Olimex ends on olimex_esp32_sbc_retro, CYD on spiffs, Tenstar on the new build; ports released.
