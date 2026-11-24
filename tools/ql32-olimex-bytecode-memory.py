#!/usr/bin/env python3
"""Olimex hardware measurement: usage hw_measure2.py LABEL OUT.json [--no-upload]"""
import json, re, sys, time
from pathlib import Path
import serial

ROOT = Path(__file__).resolve().parent.parent
WORK = Path("/tmp/ql32-olimex-bytecode-memory")
sys.path.insert(0, str(ROOT / "tools"))
import ql32_ymodem

label, out_path = sys.argv[1], sys.argv[2]
upload = "--no-upload" not in sys.argv
port = serial.Serial("/dev/ttyUSB0", 115200, timeout=0.2)


def read_until(token, timeout=30.0):
    deadline = time.monotonic() + timeout
    buf = bytearray()
    while time.monotonic() < deadline:
        buf += port.read(4096)
        if token in buf:
            break
    return bytes(buf)


def cmd(line, timeout=30.0):
    port.reset_input_buffer()
    port.write(line.encode() + b"\r")
    return read_until(b"\n> ", timeout).decode("utf-8", "replace")


def reboot():
    port.write(b"REBOOT\r")
    read_until(b"BASIC REPL ready", 90)
    read_until(b"> ", 30)
    time.sleep(1)
    cmd("x = OPEN(2)")


def mem(text, tag):
    m = re.search(r"MEM\|%s\|(\d+),(\d+),(\d+)" % re.escape(tag), text)
    return {"total": int(m.group(1)), "largest": int(m.group(2)), "blocks": int(m.group(3))} if m else None


def heap(tag):
    o = cmd('PRINT #x,"MEM|%s|";FREE(0);",";FREE(1);",";FREE(2)' % tag)
    return mem(o, tag) or {"error": o[-200:]}


def start_task(name, tokens, timeout=20.0):
    port.reset_input_buffer()
    port.write(f't = TASK("FFS:/{name}")\r'.encode())
    deadline = time.monotonic() + timeout
    buf = bytearray()
    while time.monotonic() < deadline and not any(t in buf for t in tokens):
        buf += port.read(4096)
    time.sleep(1.0)
    buf += port.read(8192)
    o = cmd('PRINT #x,"TID|";t')
    m = re.search(r"TID\|(\d+)", o)
    return (int(m.group(1)) if m else None), buf.decode("utf-8", "replace")


def reap(tid):
    print("reap", tid, flush=True)
    cmd(f"KILL {tid}")
    cmd(f"KILL {tid}")


def pcode(n):
    return f"p = OPEN(2)\nPRINT #p,\"DIM|before|\";FREE(0);\",\";FREE(1)\n"


# ---- fixtures -------------------------------------------------------------
WORK.mkdir(parents=True, exist_ok=True)
GOLDEN = ["abbreviations", "arrays", "array_by_reference", "block_if", "expression_rules",
          "for_exit", "for_inline", "for_ranges", "functions", "language_slice",
          "numeric_portable", "print_separators", "procedure_locals", "repeat_control",
          "repeat_inline", "select", "select_edges"]
files = {}
for g in GOLDEN:
    src = (ROOT / "tests/cli" / f"cli_{g}.bas").read_text()
    files[f"g_{g}.bas"] = "zzser = OPEN(2)\nCHANNEL #1 TO #zzser\n" + src.rstrip("\n") + '\nPRINT "GOLDEN-DONE"\n'
body = ["p = OPEN(2)", "n = 1", 's$ = ""']
for i in range(400):
    body += ["n = n + 3 * (n MOD 7) + 1", 's$ = "abc" + "defg"', "n = n MOD 1000"]
body += ['PRINT #p,"MEM|big|";FREE(0);",";FREE(1);",";FREE(2)', "REPEAT k", "i = i + 1", "END REPEAT k"]
files["foot_big.bas"] = "\n".join(body) + "\n"
files["foot_tiny.bas"] = 'p = OPEN(2)\nPRINT #p,"MEM|tiny|";FREE(0);",";FREE(1);",";FREE(2)\nREPEAT k\ni = i + 1\nEND REPEAT k\n'
files["foot_arr.bas"] = (ROOT / "examples/memory-tests/array-worker.bas").read_text().replace("MEM|array|active|", "MEM|array|")
DIMS = {"d60000": ("a", 60000), "d150000": ("a", 150000), "d450000": ("a", 450000),
        "d1000001": ("a", 1000001), "s2000": ("s$", 2000)}
