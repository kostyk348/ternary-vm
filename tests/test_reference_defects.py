"""test_reference_defects.py -- behaviour of the reference runtime that the
audit did not mention, measured and pinned so it cannot change silently.

Everything here is a *property of vm/ternary_vm.py as it stands today*. The C
core reproduces each item deliberately (parity beats prettiness for a port),
so if one of these tests starts failing because upstream was fixed, the C side
must be updated in the same commit.
"""
from __future__ import annotations

import unittest

import refvm
from refvm import (
    MEM_SIZE, OP, TRYTE_MAX, assemble, int_to_tryte_code, run_python,
    tryte_to_int,
)

BIAS = 3 ** 8            # 6561: the bias the reference uses
CORRECT_BIAS = TRYTE_MAX  # 9841 = (3**9 - 1)/2, what a 9-trit tryte needs


class CodecDefectTest(unittest.TestCase):
    """int_to_tryte_code is only exact on [-6561, 9841]."""

    def test_round_trip_fails_for_exactly_3280_values(self):
        broken = [v for v in range(-TRYTE_MAX, TRYTE_MAX + 1)
                  if tryte_to_int(int_to_tryte_code(v)) != v]
        self.assertEqual(len(broken), 3280)
        self.assertEqual(min(broken), -9841)
        self.assertEqual(max(broken), -6562)

    def test_round_trip_is_exact_on_the_upper_sub_range(self):
        for v in range(-BIAS, TRYTE_MAX + 1):
            self.assertEqual(tryte_to_int(int_to_tryte_code(v)), v, "v=%d" % v)

    def test_measured_examples(self):
        cases = {
            -6562: (19682, 13121),
            -9000: (17244, 10683),
            -9841: (16403, 9842),
        }
        for value, (code, decoded) in cases.items():
            with self.subTest(value=value):
                self.assertEqual(int_to_tryte_code(value), code)
                self.assertEqual(tryte_to_int(code), decoded)
                self.assertGreater(decoded, TRYTE_MAX)

    def test_correct_bias_would_be_a_bijection(self):
        # The defect is a single wrong constant: (3**9-1)/2 = 9841 maps the
        # whole tryte range onto the whole 9-trit code space.
        self.assertEqual(BIAS, 6561)
        self.assertEqual(CORRECT_BIAS, 9841)
        codes = {v + CORRECT_BIAS for v in range(-TRYTE_MAX, TRYTE_MAX + 1)}
        self.assertEqual(len(codes), 3 ** 9)
        self.assertEqual(min(codes), 0)
        self.assertEqual(max(codes), 3 ** 9 - 1)

    def test_out_of_range_values_clamp_to_the_same_code(self):
        self.assertEqual(int_to_tryte_code(9842), int_to_tryte_code(9841))
        self.assertEqual(int_to_tryte_code(-9842), int_to_tryte_code(-9841))


class LoadLiteralDefectTest(unittest.TestCase):
    """The codec defect is visible from the ISA: big negative literals lie."""

    def test_loading_a_literal_below_minus_6561_loads_a_wrong_value(self):
        res = run_python(assemble("LOAD R0 -7000\nOUT R0\nHALT\n"))
        loaded = res["output"][0]
        self.assertNotEqual(loaded, -7000)
        self.assertEqual(loaded, tryte_to_int(int_to_tryte_code(-7000)))
        self.assertEqual(loaded, 12683)          # the value actually loaded

    def test_loading_the_worst_literal_gives_a_value_above_the_range(self):
        res = run_python(assemble("LOAD R0 -9841\nOUT R0\nHALT\n"))
        self.assertEqual(res["output"], [9842])       # outside [-9841, 9841]

    def test_small_negative_literals_are_exact(self):
        for literal in (-1, -100, -6561):
            with self.subTest(literal=literal):
                res = run_python(assemble("LOAD R0 %d\nOUT R0\nHALT\n" % literal))
                self.assertEqual(res["output"], [literal])

    def test_no_program_in_the_repository_is_affected(self):
        for path in sorted((refvm.ROOT / "programs").glob("*.tasm")):
            for token in path.read_text().split():
                if token.lstrip("-").isdigit():
                    literal = int(token)
                    if literal < 0:
                        self.assertGreaterEqual(
                            literal, -BIAS,
                            "%s uses %d, below the exact sub-range" % (path.name, literal))


