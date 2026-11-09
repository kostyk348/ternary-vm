#!/bin/sh
# run_property_tests.sh -- plain gcc script that builds and runs the C
# property suite (tests/property_test.c) with the same strict flags CI uses.
#
# The Python test runner (python -m unittest discover -s tests -t tests)
# builds and runs the same binary through tests/test_property_c.py; this
# script exists so the property tests can also be run without Python.
#
# Usage:  sh tests/run_property_tests.sh
# Exit:   0 = all property tests passed, 1 = build or test failure.
set -eu

cd "$(dirname "$0")/.."
OUT="${TMPDIR:-/tmp}/ternary-vm-property-test"

gcc -std=c99 -O2 -Wall -Wextra -Werror -pedantic \
    -Iinclude -Itools tests/property_test.c -o "$OUT"

"$OUT"
