#!/usr/bin/env python3
"""Run the portable golden programs (tests/cli/cli_*.bas) on a QL/32 board.

Each program is uploaded over YMODEM as a file and started with TASK(), with
channel #1 routed to the serial endpoint, so it runs as a stored program on any
target with a serial REPL and a writable FFS:/SDC: drive. Output is compared
with the host golden (.out) exactly.

    tools/ql32-golden-device.py /dev/ttyACM0 [--drive FFS:] [name ...]
"""
import argparse
import pathlib
import sys
import time

import serial

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import ql32_ymodem  # noqa: E402

# Programs that need stdin, files, a UI, numbered stored lines (abbreviations)
# or that deliberately fail are not portable to a serial-only TASK run.
PORTABLE = ["arrays", "array_by_reference", "block_if",
            "expression_rules", "for_exit", "for_inline", "for_ranges", "functions",
            "language_slice", "numeric_portable", "print_separators",
            "procedure_locals", "repeat_control", "repeat_inline", "select",
            "select_edges"]
DONE = b"GOLDEN-DONE"


def read_until(port, tokens, timeout):
    buffer = b""
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        buffer += port.read(512)
        if any(token in buffer for token in tokens):
            break
    return buffer


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("port")
    parser.add_argument("--drive", default="FFS:")
    parser.add_argument("names", nargs="*")
    args = parser.parse_args()
    names = args.names or PORTABLE
    port = serial.Serial(args.port, 115200, timeout=0.2)
    port.write(b"\r")
    read_until(port, [b"> "], 5)
    failures = 0
    for name in names:
        source = (ROOT / "tests/cli" / f"cli_{name}.bas").read_text()
        expected = (ROOT / "tests/cli" / f"cli_{name}.out").read_text()
        program = ("zzser = OPEN(2)\nCHANNEL #1 TO #zzser\n" + source.rstrip("\n") +
                   '\nPRINT "GOLDEN-DONE"\n')
        path = pathlib.Path("/tmp") / f"g_{name}.bas"
        path.write_text(program)
        remote = f"{args.drive}/g_{name}.bas"
        ql32_ymodem.send_file(port, str(path), remote, progress=lambda *a: None)
        time.sleep(0.5)
        port.reset_input_buffer()
        port.write(f't = TASK("{remote}")\r'.encode())
        raw = read_until(port, [DONE + b"\r", DONE + b"\n", b"VM error"], 20)
        text = raw.decode("utf-8", "replace").replace("\r", "")
        # Drop the echoed TASK command line, then keep output up to the marker.
        body = text.split("\n", 1)[-1].split("GOLDEN-DONE")[0]
        actual = body.strip("\n")
        if actual.startswith("> "):
            actual = actual[2:]
        ok = "VM error" not in text and actual == expected.strip("\n")
        print(("PASS " if ok else "FAIL ") + name)
        if not ok:
            failures += 1
            print("  expected:", repr(expected))
            print("  actual:  ", repr(actual))
        port.write(f'DELETE "{remote}"\r'.encode())
        read_until(port, [b"> "], 5)
    port.close()
    print(f"{len(names) - failures}/{len(names)} golden programs match")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