class MemoryInitDefectTest(unittest.TestCase):
    """Memory starts as all-zero codes, which decode to -6561, not to 0."""

    def test_zero_code_decodes_to_minus_6561(self):
        self.assertEqual(tryte_to_int(0), -BIAS)

    def test_fresh_memory_holds_minus_6561_everywhere(self):
        res = run_python(assemble("HALT\n"))
        self.assertEqual(res["mem"], [0] * MEM_SIZE)
        self.assertEqual([tryte_to_int(c) for c in res["mem"]], [-BIAS] * MEM_SIZE)

    def test_store_overwrites_with_a_valid_code(self):
        res = run_python(assemble("LOAD R0 1234\nSTORE R0 5\nHALT\n"))
        self.assertEqual(res["mem"][5], int_to_tryte_code(1234))
        self.assertEqual(tryte_to_int(res["mem"][5]), 1234)


class DivisionDefectTest(unittest.TestCase):
    """The ISA has no division: only exact division is expressible."""

    EXACT = """\
LOAD R0 48
LOAD R1 6
LOAD R2 0
LOOP:
JZ DONE R0
SUB R0 R1
LOAD R3 1
ADD R2 R3
JNZ LOOP R0
DONE:
OUT R2
HALT
"""

    def test_isa_has_no_division_or_modulo(self):
        self.assertFalse({"DIV", "IDIV", "MOD", "REM"} & set(OP))

    def test_exact_division_works_via_repeated_subtraction(self):
        res = run_python(assemble(self.EXACT))
        self.assertIsNone(res["error"])
        self.assertEqual(res["output"], [8])          # 48 / 6
        self.assertEqual(res["reg"][0], 0)            # remainder drained to 0

    def test_non_exact_division_cannot_terminate(self):
        # 47 / 6 leaves a remainder, no comparison instruction exists (JZ/JNZ
        # only test equality with zero), so the loop never reaches zero.
        src = self.EXACT.replace("LOAD R0 48", "LOAD R0 47")
        res = run_python(assemble(src), max_steps=2000)
        self.assertEqual(res["error"], "step_limit")

    def test_quotient_saturates_instead_of_overflowing(self):
        # 9841 / 1 = 9841 steps of a saturating counter: it clamps, it does not
        # wrap, and the dividend hits zero exactly when the quotient is full.
        src = self.EXACT.replace("LOAD R0 48", "LOAD R0 9841").replace(
            "LOAD R1 6", "LOAD R1 1")
        res = run_python(assemble(src), max_steps=60000)
        self.assertIsNone(res["error"])
        self.assertEqual(res["output"], [9841])


class UncheckedFieldsTest(unittest.TestCase):
    """The reference trusts register fields; the C core bounds-checks them."""

    def test_bad_register_field_raises_in_python(self):
        res = run_python([OP["LOAD"], 99, int_to_tryte_code(1)])
        self.assertEqual(res["error"], "IndexError")

    def test_unknown_opcode_raises_in_python(self):
        res = run_python([99])
        self.assertEqual(res["error"], "ValueError")

    def test_operand_field_is_used_as_a_raw_index(self):
        # Confirm the field really is a raw index (not a decoded tryte): 7 is
        # the last valid register, and it is written as the plain number 7.
        res = run_python([OP["LOAD"], 7, int_to_tryte_code(11), OP["OUT"], 7, OP["HALT"]])
        self.assertIsNone(res["error"])
        self.assertEqual(res["output"], [11])
        self.assertEqual(res["reg"][7], 11)


if __name__ == "__main__":
    unittest.main()
