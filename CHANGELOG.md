# Changelog

All notable changes to this project are documented in this file.
The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Added

- **Research-lab layer** (the repo is now positioned as a reference VM for
  balanced-ternary arithmetic research, not a competitor to binary VMs):
  - `tools/ttrace.c` + `tools/ttrace_add.h`: carry/borrow visualization
    tracer — per-trit tables of operand trits, incoming/outgoing carry
    (add) or signed borrow (sub), result trit; ASCII by default, `--html`;
    `mul` shown as a shift-and-add ladder; overflow reported with exit
    code 3. Digit-wise arithmetic (`add/sub/mul/neg/compare`) lives in the
    single-header `tools/ttrace_add.h` and is exhaustively verified.
  - `tests/property_test.c` (+ `tests/run_property_tests.sh`,
    `tests/test_property_c.py`): C property suite — balanced-ternary
    round-trip (exhaustive for the 9-trit tryte and widths 1..6, sampled to
    width 18), digit-wise carry/borrow correctness vs independent int64
    references, VM `ADD/SUB/MUL/NEG` saturation equality vs int64 over
    randomized + boundary inputs, packed-codec round-trip with the pinned
    bias defect, comparison/zero-uniqueness/negation-involution edge cases.
    337 761 089 checks pass in ~5 s (gcc 14.2, `-O2`).
  - `bench/bench_tri_vs_bin.c`: ternary-vs-binary arithmetic cost on the
    host (value-domain / packed-domain / digit-wise vs 16/32/64-bit binary),
    trit/bit count facts and codec cost; integrated into
    `bench/run_all.py`. Wall-clock only — no energy conclusions.
  - `docs/lab.md`: the research write-up — arithmetic property table with
    code references, opcode semantics, carry/borrow rules, tracer examples,
    recorded tri-vs-bit numbers, and the energy disclaimer.
- Bilingual README: `README.ru.md` (full Russian mirror of `README.md`)
  with a language switcher at the top of both files.
- `CHANGELOG.md` (this file).

### Changed

- `README.md` repositioned as a research lab (reference VM framing,
  quickstart, honesty section); the C core, codec-defect and MPTC sections
  are preserved.
- CI (`.github/workflows/ci.yml`): the `c-build` job now also builds and
  runs the property suite (`tests/run_property_tests.sh`), the tracer
  smoke test, and the tri-vs-binary benchmark; the python job picks up the
  new `test_property_c.py` wrapper automatically.
- `.gitignore`: new build artifacts (`/ttrace`, `/bench_tri_vs_bin`,
  `/property_test`).
