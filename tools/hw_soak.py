#!/usr/bin/env python3
"""Host-first driver for the Phase 3 VM soak corpus.

This mode validates the REF/CAND corpus, repeats candidate load/run/free cycles,
starts concurrent host processes, and optionally runs a timed mixed workload.
It does not emulate ESP32 heap behavior or replace the device soak.
"""

from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
from datetime import datetime, timezone
import json
from pathlib import Path
import platform
import subprocess
import sys
import time


ROOT = Path(__file__).resolve().parents[1]
SMOKE_CASES = ("cli_numeric_portable", "cli_arrays", "cli_functions")
FILE_ARGUMENT_CASES = {"cli_file", "cli_language_slice", "b4_extra_opcodes",
                       "b4_call_depth_limit"}


class SoakFailure(RuntimeError):
    pass


def discover_corpus() -> list[Path]:
    fixtures = list((ROOT / "tests/cli").glob("*.bas"))
    fixtures.extend((ROOT / "tests/language").glob("b4_*.bas"))
    return sorted(path for path in fixtures
                  if path.with_suffix(".out").is_file()
                  and path.with_suffix(".err").is_file())


def expected_result(source: Path) -> tuple[int, bytes, bytes]:
    stdout = source.with_suffix(".out").read_bytes()
    stderr = source.with_suffix(".err").read_bytes()
    return (1 if stderr else 0, stdout, stderr)


def run_case(binary: Path, source: Path) -> tuple[int, bytes, bytes]:
    command = [str(binary), "--console"]
    if source.stem in FILE_ARGUMENT_CASES:
        command.append(str(source))
        input_bytes = None
    else:
        input_bytes = source.read_bytes()
    try:
        result = subprocess.run(command, cwd=ROOT, input=input_bytes,
                                capture_output=True, timeout=120, check=False)
    except subprocess.TimeoutExpired as error:
        raise SoakFailure(f"{source.name} timed out on {binary}") from error
    return result.returncode, result.stdout, result.stderr


def check_corpus(reference: Path, candidate: Path) -> list[dict[str, object]]:
    rows = []
    corpus = discover_corpus()
    if not corpus:
        raise SoakFailure("no CLI corpus fixtures with .out and .err files were found")
    for source in corpus:
        expected = expected_result(source)
        ref_result = run_case(reference, source)
        cand_result = run_case(candidate, source)
        if ref_result != expected:
            raise SoakFailure(f"REF disagrees with expected files for {source.name}")
        if cand_result != expected:
            raise SoakFailure(f"CAND differs from REF/expected files for {source.name}")
        rows.append({"case": str(source.relative_to(ROOT)), "exit_code": cand_result[0],
                     "stdout_bytes": len(cand_result[1]), "stderr_bytes": len(cand_result[2]),
                     "identical": True})
    return rows


def run_stress_cycle(candidate: Path, cycle: int) -> dict[str, object]:
    case = SMOKE_CASES[cycle % len(SMOKE_CASES)]
    source = ROOT / "tests/cli" / f"{case}.bas"
    actual = run_case(candidate, source)
    expected = expected_result(source)
    if actual != expected:
        raise SoakFailure(f"candidate cycle {cycle + 1} failed on {case}")
    return {"cycle": cycle + 1, "case": case, "passed": True}


def run_host_stress(candidate: Path, cycles: int, workers: int,
                    soak_seconds: int) -> dict[str, object]:
    started = time.perf_counter()
    cycle_rows = []
    for cycle in range(cycles):
        cycle_rows.append(run_stress_cycle(candidate, cycle))

    concurrent_rows = []
    with ThreadPoolExecutor(max_workers=workers) as pool:
        futures = [pool.submit(run_stress_cycle, candidate, index)
                   for index in range(workers)]
        for future in as_completed(futures):
            concurrent_rows.append(future.result())

    timed_runs = 0
    deadline = time.monotonic() + soak_seconds
    while soak_seconds and time.monotonic() < deadline:
        run_stress_cycle(candidate, timed_runs)
        timed_runs += 1

    return {
        "cycles": cycle_rows,
        "concurrent_workers": workers,
        "concurrent_runs": concurrent_rows,
        "timed_soak_seconds_requested": soak_seconds,
        "timed_soak_runs": timed_runs,
        "elapsed_seconds": round(time.perf_counter() - started, 3),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", required=True, type=Path, help="B3 ql32 executable")
    parser.add_argument("--candidate", required=True, type=Path, help="B4 ql32 executable")
    parser.add_argument("--cycles", type=int, default=200,
                        help="candidate host load/run/free process cycles (default: 200)")
    parser.add_argument("--workers", type=int, default=3,
                        help="concurrent candidate host processes (default: 3)")
    parser.add_argument("--soak-seconds", type=int, default=0,
                        help="optional timed host workload; 1800 approximates the device duration")
    parser.add_argument("--report", type=Path, default=Path("build/hw-soak-host-report.json"))
    args = parser.parse_args()
    if args.cycles < 0 or args.workers < 1 or args.soak_seconds < 0:
        parser.error("cycles/soak-seconds must be nonnegative and workers must be positive")

    reference = args.reference.expanduser().resolve()
    candidate = args.candidate.expanduser().resolve()
    for binary in (reference, candidate):
        if not binary.is_file():
            parser.error(f"executable not found: {binary}")

    report: dict[str, object] = {
        "schema": "ql32-b4-host-soak-v1",
        "mode": "host-only",
        "device_gate_replacement": False,
        "reference": str(reference),
        "candidate": str(candidate),
        "host": platform.platform(),
        "started_at": datetime.now(timezone.utc).isoformat(),
        "passed": False,
    }
    try:
        report["differential_corpus"] = check_corpus(reference, candidate)
        report["stress"] = run_host_stress(candidate, args.cycles,
                                           args.workers, args.soak_seconds)
        report["passed"] = True
    except (OSError, SoakFailure, subprocess.SubprocessError) as error:
        report["failure"] = str(error)
        print(f"host soak failed: {error}", file=sys.stderr)
    finally:
        report["finished_at"] = datetime.now(timezone.utc).isoformat()
        report_path = args.report.expanduser().resolve()
        report_path.parent.mkdir(parents=True, exist_ok=True)
        report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(f"Report: {report_path}")

    if report["passed"]:
        corpus = report["differential_corpus"]
        print(f"Host-only soak passed: {len(corpus)} differential fixtures")
        print("This does not validate embedded heap recovery or replace T11 device soak.")
        return 0
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
