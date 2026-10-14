#!/usr/bin/env python3
"""run_all.py -- one command for the whole speed comparison.

Runs bench/bench_vm.py (the Python reference VM) and bench/bench_vm.c (the new
C core include/ternary_vm.h, optionally linked against the MPTC primitive
layer), parses both outputs and prints a single table with the ratios, the
compiler flags and the exact host, so the numbers in README.md can be checked
by re-running this file.

What is compared
----------------
* vm_program      : the very same bytecode images from programs/, executed
                    R times; nanoseconds per executed instruction.
* tryte_add_value : value-domain saturating tryte add, i.e. what the ISA's ADD
                    instruction does. Python `to_tryte(a+b)` vs C `tvm_clamp`.
* tryte_add_packed: encode + add + decode in the packed 9-trit code domain of
                    ternary_vm.h -- the closest analogue of MPTC's add.
* mptc_ternary_*  : MPTC's own packed trit8 (8 trits, range +-3280) primitives
                    from include/mptc/ternary.h.

Not compared (does not exist): MPTC has no VM/interpreter, so there is no
"same program on MPTC" measurement to make. Only the primitive layer is
comparable, and that comparison is reported as such.

Usage:
    python bench/run_all.py                        # C core only
    python bench/run_all.py --mptc-dir ../MPTC     # + MPTC comparison
"""
from __future__ import annotations

import argparse
import os
import pathlib
import platform
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
BENCH_LINE = re.compile(r"^BENCH\s+(\S+)\s+(.*)$")
KV = re.compile(r"(\w+)=([^\s]+)")


def load_average() -> str:
    try:
        with open("/proc/loadavg") as fh:
            return " ".join(fh.read().split()[:3])
    except OSError:
        return "unknown"


def host_info() -> str:
    model = "unknown CPU"
    try:
        with open("/proc/cpuinfo") as fh:
            for line in fh:
                if line.startswith("model name"):
                    model = line.split(":", 1)[1].strip()
                    break
    except OSError:
        pass
    cores = os.cpu_count() or 0
    return "%s, %s %s, %d cores" % (model, platform.system(), platform.machine(), cores)


def parse(text: str) -> dict:
    """BENCH lines -> {name: {k: value}} (numbers converted to float where useful)."""
    out: dict = {}
    for line in text.splitlines():
        m = BENCH_LINE.match(line.strip())
        if not m:
            continue
        name, rest = m.group(1), m.group(2)
        fields = {}
        for k, v in KV.findall(rest):
            try:
                fields[k] = float(v)
            except ValueError:
                fields[k] = v
        out.setdefault(name, fields)
        if "name" in fields:
            out["%s:%s" % (name, fields["name"])] = fields
    return out


def merge_min(dicts: list) -> dict:
    """Merge repeated benchmark runs, keeping the minimum (least noisy) value."""
    out: dict = {}
    for d in dicts:
        for name, fields in d.items():
            cur = out.setdefault(name, dict(fields))
            for k, v in fields.items():
                if isinstance(v, float) and isinstance(cur.get(k), float):
                    cur[k] = min(cur[k], v)
                else:
                    cur[k] = v
    return out


