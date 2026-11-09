"""test_property_c.py -- unittest wrapper for the C property suite.

Builds tests/property_test.c with the same strict flags as CI and runs it.
The binary itself checks the arithmetic properties (round-trip, digit-wise
carry/borrow vs an independent int64 reference, VM arithmetic invariance,
packed-codec round-trip, comparison/equality edge cases) and exits non-zero
on any violation; the wrapper additionally asserts the "ALL PROPERTY TESTS
PASSED" summary line so a truncated run cannot look like a pass.

Run directly:  sh tests/run_property_tests.sh
Run via unittest:  python -m unittest discover -s tests -t tests -v

Skipped when gcc is not available.
"""
from __future__ import annotations

import unittest

import cbuild

NEEDS_GCC = unittest.skipUnless(cbuild.have_gcc(), "gcc is required for the property tests")


@NEEDS_GCC
class PropertyTest(unittest.TestCase):
    def test_property_suite_passes(self):
        # STRICT_FLAGS is shared: never mutate it, build a fresh flag list
        # that also points at tools/ (the digit-layer header lives there).
        flags = cbuild.STRICT_FLAGS + ["-O2", "-I", str(cbuild.ROOT / "tools")]
        proc = cbuild.compile_c([cbuild.ROOT / "tests" / "property_test.c"],
                                "property_test", flags=flags)
        self.assertEqual(proc.returncode, 0,
                         "property_test build failed:\n%s\n%s"
                         % (proc.stdout, proc.stderr))
        binary = cbuild.build_dir() / "property_test"
        import subprocess
        run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=300)
        self.assertEqual(run.returncode, 0,
                         "property tests failed:\n%s\n%s" % (run.stdout, run.stderr))
        self.assertIn("ALL PROPERTY TESTS PASSED", run.stdout)
        # every PROPERTY line must report ok -- a suite that silently stopped
        # early would otherwise still print the summary
        for line in run.stdout.splitlines():
            if line.startswith("PROPERTY "):
                self.assertTrue(line.endswith(" ok"),
                                "non-ok property line: %s" % line)


if __name__ == "__main__":
    unittest.main()
