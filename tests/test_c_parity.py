"""test_c_parity.py -- differential testing of include/ternary_vm.h against
the Python reference machine (vm/ternary_vm.py).

Same bytecode image in, same machine state out: output stream, register file,
program counter, step count, memory image. This is the evidence that the C
header really is a port of the reference ISA and not a lookalike.

Skipped when gcc is not available.
"""
from __future__ import annotations

import random
import unittest

import cbuild
import refvm
from refvm import (MEM_SIZE, OP, OP_REVERSE, assemble, int_to_tryte_code, run_python,
                   tryte_to_int)

NEEDS_GCC = unittest.skipUnless(cbuild.have_gcc(), "gcc is required for the C parity tests")


def compare(test: unittest.TestCase, code, max_steps=None, dump_mem=False):
    """Run the same image on both machines and assert identical state."""
    py = run_python(code, max_steps=100000 if max_steps is None else max_steps)
    c = cbuild.run_c(code, max_steps=max_steps, dump_mem=dump_mem)
    test.assertEqual(c["status"], "ok", "C core faulted: %r" % c)
    test.assertEqual(c["output"], py["output"], "OUT stream differs")
    test.assertEqual(c["regs"], py["reg"], "register file differs")
    test.assertEqual(c["pc"], py["pc"], "program counter differs")
    test.assertEqual(c["steps"], py["steps"], "step count differs")
    test.assertEqual(c["out_total"], len(py["output"]), "OUT count differs")
    if dump_mem:
        test.assertEqual(c["mem"], py["mem"], "memory image differs")
    return py, c


