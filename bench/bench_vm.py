#!/usr/bin/env python3
"""bench_vm.py -- Python reference VM benchmark.

Measures the same quantities as bench/bench_vm.c, in the same output format,
so bench/run_all.py can put both side by side:

  * ns per executed VM instruction for each program in programs/;
  * ns per value-domain saturating tryte add (what the ISA's ADD does).

Reference-side numbers are dominated by CPython interpreter overhead: the point
of the comparison is the *ratio* against the C core running the same bytecode
image, not the absolute cost of the algorithm.

Usage:
    python bench/bench_vm.py [--runs N] [--prim-iter N]
"""
from __future__ import annotations

import argparse
import pathlib
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "vm"))

from asm import assemble  # noqa: E402
from ternary_vm import TernaryVM, to_tryte  # noqa: E402

PROGRAMS = [
    ("add.tasm", [12]),
    ("mul.tasm", [48]),
    ("fib.tasm", [1, 1, 2, 3, 5, 8, 13, 21, 34, 55]),
]

DEFAULT_RUNS = 20000
DEFAULT_PRIM_ITER = 200000  # CPython is ~50x slower per op than C


def bench_programs(runs: int) -> None:
    for name, expect in PROGRAMS:
        code = assemble((ROOT / "programs" / name).read_text())
        failures = 0
        steps = 0
        sink = 0
        t0 = time.perf_counter()
        for _ in range(runs):
            vm = TernaryVM(list(code))
            while vm.step():
                steps += 1
            steps += 1          # count the HALT step, like tvm_run() does
            if vm.output != expect:
                failures += 1
            sink += vm.reg[0]
        t1 = time.perf_counter()
        print("BENCH vm_program name=%s runs=%d steps=%d seconds=%.6f "
              "ns_per_instruction=%.2f failures=%d"
              % (name, runs, steps, t1 - t0, (t1 - t0) * 1e9 / steps, failures))


def bench_primitives(prim_iter: int) -> None:
    acc = 0
    t0 = time.perf_counter()
    for i in range(prim_iter):
        acc = to_tryte(acc + ((i % 2001) - 1000))
    t1 = time.perf_counter()
    print("BENCH tryte_add_value n=%d seconds=%.6f ns_per_op=%.3f checksum=%d"
          % (prim_iter, t1 - t0, (t1 - t0) * 1e9 / prim_iter, acc))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--runs", type=int, default=DEFAULT_RUNS)
    ap.add_argument("--prim-iter", type=int, default=DEFAULT_PRIM_ITER)
    args = ap.parse_args()

    print("# ternary-vm Python reference benchmark (bench/bench_vm.py)")
    print("# python: %s" % sys.version.split()[0])
    print("# build: VM_RUNS=%d PRIM_ITER=%d" % (args.runs, args.prim_iter))
    t0 = time.perf_counter()
    bench_programs(args.runs)
    bench_primitives(args.prim_iter)
    print("# total bench wall time: %.3f s" % (time.perf_counter() - t0))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
