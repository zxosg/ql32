#!/usr/bin/env python3
"""Automated CYD heap checks through the QL/32 serial REPL."""

from __future__ import annotations

import argparse
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
import importlib.util
import json
import re
import statistics
import subprocess
import sys
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FIXTURES = ROOT / "examples" / "memory-tests"
YMODEM_PATH = ROOT / "tools" / "ql32_ymodem.py"
PROFILE = "esp32_2432s028"
PROMPT = b"> "
READY = b"BASIC REPL ready (USB serial)"
METRIC_RE = re.compile(
    rb"MEM\|([a-z0-9_-]+)\|([a-z0-9_-]+)\|(\d+)\s*,\s*(\d+)\s*,\s*(\d+)",
    re.IGNORECASE,
)
TASK_ID_RE = re.compile(rb"TASKID\|[a-z0-9_-]+\|(\d+)", re.IGNORECASE)
ERROR_RE = re.compile(
    rb"(?:BASIC session \d+ VM error:|Transfer failed:|Runtime error:|Out of memory|"
    rb"\b(?:syntax|type|allocation) error\b)",
    re.IGNORECASE,
)


@dataclass(frozen=True)
class HeapSample:
    total: int
    largest: int
    blocks: int


class TestFailure(RuntimeError):
    pass


class SerialConsole:
    def __init__(self, port, timeout: float = 30.0):
        self.port = port
        self.timeout = timeout
        self.buffer = bytearray()
        self.metrics: dict[tuple[str, str], HeapSample] = {}

    def read_until(self, token: bytes, timeout: float | None = None) -> bytes:
        deadline = time.monotonic() + (self.timeout if timeout is None else timeout)
        while True:
            offset = self.buffer.find(token)
            if offset >= 0:
                end = offset + len(token)
                result = bytes(self.buffer[:end])
                del self.buffer[:end]
                return result
            if time.monotonic() >= deadline:
                tail = bytes(self.buffer[-1200:]).decode("utf-8", errors="replace")
                raise TestFailure(f"timed out waiting for {token!r}; received: {tail}")
            chunk = self.port.read(max(1, min(self.port.in_waiting, 512)))
            if chunk:
                self.buffer.extend(chunk)

    def command(self, line: str, timeout: float | None = None) -> bytes:
        self.port.write(line.encode("ascii") + b"\r")
        return self.read_until(PROMPT, timeout)

    def check_output(self, output: bytes, context: str) -> None:
        match = ERROR_RE.search(output)
        if match:
            excerpt = output.decode("utf-8", errors="replace")[-1000:]
            raise TestFailure(f"device error during {context}: {excerpt}")

    def cache_metrics(self, output: bytes) -> None:
        for match in METRIC_RE.finditer(output):
            key = (match.group(1).decode("ascii").lower(),
                   match.group(2).decode("ascii").lower())
            self.metrics[key] = HeapSample(*(int(match.group(i)) for i in (3, 4, 5)))

    def wait_for_boot(self, timeout: float = 90.0) -> bytes:
        banner = self.read_until(READY, timeout)
        prompt = self.read_until(PROMPT, timeout)
        return banner + prompt


def load_serial_module():
    try:
        import serial
        return serial
    except ImportError as error:
        raise TestFailure("pyserial is required: python3 -m pip install pyserial") from error