@NEEDS_GCC
class CodecParityTest(unittest.TestCase):
    """The packed-tryte codec agrees on every one of the 19683 values."""

    @classmethod
    def setUpClass(cls):
        cls.rows = cbuild.c_codec_table()

    def test_encode_table_matches(self):
        rows = [r for r in self.rows if r[0] == "E"]
        self.assertEqual(len(rows), 2 * refvm.TRYTE_MAX + 1)
        for _, v, code in rows:
            self.assertEqual(code, int_to_tryte_code(v), "encode(%d)" % v)

    def test_decode_table_matches(self):
        rows = [r for r in self.rows if r[0] == "D"]
        self.assertEqual(len(rows), 3 ** 9)
        for _, code, value in rows:
            self.assertEqual(value, tryte_to_int(code), "decode(%d)" % code)

    def test_trit_extraction_matches_the_python_digit_expansion(self):
        rows = [r for r in self.rows if r[0] == "T"]
        self.assertTrue(rows)
        for _, code, pos, trit in rows:
            digits = [(code // (3 ** i)) % 3 - 1 for i in range(9)]
            self.assertEqual(trit, digits[pos], "trit %d of code %d" % (pos, code))

    def test_core_constants_match_the_reference(self):
        header = (refvm.ROOT / "include" / "ternary_vm.h").read_text()
        self.assertRegex(header, r"TVM_TRYTE_MAX\s+\(\(tvm_tryte_t\)9841\)")
        self.assertRegex(header, r"#define\s+TVM_TRITS_PER_TRYTE\s+9")
        self.assertRegex(header, r"#define\s+TVM_MEM_SIZE\s+256")
        self.assertRegex(header, r"#define\s+TVM_REG_COUNT\s+8")
        self.assertEqual(MEM_SIZE, 256)


@NEEDS_GCC
class RepositoryProgramParityTest(unittest.TestCase):
    """Every program in programs/ runs identically on both machines."""

    def test_all_repository_programs(self):
        for path in sorted((refvm.ROOT / "programs").glob("*.tasm")):
            with self.subTest(program=path.name):
                compare(self, assemble(path.read_text()), dump_mem=True)

    def test_demo_expectations_match_the_python_vm(self):
        # examples/c/programs_generated.h carries the expected OUT streams; the
        # Python VM must produce exactly those.
        text = (refvm.ROOT / "examples" / "c" / "programs_generated.h").read_text()
        for path in sorted((refvm.ROOT / "programs").glob("*.tasm")):
            ident = path.stem
            block = text.split("expect_%s[] = { " % ident, 1)[1].split(" }", 1)[0]
            expect = [int(v) for v in block.split(",") if v.strip()]
            self.assertEqual(run_python(assemble(path.read_text()))["output"], expect)


@NEEDS_GCC
class BoundaryParityTest(unittest.TestCase):
    """Boundary programs (saturation, jumps, memory) agree cell for cell."""

    PROGRAMS = {
        "add-saturation": "LOAD R0 9841\nLOAD R1 9841\nADD R0 R1\nOUT R0\nHALT\n",
        "sub-saturation": "LOAD R0 9841\nNEG R0\nLOAD R1 9841\nSUB R0 R1\nOUT R0\nHALT\n",
        "mul-saturation": "LOAD R0 9841\nLOAD R1 9841\nMUL R0 R1\nOUT R0\nHALT\n",
        "mul-negative": "LOAD R0 9841\nNEG R0\nLOAD R1 100\nMUL R0 R1\nOUT R0\nHALT\n",
        "neg-twice": "LOAD R0 9841\nNEG R0\nOUT R0\nNEG R0\nOUT R0\nHALT\n",
        "literal-below-bias": "LOAD R0 -7000\nOUT R0\nHALT\n",
        "literal-worst": "LOAD R0 -9841\nOUT R0\nHALT\n",
        "mov-nop": "LOAD R3 41\nMOV R4 R3\nNOP\nOUT R4\nHALT\n",
        "jz-taken": "LOAD R0 0\nJZ SKIP R0\nLOAD R1 5\nSKIP:\nOUT R1\nHALT\n",
        "jnz-taken": "LOAD R0 1\nJNZ SKIP R0\nLOAD R1 5\nSKIP:\nOUT R1\nHALT\n",
        "store-wrap": "LOAD R0 42\nSTORE R0 300\nHALT\n",
        "store-negative-addr": "LOAD R0 7\nSTORE R0 -1\nHALT\n",
        "out-every-register": ("LOAD R0 1\nLOAD R1 2\nLOAD R2 3\nLOAD R3 4\n"
                               "LOAD R4 5\nLOAD R5 6\nLOAD R6 7\nLOAD R7 9841\n"
                               "OUT R0\nOUT R1\nOUT R2\nOUT R3\nOUT R4\nOUT R5\n"
                               "OUT R6\nOUT R7\nHALT\n"),
        "memory-untouched": "HALT\n",
    }

    def test_boundary_programs(self):
        for name, src in self.PROGRAMS.items():
            with self.subTest(program=name):
                compare(self, assemble(src), dump_mem=True)

    def test_jump_into_the_middle_of_an_instruction(self):
        # JMP to a cell that is a register operand: both machines must agree on
        # what that means (it is simply decoded as the next opcode).
        code = [OP["JMP"], int_to_tryte_code(3), OP["LOAD"], 0, int_to_tryte_code(1),
                OP["HALT"], OP["HALT"]]
        compare(self, code, dump_mem=True)

    def test_negative_jump_target_wrap_matches(self):
        code = [OP["JMP"], int_to_tryte_code(-1), OP["HALT"]]
        compare(self, code)

    def test_empty_program(self):
        compare(self, [])

    def test_running_past_the_end(self):
        compare(self, [OP["NOP"], OP["NOP"]])


@NEEDS_GCC
class StepLimitParityTest(unittest.TestCase):
    """Both machines stop a runaway program, and the C core reports it."""

    def test_spin_program_hits_the_limit_on_both_sides(self):
        code = [OP["JMP"], int_to_tryte_code(0), OP["HALT"]]
        py = run_python(code, max_steps=100)
        self.assertEqual(py["error"], "step_limit")
        c = cbuild.run_c(code, max_steps=100)
        self.assertEqual(c["rc"], -1)
        self.assertEqual(c["status"], "fault")

    def test_non_exact_division_program_hits_the_limit_on_both_sides(self):
        src = ("LOAD R0 47\nLOAD R1 6\nLOAD R2 0\nLOOP:\nJZ DONE R0\nSUB R0 R1\n"
               "LOAD R3 1\nADD R2 R3\nJNZ LOOP R0\nDONE:\nOUT R2\nHALT\n")
        code = assemble(src)
        self.assertEqual(run_python(code, max_steps=500)["error"], "step_limit")
        self.assertEqual(cbuild.run_c(code, max_steps=500)["rc"], -1)


@NEEDS_GCC
class FaultPathParityTest(unittest.TestCase):
    """Where the reference raises, the C core returns a documented code."""

    def test_bad_opcode(self):
        self.assertEqual(run_python([99])["error"], "ValueError")
        self.assertEqual(cbuild.run_c([99])["rc"], -2)     # TVM_ERR_BAD_OPCODE

    def test_bad_register(self):
        code = [OP["LOAD"], 99, int_to_tryte_code(1)]
        self.assertEqual(run_python(code)["error"], "IndexError")
        self.assertEqual(cbuild.run_c(code)["rc"], -3)     # TVM_ERR_BAD_REG

    def test_bad_register_in_a_branch(self):
        code = [OP["JZ"], int_to_tryte_code(0), 99]
        self.assertEqual(run_python(code)["error"], "IndexError")
        self.assertEqual(cbuild.run_c(code)["rc"], -3)


@NEEDS_GCC
class RandomProgramParityTest(unittest.TestCase):
    """200 pseudo-random (seeded, deterministic) images must agree exactly."""

    SEED = 0x7E3A
    COUNT = 200

    _LENGTH = {
        "HALT": 1, "NOP": 1, "NEG": 2, "OUT": 2, "LOAD": 3, "STORE": 3,
        "ADD": 3, "SUB": 3, "MUL": 3, "MOV": 3, "JZ": 3, "JNZ": 3, "JMP": 2,
    }

    @classmethod
    def _generate(cls, rng: random.Random):
        """A restricted grammar: valid registers, in-range immediates, real targets.

        Branch targets are recorded while emitting, because operand values
        collide with opcode values (register 7 == OP['JMP']).
        """
        code = []
        branches = []           # indices of cells holding a branch *target*
        for _ in range(rng.randint(2, 14)):
            op = rng.choice(["LOAD", "ADD", "SUB", "MUL", "NEG", "MOV", "OUT",
                             "NOP", "STORE", "JZ", "JNZ", "JMP"])
            code.append(OP[op])
            if op in ("LOAD", "STORE"):
                code.append(rng.randrange(8))
                code.append(int_to_tryte_code(rng.randint(-6561, 9841)))
            elif op in ("ADD", "SUB", "MUL", "MOV"):
                code.append(rng.randrange(8))
                code.append(rng.randrange(8))
            elif op == "NEG":
                code.append(rng.randrange(8))
            elif op == "OUT":
                code.append(rng.randrange(8))
            elif op in ("JZ", "JNZ"):
                branches.append(len(code))
                code.append(int_to_tryte_code(0))   # patched below
                code.append(rng.randrange(8))
            elif op == "JMP":
                branches.append(len(code))
                code.append(int_to_tryte_code(0))   # patched below
        code.append(OP["HALT"])

        # Instruction boundaries of the final image (targets must land on one).
        boundaries = []
        i = 0
        while i < len(code) - 1:                      # never target the HALT
            op = code[i]
            boundaries.append(i)
            i += cls._LENGTH[OP_REVERSE[op]]
        for cell in branches:
            code[cell] = int_to_tryte_code(rng.choice(boundaries))
        return code

    def test_random_programs_agree(self):
        rng = random.Random(self.SEED)
        halted = limited = 0
        for n in range(self.COUNT):
            code = self._generate(rng)
            py = run_python(code, max_steps=400)
            c = cbuild.run_c(code, max_steps=400, dump_mem=True)
            with self.subTest(program=n, code=code):
                if py["error"] == "step_limit":
                    limited += 1
                    self.assertEqual(c["rc"], -1,
                                     "C core did not report the step limit")
                    continue
                self.assertIsNone(py["error"], "unexpected Python error %r" % py["error"])
                halted += 1
                self.assertEqual(c["status"], "ok", "C core faulted: %r" % c)
                self.assertEqual(c["output"], py["output"])
                self.assertEqual(c["regs"], py["reg"])
                self.assertEqual(c["pc"], py["pc"])
                self.assertEqual(c["steps"], py["steps"])
                self.assertEqual(c["mem"], py["mem"])
        self.assertGreater(halted, 0)
        self.assertGreater(limited, 0)


if __name__ == "__main__":
    unittest.main()