def run(cmd, **kw) -> str:
    print("$ " + " ".join(str(c) for c in cmd))
    proc = subprocess.run([str(c) for c in cmd], capture_output=True, text=True, **kw)
    sys.stdout.write(proc.stdout)
    if proc.stderr:
        sys.stdout.write(proc.stderr)
    if proc.returncode != 0:
        raise SystemExit("command failed with exit code %d" % proc.returncode)
    return proc.stdout


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--mptc-dir", default=None,
                    help="path to a checkout of https://github.com/kostyk348/MPTC")
    ap.add_argument("--runs", type=int, default=None, help="VM runs per program")
    ap.add_argument("--prim-iter", type=int, default=None, help="primitive iterations")
    ap.add_argument("--cc", default="gcc")
    ap.add_argument("--repeat", type=int, default=3,
                    help="number of C bench repetitions; the per-metric minimum is reported")
    ap.add_argument("--py-repeat", type=int, default=1,
                    help="number of Python bench repetitions; minimum reported")
    args = ap.parse_args()

    py_cmd = [sys.executable, ROOT / "bench" / "bench_vm.py"]
    if args.runs:
        py_cmd += ["--runs", str(args.runs)]
    if args.prim_iter:
        py_cmd += ["--prim-iter", str(args.prim_iter)]

    cflags = ["-std=c99", "-O2", "-Wall", "-Wextra", "-I", ROOT / "include",
              "-I", ROOT / "examples" / "c"]
    extra = ""
    if args.runs:
        extra += " -DVM_RUNS=%d" % args.runs
    if args.prim_iter:
        extra += " -DPRIM_ITER=%d" % args.prim_iter

    with tempfile.TemporaryDirectory() as tmp:
        binary = pathlib.Path(tmp) / "bench_vm"
        c_cmd = [args.cc] + cflags
        mptc = None
        if args.mptc_dir:
            mptc = pathlib.Path(args.mptc_dir).resolve()
            c_cmd += ["-I", mptc / "include", "-DHAVE_MPTC=1"]
        if extra:
            c_cmd += extra.split()
        c_cmd += [ROOT / "bench" / "bench_vm.c", "-o", binary]

        print("=== host ===")
        print(host_info())
        print("=== build ===")
        run(c_cmd)
        print("\n=== run: C core (include/ternary_vm.h) ===")
        c_out = run([binary])
        c_runs = [parse(c_out)]
        for i in range(1, max(1, args.repeat)):
            print("\n--- repeat %d/%d ---" % (i + 1, args.repeat))
            c_runs.append(parse(run([binary])))
        print("\n=== run: Python reference VM (vm/ternary_vm.py) ===")
        py_out = run(py_cmd)
        py_runs = [parse(py_out)]
        for i in range(1, max(1, args.py_repeat)):
            print("\n--- python repeat %d/%d ---" % (i + 1, args.py_repeat))
            py_runs.append(parse(run(py_cmd)))

    c = merge_min(c_runs)
    p = merge_min(py_runs)

    print("\n=== comparison ===")
    print("%-34s %14s %14s %10s" % ("measurement", "Python (ns)", "C core (ns)", "C/Py"))
    print("-" * 78)
    rows = []
    for name in ("add.tasm", "mul.tasm", "fib.tasm"):
        py = p.get("vm_program:%s" % name)
        cc = c.get("vm_program:%s" % name)
        if not py or not cc:
            continue
        rows.append(("vm instruction: %s" % name,
                     py["ns_per_instruction"], cc["ns_per_instruction"]))
    if "tryte_add_value" in p and "tryte_add_value" in c:
        rows.append(("tryte add (value domain)",
                     p["tryte_add_value"]["ns_per_op"], c["tryte_add_value"]["ns_per_op"]))
    for label, pyv, cv in rows:
        print("%-34s %14.2f %14.2f %9.1fx" %
              (label, pyv, cv, pyv / cv if cv else float("nan")))

    if mptc and "mptc_ternary_add8" in c:
        print("\n=== ternary primitive: same operation class, same compiler/flags/machine ===")
        print("%-34s %14s %10s" % ("measurement", "ns/op", "ratio vs MPTC"))
        print("-" * 78)
        mptc_add = c["mptc_ternary_add8"]["ns_per_op"]
        for label, key in (("MPTC ternary_add8 (packed, 8 trits)", "mptc_ternary_add8"),
                           ("MPTC ternary_to_int8", "mptc_to_int8"),
                           ("MPTC ternary_from_int8", "mptc_from_int8"),
                           ("MPTC ternary_dot8", "mptc_ternary_dot8"),
                           ("ternary_vm.h add, packed codes (9 trits)", "tryte_add_packed"),
                           ("ternary_vm.h add, value domain", "tryte_add_value")):
            if key not in c:
                continue
            v = c[key]["ns_per_op"]
            print("%-34s %14.3f %9.2fx" % (label, v, mptc_add / v if v else float("nan")))
        print("\nnotes: MPTC packs 8 trits in uint16_t (range +-3280), ternary_vm.h keeps a")
        print("       9-trit tryte (range +-9841) as int16_t and packs only for STORE.")
        print("       MPTC has no VM/interpreter, so no per-program instruction")
        print("       comparison against MPTC exists; only primitives are comparable.")
        print("       MPTC built from %s" % mptc)
    elif mptc:
        print("\nMPTC comparison requested but the C bench produced no mptc_* numbers.")
    else:
        print("\n(MPTC comparison skipped: pass --mptc-dir /path/to/MPTC)")

    print("\nhost: %s" % host_info())
    print("load average (1/5/15 min): %s" % load_average())
    print("python: %s   compiler: %s" % (sys.version.split()[0], args.cc))
    print("flags: %s %s" % (" ".join(str(f) for f in cflags), extra.strip()))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
