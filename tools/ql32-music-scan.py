#!/usr/bin/env python3
"""Write the playlist FFS:/music.lst that examples/music-player/musicplayer.bas reads.

BASIC programs cannot run DIR, so this tool types the REPL commands for you
over the board's serial console (each `DIR #file pattern` appends the matching
names to the open playlist file):

    tools/ql32-music-scan.py /dev/ttyUSB0 [--dir SDC:/music --dir FFS:/ ...]

Default directories: SDC:/music if it exists, else SDC:/, plus FFS:/.
The same commands can be typed by hand; see examples/music-player/README.md.
"""
import argparse
import sys
import time

import serial

PATTERNS = ["*.ym", "*.vtx", "*.pt3", "*.sqt", "*.wav"]


def read_prompt(port, timeout=8.0):
    data = b""
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        data += port.read(1024)
        if data.endswith(b"> "):
            break
    return data.decode(errors="replace")


def command(port, text):
    port.write(text.encode() + b"\r")
    return read_prompt(port)


def exists(port, directory):
    reply = command(port, f"DIR #p {directory}")
    return "VM error" not in reply and "not found" not in reply


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("port")
    parser.add_argument("--dir", action="append", dest="dirs")
    args = parser.parse_args()
    port = serial.Serial(args.port, 115200, timeout=0.2)
    port.write(b"\r")
    read_prompt(port, 5)
    command(port, "p=OPEN(2)")
    dirs = args.dirs
    if not dirs:
        dirs = ["SDC:/music" if exists(port, "SDC:/music") else "SDC:/", "FFS:/"]
    command(port, 'f=OPEN("FFS:/music.lst","WRITE")')
    for directory in dirs:
        base = directory if directory.endswith("/") else directory + "/"
        if not exists(port, base):
            print(f"skip {base} (not available)")
            continue
        command(port, f'PRINT #f, "@{base}"')
        for pattern in PATTERNS:
            reply = command(port, f"DIR #f {base}{pattern}")
            if "VM error" in reply:
                print(f"{base}{pattern}: {reply.strip().splitlines()[-2]}")
    command(port, "CLOSE f")
    size = command(port, 'PRINT #p, FLEN(OPEN("FFS:/music.lst","READ"))')
    print("music.lst bytes:", size.strip().splitlines()[-2])
    port.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
