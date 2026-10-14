"""test_vm.py -- behaviour of the Python reference VM (vm/ternary_vm.py).

Covers the DoD points that belong to the Python side: executing every program
in programs/, boundary behaviour (trit overflow/saturation, jumps, memory
wraps), determinism of repeated runs, and the documented error paths.
"""
from __future__ import annotations

import subprocess
import sys
import unittest

import refvm
from refvm import (
    MEM_SIZE, OP, TernaryVM, assemble, int_to_tryte_code, run_python, to_tryte,
    tryte_to_int,
)

PROGRAMS = {
    "add.tasm": [12],
    "mul.tasm": [48],
    "fib.tasm": [1, 1, 2, 3, 5, 8, 13, 21, 34, 55],
}


class RepositoryProgramsTest(unittest.TestCase):
    """Every program in programs/ produces its documented output."""

    def test_programs_run_and_match_expected_output(self):
        for name, expect in PROGRAMS.items():
            with self.subTest(program=name):
                res = run_python(refvm.assemble_file(name))
                self.assertIsNone(res["error"])
                self.assertEqual(res["output"], expect)

    def test_add_program_leaves_operands_in_place(self):
        res = run_python(refvm.assemble_file("add.tasm"))
        self.assertEqual(res["reg"][0], 12)   # 5 + 7
        self.assertEqual(res["reg"][1], 7)    # untouched
        self.assertEqual(res["reg"][2:], [0] * 6)

    def test_mul_program_is_repeated_addition(self):
        res = run_python(refvm.assemble_file("mul.tasm"))
        self.assertEqual(res["output"], [48])
        self.assertEqual(res["reg"][1], 0)    # counter drained

    def test_fib_program_output_is_monotonic(self):
        out = run_python(refvm.assemble_file("fib.tasm"))["output"]
        self.assertEqual(out, sorted(out))
        self.assertEqual(out[0], out[1])      # the program prints 1, 1

    def test_cli_end_to_end(self):
        proc = subprocess.run(
            [sys.executable, str(refvm.ROOT / "vm" / "ternary_vm.py"),
             str(refvm.ROOT / "programs" / "fib.tasm")],
            capture_output=True, text=True)
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertEqual(
            proc.stdout.strip(),
            "output: [1, 1, 2, 3, 5, 8, 13, 21, 34, 55]")

    def test_cli_without_arguments_prints_usage(self):
        proc = subprocess.run(
            [sys.executable, str(refvm.ROOT / "vm" / "ternary_vm.py")],
            capture_output=True, text=True)
        self.assertEqual(proc.returncode, 1)
        self.assertIn("usage", proc.stdout.lower())


class ProgramImageTest(unittest.TestCase):
    """The frozen bytecode images used by the C examples match the assembler."""

    def test_emit_bytecode_is_up_to_date(self):
        proc = subprocess.run(
            [sys.executable, str(refvm.ROOT / "tools" / "emit_bytecode.py"), "--check"],
            capture_output=True, text=True)
        self.assertEqual(proc.returncode, 0,
                         "examples/c/programs_generated.h is stale:\n%s%s"
                         % (proc.stdout, proc.stderr))

    def test_generated_lengths_match_the_assembler(self):
        text = (refvm.ROOT / "examples" / "c" / "programs_generated.h").read_text()
        for name in PROGRAMS:
            ident = name.split(".")[0]
            code = refvm.assemble_file(name)
            self.assertIn("prog_%s_len = %d;" % (ident, len(code)), text)