for name, (var, n) in DIMS.items():
    assign = f'{var}({n}) = "q"' if var.endswith("$") else f"{var}({n}) = 5"
    files[f"{name}.bas"] = (
        f'p = OPEN(2)\nPRINT #p,"MEM|{name}-before|";FREE(0);",";FREE(1);",";FREE(2)\n'
        f"DIM {var}({n})\n{assign}\n"
        f'PRINT #p,"MEM|{name}-after|";FREE(0);",";FREE(1);",";FREE(2)\n'
        f'PRINT #p,"READBACK|{name}|";{var}({n})\nPRINT #p,"DIMDONE"\n')
for name, text in files.items():
    (WORK / name).write_text(text)

report = {"label": label, "time": time.strftime("%Y-%m-%dT%H:%M:%S")}
port.write(b"\r")
read_until(b"> ", 10)
if upload:
    for name in files:
        ql32_ymodem.send_file(port, str(WORK / name), f"FFS:/{name}", progress=lambda *a: None)
        print("uploaded", name, flush=True)

print("reboot1", flush=True)
reboot()
print("rebooted", flush=True)
report["idle"] = [heap(f"idle{i}") for i in range(3)]

# ---- DIM boundary (each in its own task) -------------------------------------
dim = {}
for name in DIMS:
    tid, o = start_task(f"{name}.bas", (b"DIMDONE", b"VM error"), 15)
    dim[name] = {"before": mem(o, name + "-before"), "after": mem(o, name + "-after"),
                 "readback": (re.search(r"READBACK\|%s\|(\S+)" % name, o) or [None, None])[1],
                 "error": (re.findall(r"VM error: [^\r\n]*", o) or [None])[0]}
    time.sleep(0.5)
    if tid is not None:
        reap(tid)
report["dim"] = dim

# ---- footprint ----------------------------------------------------------------
reboot()
foot = {"idle": heap("f0")}
for name, tag in (("foot_tiny.bas", "tiny"), ("foot_big.bas", "big"), ("foot_arr.bas", "array")):
    tid, o = start_task(name, (b"MEM|%s|" % tag.encode(),), 15)
    foot[tag] = {"in_program": mem(o, tag), "from_repl": heap("fr_" + tag)}
    if tid is not None:
        reap(tid)
    foot[tag]["after_kill"] = heap("fk_" + tag)
report["footprint"] = foot

# ---- golden -------------------------------------------------------------------
reboot()
golden = {}
for g in GOLDEN:
    tid, o = start_task(f"g_{g}.bas", (b"GOLDEN-DONE", b"VM error"), 15)
    done = "GOLDEN-DONE" in o
    text = o.replace("\r", "").replace("GOLDEN-DONE", "").strip()
    golden[g] = {"done": done, "output": text}
    time.sleep(0.5)
    if tid is not None:
        reap(tid)
report["golden"] = golden

# ---- MODE switching ----------------------------------------------------------
reboot()
modes = {"start": heap("m0"), "log": []}
for i in range(12):
    o = cmd(f"MODE {i % 3}", 20)
    m = re.search(r"heap before allocation: free=(\d+), largest=(\d+)", o)
    modes["log"].append({"mode": i % 3, "dma_free": int(m.group(1)) if m else None,
                         "dma_largest": int(m.group(2)) if m else None,
                         "error": "error" in o.lower()})
    cmd("CLS")
modes["end"] = heap("m1")
report["mode"] = modes

# ---- load check: tasks while switching modes -----------------------------------
tid, _ = start_task("foot_big.bas", (b"MEM|big|",), 15)
loadlog = []
for i in range(6):
    o = cmd(f"MODE {i % 3}", 20)
    loadlog.append({"mode": i % 3, "error": "error" in o.lower(), "text": o.strip()[-160:]})
report["mode_with_task"] = {"log": loadlog, "heap": heap("ml")}
if tid is not None:
    reap(tid)

for name in files:
    cmd(f'DELETE "FFS:/{name}"')
cmd("MODE 2")
report["final"] = heap("fin")
Path(out_path).write_text(json.dumps(report, indent=2))
print("wrote", out_path)
