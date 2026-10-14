"""cbuild.py -- build/run helpers shared by the tests that exercise the C core.

Not a test module itself (no test_* functions), so `unittest discover` ignores
it, but it is importable from the test modules.
"""
from __future__ import annotations

import functools
import pathlib
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
INCLUDE = ROOT / "include"
EXAMPLES = ROOT / "examples" / "c"

STRICT_FLAGS = ["-std=c99", "-Wall", "-Wextra", "-Werror", "-pedantic"]


def have_gcc() -> bool:
    return shutil.which("gcc") is not None


def build_dir() -> pathlib.Path:
    d = pathlib.Path(tempfile.gettempdir()) / "ternary-vm-tests"
    d.mkdir(parents=True, exist_ok=True)
    return d


def compile_c(sources, out_name: str, flags=None, extra=None) -> subprocess.CompletedProcess:
    """Compile with gcc and return the CompletedProcess (no check)."""
    cmd = ["gcc"] + list(flags or STRICT_FLAGS)
    cmd += ["-I", str(INCLUDE), "-I", str(EXAMPLES)]
    cmd += [str(s) for s in sources]
    cmd += ["-o", str(build_dir() / out_name)]
    if extra:
        cmd += list(extra)
    return subprocess.run(cmd, capture_output=True, text=True)


@functools.lru_cache(maxsize=None)
def c_runner() -> pathlib.Path:
    """The differential-test runner from tools/c_runner.c, built once."""
    proc = compile_c([ROOT / "tools" / "c_runner.c"], "c_runner")
    if proc.returncode != 0:
        raise RuntimeError("c_runner build failed:\n%s\n%s" % (proc.stdout, proc.stderr))
    return build_dir() / "c_runner"


@functools.lru_cache(maxsize=None)
def c_demo() -> pathlib.Path:
    proc = compile_c([ROOT / "examples" / "c" / "demo.c"], "demo")
    if proc.returncode != 0:
        raise RuntimeError("demo build failed:\n%s\n%s" % (proc.stdout, proc.stderr))
    return build_dir() / "demo"


def run_c(bytecode, max_steps=None, dump_mem=False) -> dict:
    """Run a program image on the C core and parse the runner's report."""
    args = [str(c_runner())]
    if dump_mem:
        args.append("--mem")
    if max_steps is not None:
        args.append(str(max_steps))
    text = " ".join(str(v) for v in bytecode)
    proc = subprocess.run(args, input=text, capture_output=True, text=True)
    if proc.returncode != 0:
        raise RuntimeError("c_runner failed: %s" % proc.stderr)
    return parse_report(proc.stdout)


def parse_report(text: str) -> dict:
    out: dict = {}
    for line in text.splitlines():
        parts = line.split()
        if not parts:
            continue
        key = parts[0]
        if key in ("STATUS",):
            out["status"] = parts[1]
        elif key in ("FAULT", "STEPS", "PC", "OUT_TOTAL", "RC"):
            out[key.lower()] = int(parts[1])
        elif key in ("OUTPUT", "REGS", "MEM"):
            out[key.lower()] = [int(v) for v in parts[1:]]
    return out


def c_codec_table() -> list:
    """The packed-tryte codec table produced by the C core."""
    proc = subprocess.run([str(c_runner()), "--table"], capture_output=True, text=True)
    if proc.returncode != 0:
        raise RuntimeError("c_runner --table failed: %s" % proc.stderr)
    rows = []
    for line in proc.stdout.splitlines():
        parts = line.split()
        rows.append((parts[0], int(parts[1]), int(parts[2])))
        if parts[0] == "T":
            rows[-1] = ("T", int(parts[1]), int(parts[2]), int(parts[3]))
    return rows


def pythonpath_setup() -> pathlib.Path:
    """Make vm/ importable (the reference runtime lives there)."""
    vm = ROOT / "vm"
    if str(vm) not in sys.path:
        sys.path.insert(0, str(vm))
    return vm