class TryteBoundaryTest(unittest.TestCase):
    """Boundaries from the audit: trit overflow, saturation, codec limits."""

    def test_max_is_exactly_the_9_trit_half_range(self):
        self.assertEqual(refvm.TRYTE_MAX, (3 ** 9 - 1) // 2)
        self.assertEqual(refvm.TRYTE_MAX, 9841)

    def test_saturation_on_add(self):
        src = "LOAD R0 9841\nLOAD R1 9841\nADD R0 R1\nOUT R0\nHALT\n"
        self.assertEqual(run_python(assemble(src))["output"], [9841])
        src = "LOAD R0 9841\nLOAD R1 1\nADD R0 R1\nOUT R0\nHALT\n"
        self.assertEqual(run_python(assemble(src))["output"], [9841])

    def test_saturation_on_sub(self):
        # -9841 cannot be written as a literal (see test_reference_defects.py):
        # it is built with NEG, which is exact.
        src = ("LOAD R0 9841\nNEG R0\n"
               "LOAD R1 9841\nSUB R0 R1\nOUT R0\nHALT\n")
        self.assertEqual(run_python(assemble(src))["output"], [-9841])

    def test_saturation_on_mul(self):
        cases = (
            ("LOAD R0 9841\nLOAD R1 100\n", 9841),
            ("LOAD R0 9841\nNEG R0\nLOAD R1 100\n", -9841),
            ("LOAD R0 9841\nLOAD R1 1\nNEG R1\n", -9841),
            ("LOAD R0 99\nLOAD R1 99\n", 9801),
        )
        for prologue, expect in cases:
            with self.subTest(prologue=prologue.replace("\n", " ; ")):
                src = prologue + "MUL R0 R1\nOUT R0\nHALT\n"
                self.assertEqual(run_python(assemble(src))["output"], [expect])

    def test_negation_at_the_boundary(self):
        src = "LOAD R0 9841\nNEG R0\nOUT R0\nNEG R0\nOUT R0\nHALT\n"
        self.assertEqual(run_python(assemble(src))["output"], [-9841, 9841])

    def test_register_values_are_always_inside_the_tryte_range(self):
        src = ("LOAD R0 9841\nLOAD R1 9841\nMUL R0 R1\n"
               "NEG R1\nADD R0 R1\nSUB R0 R1\nHALT\n")
        res = run_python(assemble(src))
        for i, v in enumerate(res["reg"]):
            self.assertGreaterEqual(v, -9841, "R%d" % i)
            self.assertLessEqual(v, 9841, "R%d" % i)

    def test_to_tryte_clamps_both_sides(self):
        self.assertEqual(to_tryte(10 ** 9), 9841)
        self.assertEqual(to_tryte(-10 ** 9), -9841)
        self.assertEqual(to_tryte(0), 0)

    def test_codec_exact_on_the_upper_sub_range(self):
        # [-6561, 9841] round-trips exactly (see test_reference_defects.py for
        # the 3280 values below -6561 that do not).
        for v in (-6561, -6560, -1, 0, 1, 6561, 9841):
            with self.subTest(v=v):
                self.assertEqual(tryte_to_int(int_to_tryte_code(v)), v)

    def test_operand_literal_above_the_range_is_rejected_by_the_assembler(self):
        with self.assertRaises(ValueError):
            assemble("LOAD R0 9842\nHALT\n")


class JumpTest(unittest.TestCase):
    """JMP / JZ / JNZ semantics, including the modulo wrap of the target."""

    def test_jz_taken_and_not_taken(self):
        taken = ("LOAD R0 0\nJZ SKIP R0\nLOAD R1 5\nSKIP:\nOUT R1\nHALT\n")
        self.assertEqual(run_python(assemble(taken))["output"], [0])
        not_taken = ("LOAD R0 1\nJZ SKIP R0\nLOAD R1 5\nSKIP:\nOUT R1\nHALT\n")
        self.assertEqual(run_python(assemble(not_taken))["output"], [5])

    def test_jnz_taken_and_not_taken(self):
        # taken: the guarded LOAD R1 5 is skipped, so R1 stays 0
        taken = ("LOAD R0 1\nJNZ SKIP R0\nLOAD R1 5\nSKIP:\nOUT R1\nHALT\n")
        self.assertEqual(run_python(assemble(taken))["output"], [0])
        not_taken = ("LOAD R0 0\nJNZ SKIP R0\nLOAD R1 5\nSKIP:\nOUT R1\nHALT\n")
        self.assertEqual(run_python(assemble(not_taken))["output"], [5])

    def test_backward_jump_drives_the_fibonacci_loop(self):
        res = run_python(refvm.assemble_file("fib.tasm"))
        self.assertGreater(res["steps"], len(refvm.assemble_file("fib.tasm")))

    def test_forward_jmp_skips_instructions(self):
        src = "JMP TARGET\nOUT R0\nLOAD R0 3\nTARGET:\nLOAD R1 9\nOUT R1\nHALT\n"
        self.assertEqual(run_python(assemble(src))["output"], [9])

    def test_jump_to_minus_one_wraps_onto_the_last_cell(self):
        # JMP target -1: Python's floor modulo maps it to len(prog)-1, which is
        # the HALT cell of this three-cell image.
        code = [OP["JMP"], int_to_tryte_code(-1), OP["HALT"]]
        res = run_python(code)
        self.assertIsNone(res["error"])
        self.assertEqual(res["output"], [])
        self.assertEqual(res["steps"], 2)

    def test_negative_jump_target_uses_floor_modulo(self):
        # JZ -4 in a 7-cell image: -4 % 7 == 3, i.e. the branch lands back on
        # itself, so the machine loops until the step limit. A truncated or
        # absolute-value wrap would land somewhere else and terminate.
        code = ([OP["LOAD"], 0, int_to_tryte_code(0)]
                + [OP["JZ"], int_to_tryte_code(-4), 0]
                + [OP["HALT"]])
        self.assertEqual(len(code), 7)
        res = run_python(code, max_steps=50)
        self.assertEqual(res["error"], "step_limit")

    def test_jump_to_empty_program_wraps_to_zero(self):
        res = run_python([])
        self.assertIsNone(res["error"])          # empty image -> virtual HALT
        self.assertEqual(res["output"], [])

    def test_running_past_the_end_fetches_halt(self):
        res = run_python([OP["NOP"]])
        self.assertIsNone(res["error"])
        self.assertEqual(res["steps"], 2)        # the NOP and the virtual HALT


class MemoryTest(unittest.TestCase):
    """STORE addressing and the "write only" nature of memory."""

    def test_store_writes_the_packed_code(self):
        src = "LOAD R0 7\nSTORE R0 10\nHALT\n"
        res = run_python(assemble(src))
        self.assertEqual(res["mem"][10], int_to_tryte_code(7))

    def test_store_address_is_wrapped_modulo_memory_size(self):
        res = run_python(assemble("LOAD R0 7\nSTORE R0 300\nHALT\n"))
        self.assertEqual(res["mem"][300 % MEM_SIZE], int_to_tryte_code(7))

    def test_memory_starts_as_zero_codes(self):
        res = run_python(assemble("HALT\n"))
        self.assertEqual(res["mem"], [0] * MEM_SIZE)

    def test_there_is_no_memory_read_instruction(self):
        expected = {"HALT", "LOAD", "STORE", "ADD", "SUB", "MUL", "NEG", "JMP",
                    "JZ", "JNZ", "OUT", "NOP", "MOV"}
        self.assertEqual(set(OP), expected)
        # nothing in the ISA moves memory back into a register
        self.assertFalse({"LDM", "LOADM", "POP", "LOADI"} & set(OP))


class ErrorPathTest(unittest.TestCase):
    """Documented failure modes of the reference implementation."""

    def test_infinite_loop_hits_the_step_limit(self):
        res = run_python(assemble("SPIN:\nJMP SPIN\nHALT\n"), max_steps=100)
        self.assertEqual(res["error"], "step_limit")

    def test_unknown_opcode_raises_value_error(self):
        res = run_python([99])
        self.assertEqual(res["error"], "ValueError")

    def test_out_of_range_register_index_raises_index_error(self):
        # The reference does not bounds-check register fields; the C core does
        # (TVM_ERR_BAD_REG) -- see test_reference_defects.py.
        res = run_python([OP["LOAD"], 99, int_to_tryte_code(1)])
        self.assertEqual(res["error"], "IndexError")

    def test_empty_output_for_a_program_that_never_prints(self):
        res = run_python(assemble("LOAD R0 5\nHALT\n"))
        self.assertEqual(res["output"], [])


class DeterminismTest(unittest.TestCase):
    """Repeated runs of the same image must be bit-identical."""

    def test_repeated_runs_produce_identical_state(self):
        for name in PROGRAMS:
            code = refvm.assemble_file(name)
            first = run_python(code)
            for i in range(50):
                again = run_python(code)
                self.assertEqual(again, first, "%s run %d diverged" % (name, i + 1))

    def test_two_virtual_machines_do_not_share_state(self):
        code = refvm.assemble_file("fib.tasm")
        a = TernaryVM(code)
        b = TernaryVM(code)
        while a.step():
            pass
        self.assertEqual(b.pc, 0)
        self.assertEqual(b.output, [])
        self.assertEqual(b.reg, [0] * 8)

    def test_assembler_is_deterministic(self):
        src = refvm.program_path("fib.tasm").read_text()
        self.assertEqual(assemble(src), assemble(src))

    def test_assemble_does_not_mutate_its_argument(self):
        code = refvm.assemble_file("mul.tasm")
        snapshot = list(code)
        run_python(code)
        self.assertEqual(code, snapshot)


if __name__ == "__main__":
    unittest.main()
