#!/usr/bin/env python3
"""Run repeatable, non-CTest QL/32 VM timing workloads."""

import argparse
import pathlib
import statistics
import subprocess
import sys
import time


ROOT = pathlib.Path(__file__).resolve().parent.parent
WORKLOADS = [
    ("loops", ROOT / "benchmarks/loop.bas", True),
    ("recursion", ROOT / "benchmarks/recursion.bas", True),
    ("array fill", ROOT / "benchmarks/array_fill.bas", True),
    ("string concat", ROOT / "benchmarks/string_concat.bas", True),
    ("Mandelbrot", ROOT / "benchmarks/mandelbrot.bas", True),
]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("ql32", type=pathlib.Path, help="path to the ql32 executable")
    parser.add_argument("--runs", type=int, default=5, help="runs per workload (default: 5)")
    args = parser.parse_args()
    if args.runs < 1:
        parser.error("--runs must be at least 1")

    executable = args.ql32.resolve()
    if not executable.is_file():
        parser.error(f"executable not found: {executable}")

    print(f"QL/32 VM benchmarks ({args.runs} runs each; median wall time)")
    for name, source, console_only in WORKLOADS:
        samples = []
        for _ in range(args.runs):
            start = time.perf_counter()
            command = [str(executable)]
            if console_only:
                command.append("--console")
            command.append(str(source))
            result = subprocess.run(
                command,
                cwd=ROOT,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.PIPE,
                text=True,
                check=False,
                timeout=120,
            )
            samples.append(time.perf_counter() - start)
            if result.returncode != 0:
                print(f"{name} failed ({result.returncode}): {result.stderr}", file=sys.stderr)
                return result.returncode or 1
        print(f"{name:16} {statistics.median(samples):.6f} s")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
