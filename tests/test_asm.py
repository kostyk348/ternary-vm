"""test_asm.py -- the two-pass assembler (vm/asm.py).

The assembler defines the bytecode format shared with the C core, so its
opcode numbering and operand encoding are pinned here.
"""
from __future__ import annotations

import subprocess
import sys
import unittest

import refvm
from refvm import (OP, ROOT, assemble, int_to_tryte_code, program_path, run_python,
                   tryte_to_int)


class OpcodeTableTest(unittest.TestCase):
    """Opcode numbers are the on-the-wire format: they must not drift."""

    FROZEN = {"HALT": 0, "LOAD": 1, "STORE": 2, "ADD": 3, "SUB": 4, "MUL": 5,
              "NEG": 6, "JMP": 7, "JZ": 8, "JNZ": 9, "OUT": 10, "NOP": 11,
              "MOV": 12}

    def test_opcode_numbers_are_frozen(self):
        self.assertEqual(OP, self.FROZEN)

    def test_core_header_uses_the_same_numbers(self):
        header = (ROOT / "include" / "ternary_vm.h").read_text()
        for name, value in self.FROZEN.items():
            with self.subTest(opcode=name):
                self.assertRegex(
                    header, r"#define\s+TVM_OP_%s\s+%d\b" % (name, value))


class AssemblyTest(unittest.TestCase):
    def test_operands_are_encoded_as_packed_trytes(self):
        self.assertEqual(assemble("LOAD R0 5\nHALT\n"),
                         [OP["LOAD"], 0, int_to_tryte_code(5), OP["HALT"]])

    def test_register_operands_are_raw_indices(self):
        self.assertEqual(assemble("ADD R3 R4\nHALT\n"),
                         [OP["ADD"], 3, 4, OP["HALT"]])

    def test_negative_literals_round_trip(self):
        code = assemble("LOAD R1 -7\nOUT R1\nHALT\n")
        self.assertEqual(tryte_to_int(code[2]), -7)

    def test_comments_and_blank_lines_are_ignored(self):
        with_comments = ("# leading comment\n\nLOAD R0 5   # trailing\nHALT\n")
        self.assertEqual(assemble(with_comments), assemble("LOAD R0 5\nHALT\n"))

    def test_labels_are_resolved_forward_and_backward(self):
        # forward reference: FWD is defined after the JMP
        fwd = assemble("JMP FWD\nNOP\nFWD:\nHALT\n")
        self.assertEqual(tryte_to_int(fwd[1]), 3)
        # backward reference: the loop label sits before the branch
        back = assemble("LOAD R1 1\nLOAD R0 3\nLOOP:\nSUB R0 R1\nJNZ LOOP R0\nHALT\n")
        self.assertEqual(tryte_to_int(back[10]), 6)   # address of the SUB
        res = run_python(back)
        self.assertEqual(res["reg"][0], 0)            # loop ran to completion

    def test_mnemonics_and_registers_are_case_insensitive(self):
        self.assertEqual(assemble("load r0 5\nhalt\n"),
                         [OP["LOAD"], 0, int_to_tryte_code(5), OP["HALT"]])

    def test_label_lookup_is_asymmetric_about_case(self):
        # Label *definitions* are upper-cased in the first pass, the *reference*
        # is not, so a lower-case definition with an upper-case reference works
        # and the reverse dies with a confusing "invalid literal for int()".
        loop_case = assemble("loop:\nJMP LOOP\nHALT\n")
        self.assertEqual(tryte_to_int(loop_case[1]), 0)   # back to the label
        with self.assertRaises(ValueError):
            assemble("LOOP:\nJMP loop\nHALT\n")

    def test_missing_label_raises_instead_of_assembling(self):
        with self.assertRaises(ValueError):
            assemble("JMP NOWHERE\nHALT\n")

    def test_unknown_mnemonic_is_rejected(self):
        with self.assertRaises(ValueError) as ctx:
            assemble("FROB R0 1\nHALT\n")
        self.assertIn("unknown mnemonic", str(ctx.exception))

    def test_out_of_range_literal_is_rejected(self):
        for literal in (9842, -9842, 100000):
            with self.subTest(literal=literal):
                with self.assertRaises(ValueError):
                    assemble("LOAD R0 %d\nHALT\n" % literal)

    def test_max_literal_is_accepted(self):
        self.assertEqual(tryte_to_int(assemble("LOAD R0 9841\nHALT\n")[2]), 9841)

    def test_instruction_length_is_derived_from_the_operand_count(self):
        code = assemble("OUT R0\nHALT\n")
        self.assertEqual(len(code), 3)
        code = assemble("MOV R1 R2\nHALT\n")
        self.assertEqual(len(code), 4)

    def test_programs_in_the_repository_all_assemble(self):
        for path in sorted((ROOT / "programs").glob("*.tasm")):
            with self.subTest(program=path.name):
                code = assemble(path.read_text())
                self.assertTrue(code)
                self.assertIn(OP["HALT"], code)

    def test_assembler_cli_prints_the_bytecode(self):
        src = program_path("add.tasm").read_text()
        proc = subprocess.run(
            [sys.executable, str(ROOT / "vm" / "asm.py")],
            input=src, capture_output=True, text=True)
        self.assertEqual(proc.returncode, 0, proc.stderr)
        printed = [int(v) for v in proc.stdout.split()]
        self.assertEqual(printed, assemble(src))


if __name__ == "__main__":
    unittest.main()