def load_ymodem_module():
    spec = importlib.util.spec_from_file_location("ql32_ymodem_cli", YMODEM_PATH)
    if spec is None or spec.loader is None:
        raise TestFailure(f"could not load YMODEM utility at {YMODEM_PATH}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    if not hasattr(module, "send_file"):
        raise TestFailure("YMODEM utility does not provide send_file()")
    return module


def git_revision() -> str:
    try:
        result = subprocess.run(
            ["git", "rev-parse", "--short", "HEAD"], cwd=ROOT,
            check=True, capture_output=True, text=True,
        )
        dirty = subprocess.run(
            ["git", "status", "--porcelain"], cwd=ROOT,
            check=True, capture_output=True, text=True,
        ).stdout.strip()
        return result.stdout.strip() + ("-dirty" if dirty else "")
    except (OSError, subprocess.CalledProcessError):
        return "unknown"


def parse_metric(output: bytes, case: str, phase: str) -> HeapSample:
    for match in METRIC_RE.finditer(output):
        got_case = match.group(1).decode("ascii").lower()
        got_phase = match.group(2).decode("ascii").lower()
        if got_case == case.lower() and got_phase == phase.lower():
            return HeapSample(*(int(match.group(i)) for i in (3, 4, 5)))
    excerpt = output.decode("utf-8", errors="replace")[-1200:]
    raise TestFailure(f"missing MEM checkpoint {case}/{phase}; received: {excerpt}")


def sample(console: SerialConsole, case: str, phase: str) -> tuple[HeapSample, bytes]:
    statement = (
        f'PRINT #serial_probe,"MEM|{case}|{phase}|";FREE(0);",";FREE(1);",";FREE(2)'
    )
    output = console.command(statement)
    console.check_output(output, f"{case}/{phase} sample")
    return parse_metric(output, case, phase), output


def median_sample(samples: list[HeapSample]) -> HeapSample:
    return HeapSample(*(int(statistics.median(getattr(item, field) for item in samples))
                         for field in ("total", "largest", "blocks")))


def spread(samples: list[HeapSample], field: str) -> int:
    values = [getattr(item, field) for item in samples]
    return max(values) - min(values)


def serial_metric_dict(sample_value: HeapSample) -> dict[str, int]:
    return asdict(sample_value)


def wait_for_metric(console: SerialConsole, case: str, phase: str,
                    timeout: float = 20.0) -> HeapSample:
    cached = console.metrics.pop((case.lower(), phase.lower()), None)
    if cached is not None:
        return cached
    output = console.read_until(b"MEM|" + case.encode() + b"|" + phase.encode() + b"|", timeout)
    # The token ends at the start of the numeric values; read through the next line ending.
    output += console.read_until(b"\n", timeout)
    console.check_output(output, f"{case}/{phase} worker checkpoint")
    return parse_metric(output, case, phase)


def start_worker(console: SerialConsole, case: str, filename: str) -> tuple[int, bytes]:
    output = console.command(f'test_task = TASK("FFS:/{filename}")')
    console.check_output(output, f"start {case} worker")
    output += console.command(f'PRINT #serial_probe,"TASKID|{case}|";test_task')
    console.cache_metrics(output)
    match = TASK_ID_RE.search(output)
    if not match:
        console.check_output(output, f"read {case} task ID")
        raise TestFailure(f"no task ID returned for {case}: {output[-1000:]!r}")
    task_id = int(match.group(1))
    try:
        console.check_output(output, f"read {case} task ID")
    except TestFailure:
        try:
            kill_worker(console, task_id)
        except Exception:
            pass
        raise
    return task_id, output


def cancel_worker(console: SerialConsole, task_id: int) -> tuple[bytes, str, int]:
    output = console.command(f"KILL {task_id}")
    expected_cancel = re.compile(
        rb"BASIC session " + str(task_id).encode() + rb" VM error: Task cancelled\r?\n",
        re.IGNORECASE,
    )
    console.check_output(expected_cancel.sub(b"", output), f"cancel task {task_id}")
    status = console.command(
        f'PRINT #serial_probe,"TASKRETAIN|";TASKRESULT({task_id},1)'
    )
    console.check_output(status, f"read cancelled task {task_id} result")
    match = re.search(rb"TASKRETAIN\|(CANCELLED)", status, re.IGNORECASE)
    if not match:
        raise TestFailure(f"cancelled task {task_id} result was not retained: {status[-500:]!r}")
    count_output = console.command(
        'PRINT #serial_probe,"TASKCOUNT|";GETTASK(0)'
    )
    count_match = re.search(rb"TASKCOUNT\|(\d+)", count_output)
    if not count_match:
        raise TestFailure(f"could not read task count after cancellation: {count_output[-500:]!r}")
    return output + status + count_output, match.group(1).decode("ascii"), int(count_match.group(1))


def reap_worker(console: SerialConsole, task_id: int) -> tuple[bytes, int]:
    output = console.command(f"KILL {task_id}")
    console.check_output(output, f"reap cancelled task {task_id}")
    output += console.command("TASKS #0")
    if re.search(rb"(?m)^\s*" + str(task_id).encode() + rb"\s+WORKER\b", output):
        raise TestFailure(f"task {task_id} still appears in TASKS after reap")
    count_output = console.command(
        'PRINT #serial_probe,"TASKCOUNT|";GETTASK(0)'
    )
    count_match = re.search(rb"TASKCOUNT\|(\d+)", count_output)
    if not count_match:
        raise TestFailure(f"could not read task count after reap: {count_output[-500:]!r}")
    return output + count_output, int(count_match.group(1))


def kill_worker(console: SerialConsole, task_id: int) -> bytes:
    output, _status, _count = cancel_worker(console, task_id)
    reap_output, _count = reap_worker(console, task_id)
    return output + reap_output


def run_worker_case(console: SerialConsole, name: str, filename: str,
                    cycles: int, tolerance: int, baseline: HeapSample,
                    minimum_drop: int = 0,
                    debugger: bool = False,
                    report_cases: list[dict] | None = None) -> dict:
    case_result = {"name": name, "repetitions": []}
    if report_cases is not None:
        report_cases.append(case_result)
    for repetition in range(1, cycles + 1):
        before, _ = sample(console, name, f"before{repetition}")
        task_id = None
        try:
            task_id, _ = start_worker(console, name, filename)
            worker_id = task_id
            active_report = wait_for_metric(console, name, "active")
            released_report = None
            if name in {"channel", "graphics", "combined"}:
                released_report = wait_for_metric(console, name, "released")
            debug_log = b""
            if debugger:
                debug_log += console.command(f"DEBUG {task_id}")
                debug_log += console.command("WATCH n")
                debug_log += console.command("STATS")
                debug_log += console.command("STEP")
                debug_log += console.command("WHERE")
                debug_log += console.command("CONTINUE")
                console.check_output(debug_log, "debugger attach/inspect/continue")
            after_active, _ = sample(console, name, f"during{repetition}")
            cancel_log, cancelled_status, task_count_after_cancel = cancel_worker(
                console, task_id
            )
            after_cancel, _ = sample(console, name, f"cancelled{repetition}")
            reap_log, task_count_after_reap = reap_worker(console, task_id)
            after_reap, _ = sample(console, name, f"reaped{repetition}")
            task_id = None
            if name in {"channel", "combined"}:
                delete_output = console.command('DELETE "FFS:/ql32_memory_probe.tmp"')
                console.check_output(delete_output, "remove channel test file")
            after, _ = sample(console, name, f"after{repetition}")

            dropped = before.total - active_report.total
            if minimum_drop and dropped < minimum_drop:
                raise TestFailure(
                    f"{name} repetition {repetition} used {dropped} bytes; "
                    f"expected at least {minimum_drop}"
                )
            total_recovered = after.total >= before.total - tolerance
            largest_recovered = after.largest >= before.largest - tolerance
            release_recovered = None
            if released_report is not None and name in {"channel", "graphics"}:
                release_recovered = (
                    released_report.total >= active_report.total - tolerance
                    and released_report.largest >= active_report.largest - tolerance
                )
            case_result["repetitions"].append({
                "repetition": repetition,
                "task_id": worker_id,
                "before": serial_metric_dict(before),
                "active_report": serial_metric_dict(active_report),
                "released_report": (serial_metric_dict(released_report)
                                    if released_report is not None else None),
                "during": serial_metric_dict(after_active),
                "after_cancel": serial_metric_dict(after_cancel),
                "after_reap": serial_metric_dict(after_reap),
                "after": serial_metric_dict(after),
                "task_record_recovery_bytes": after_reap.total - after_cancel.total,
                "cancelled_result_status": cancelled_status,
                "task_count_after_cancel": task_count_after_cancel,
                "task_count_after_reap": task_count_after_reap,
                "active_total_drop": dropped,
                "release_recovered": release_recovered,
                "recovered_total": total_recovered,
                "recovered_largest_block": largest_recovered,
                "idle_baseline_delta": {
                    "total": after.total - baseline.total,
                    "largest": after.largest - baseline.largest,
                },
                "debugger_output": debug_log.decode("utf-8", errors="replace") if debugger else None,
                "cleanup_output": (cancel_log + reap_log).decode("utf-8", errors="replace"),
            })
            if not total_recovered or not largest_recovered:
                raise TestFailure(
                    f"{name} repetition {repetition} did not recover to its pre-run "
                    f"heap within {tolerance} bytes"
                )
            if release_recovered is False:
                raise TestFailure(
                    f"{name} CLOSE/CLOSEWINDOW did not recover within {tolerance} bytes"
                )
        finally:
            if task_id is not None:
                if debugger:
                    try:
                        console.command("CONTINUE")
                    except Exception:
                        pass
                try:
                    kill_worker(console, task_id)
                except Exception:
                    pass
    return case_result


def run_graphics_case(console: SerialConsole, cycles: int, tolerance: int,
                      baseline: HeapSample, report_cases: list[dict]) -> dict:
    # CanvasArena allocates canvas buffers independently. CLOSEWINDOW releases
    # pixel storage; task reaping releases the remaining session allocations.
    return run_worker_case(console, "graphics", "ql32_mem_graphics.bas",
                           cycles, tolerance, baseline, minimum_drop=2 * 1024,
                           report_cases=report_cases)


def run_combined_case(console: SerialConsole, cycles: int, tolerance: int,
                      baseline: HeapSample, report_cases: list[dict]) -> dict:
    return run_worker_case(console, "combined", "ql32_mem_combined.bas",
                           cycles, tolerance, baseline, minimum_drop=8 * 1024,
                           report_cases=report_cases)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="serial device used by the CYD REPL")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--cycles", type=int, default=5,
                        help="repetitions per workload (default: 5)")
    parser.add_argument("--report", type=Path, default=Path("memory-test-report.json"),
                        help="JSON report destination (default: ./memory-test-report.json)")
    parser.add_argument("--firmware-revision",
                        help="revision of the image on the board; defaults to local git revision")
    parser.add_argument("--skip-upload", action="store_true",
                        help="use BASIC fixtures already deployed to FFS")
    args = parser.parse_args()
    if args.cycles < 5:
        parser.error("--cycles must be at least 5 to preserve the planned recovery checks")

    serial = load_serial_module()
    ymodem = load_ymodem_module()
    fixtures = [
        ("ql32_mem_array.bas", FIXTURES / "array-worker.bas"),
        ("ql32_mem_channel.bas", FIXTURES / "channel-worker.bas"),
        ("ql32_mem_graphics.bas", FIXTURES / "graphics-worker.bas"),
        ("ql32_mem_debugger.bas", FIXTURES / "debugger-worker.bas"),
        ("ql32_mem_combined.bas", FIXTURES / "combined-worker.bas"),
    ]
    for device_name, local_path in fixtures:
        if not local_path.is_file():
            raise TestFailure(f"missing BASIC fixture: {local_path}")

    port = serial.Serial(port=None, baudrate=args.baud, timeout=0.1,
                         write_timeout=10)
    port.port = args.port
    port.dtr = False
    port.rts = False
    report = {
        "schema": "ql32-memory-test-v2",
        "board_profile": PROFILE,
        "firmware_revision": args.firmware_revision or git_revision(),
        "started_at": datetime.now(timezone.utc).isoformat(),
        "cycles": args.cycles,
        "metrics": ["FREE(0): total", "FREE(1): largest block", "FREE(2): free block count"],
        "cases": [],
        "warnings": [],
        "passed": False,
    }

    try:
        with port:
            console = SerialConsole(port)
            if args.skip_upload:
                port.write(b"\r")
                console.read_until(PROMPT, 90)
            else:
                port.write(b"\r")
                console.read_until(PROMPT, 90)
                for device_name, local_path in fixtures:
                    ymodem.send_file(
                        port, local_path, f"FFS:/{device_name}",
                        progress=lambda _sent, _size: None,
                    )
                    console.read_until(PROMPT, 30)

            # Start every run from an empty scheduler/session heap, including
            # when a previous failed run left an idle worker behind.
            port.write(b"REBOOT\r")
            console.buffer.clear()
            console.wait_for_boot(90)

            # #0 is rendered by the CYD TFT console. Keep telemetry on a
            # dedicated serial output channel for the USB serial harness.
            setup_output = console.command("serial_probe = OPEN(2)")
            console.check_output(setup_output, "open supervisor serial telemetry channel")

            # Normalize the heap and collect the planned idle baseline.
            warmup = [sample(console, "idle", f"warm{i}")[0] for i in range(1, 4)]
            baseline_samples = [sample(console, "idle", f"base{i}")[0]
                                for i in range(1, 6)]
            baseline = median_sample(baseline_samples)
            idle_spread = max(spread(baseline_samples, "total"),
                              spread(baseline_samples, "largest"))
            tolerance = max(2048, 2 * idle_spread)
            report["baseline"] = {
                "warmup": [serial_metric_dict(item) for item in warmup],
                "samples": [serial_metric_dict(item) for item in baseline_samples],
                "median": serial_metric_dict(baseline),
                "idle_spread_bytes": idle_spread,
                "recovery_tolerance_bytes": tolerance,
            }

            run_worker_case(
                console, "array", "ql32_mem_array.bas", args.cycles,
                tolerance, baseline, minimum_drop=8 * 1024,
                report_cases=report["cases"],
            )
            run_worker_case(
                console, "channel", "ql32_mem_channel.bas", args.cycles,
                tolerance, baseline, minimum_drop=2 * 1024,
                report_cases=report["cases"],
            )
            run_graphics_case(console, args.cycles, tolerance, baseline, report["cases"])
            run_worker_case(
                console, "debugger", "ql32_mem_debugger.bas", args.cycles,
                tolerance, baseline, minimum_drop=2 * 1024, debugger=True,
                report_cases=report["cases"],
            )
            run_combined_case(console, args.cycles, tolerance, baseline, report["cases"])

            # Trend information is diagnostic; allocator block counts are not a pass gate.
            for case in report["cases"]:
                counts = [item["after"]["blocks"] for item in case["repetitions"]]
                declines = sum(right < left for left, right in zip(counts, counts[1:]))
                if declines >= max(3, len(counts) - 2):
                    report["warnings"].append(
                        f"{case['name']}: free-block count declined in {declines}/"
                        f"{len(counts) - 1} successive comparisons"
                    )
                totals = [item["after"]["total"] for item in case["repetitions"]]
                total_declines = sum(right < left for left, right in zip(totals, totals[1:]))
                total_drift = totals[-1] - totals[0] if totals else 0
                case["post_run_total_drift"] = total_drift
                if total_declines >= max(3, len(totals) - 2) and total_drift < -tolerance:
                    report["warnings"].append(
                        f"{case['name']}: post-run total heap declined by {-total_drift} bytes; "
                        "task records were removed; review persistent allocations and allocator behavior"
                    )

            # A fresh firmware boot verifies that the post-stress state is not the only
            # repeatable baseline. REBOOT is an existing monitor command.
            port.write(b"REBOOT\r")
            console.buffer.clear()
            console.wait_for_boot(90)
            setup_output = console.command("serial_probe = OPEN(2)")
            console.check_output(setup_output, "reopen serial telemetry after reboot")
            restart_warmup = [sample(console, "restart", f"warm{i}")[0]
                              for i in range(1, 4)]
            restart_samples = [sample(console, "restart", f"sample{i}")[0]
                               for i in range(1, 6)]
            restart_median = median_sample(restart_samples)
            restart_ok = (
                restart_median.total >= baseline.total - tolerance
                and restart_median.largest >= baseline.largest - tolerance
            )
            report["restart_check"] = {
                "warmup": [serial_metric_dict(item) for item in restart_warmup],
                "samples": [serial_metric_dict(item) for item in restart_samples],
                "median": serial_metric_dict(restart_median),
                "recovered_total": restart_median.total >= baseline.total - tolerance,
                "recovered_largest_block": restart_median.largest >= baseline.largest - tolerance,
            }
            if not restart_ok:
                raise TestFailure("fresh-boot heap baseline differs beyond recovery tolerance")

        report["passed"] = True
    except Exception as error:
        report["failure"] = str(error)
        print(f"memory test failed: {error}", file=sys.stderr)
    finally:
        report["finished_at"] = datetime.now(timezone.utc).isoformat()
        report_path = args.report.expanduser().resolve()
        report_path.parent.mkdir(parents=True, exist_ok=True)
        report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(f"Report: {report_path}")

    if report["passed"]:
        print(f"QL/32 memory tests passed for {PROFILE}")
        for warning in report["warnings"]:
            print(f"warning: {warning}", file=sys.stderr)
        return 0
    return 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except TestFailure as error:
        print(f"memory test setup failed: {error}", file=sys.stderr)
        raise SystemExit(2)
