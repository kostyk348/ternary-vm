"""refvm.py -- helpers to drive the Python reference VM from the tests.

Keeps the "how to run a program image" logic in one place so the test modules
can compare the Python machine against the C core field by field.
"""
from __future__ import annotations

import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
if str(ROOT / "vm") not in sys.path:
    sys.path.insert(0, str(ROOT / "vm"))

from asm import assemble  # noqa: E402
from ternary_vm import (  # noqa: E402
    MEM_SIZE, OP, TRITS_PER_TRYTE, TRYTE_MAX, TernaryVM, int_to_tryte_code,
    to_tryte, tryte_to_int,
)

OP_REVERSE = {value: name for name, value in OP.items()}

__all__ = [
    "ROOT", "MEM_SIZE", "OP", "OP_REVERSE", "TRITS_PER_TRYTE", "TRYTE_MAX",
    "TernaryVM", "assemble", "int_to_tryte_code", "to_tryte", "tryte_to_int",
    "run_python", "program_path", "assemble_file",
]


def program_path(name: str) -> pathlib.Path:
    return ROOT / "programs" / name


def assemble_file(name: str) -> list:
    return assemble(program_path(name).read_text())


def run_python(bytecode, max_steps: int = 100000) -> dict:
    """Run a program image on the Python reference machine.

    Returns output / reg / mem / pc / steps / error. `steps` counts every
    step() call including the final HALT, which is what the C core's tvm_run()
    counts too. `error` is None, 'step_limit', 'ValueError' or 'IndexError'.
    """
    vm = TernaryVM(list(bytecode))
    steps = 0
    error = None
    try:
        while vm.step():
            steps += 1
            if steps > max_steps:
                raise RuntimeError("step limit exceeded (possible infinite loop)")
        steps += 1  # the HALT instruction itself
    except RuntimeError as exc:
        if "step limit" not in str(exc):
            raise
        error = "step_limit"
    except ValueError:
        error = "ValueError"
    except IndexError:
        error = "IndexError"
    return {
        "output": list(vm.output),
        "reg": list(vm.reg),
        "mem": list(vm.mem),
        "pc": vm.pc,
        "steps": steps,
        "error": error,
    }
