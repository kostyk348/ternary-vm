# ternary-vm

[![CI](https://github.com/kostyk348/ternary-vm/actions/workflows/ci.yml/badge.svg)](https://github.com/kostyk348/ternary-vm/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/kostyk348/ternary-vm?sort=semver&color=blue)](https://github.com/kostyk348/ternary-vm/releases)
[![License](https://img.shields.io/badge/license-Apache-2.0-green)](https://github.com/kostyk348/ternary-vm/blob/main/LICENSE)
[![Last commit](https://img.shields.io/github/last-commit/kostyk348/ternary-vm?branch=main)](https://github.com/kostyk348/ternary-vm/commits/main)

**English | [Русский](README.ru.md)**

**A balanced-ternary reference VM and research lab.**

This is a *reference virtual machine for balanced-ternary arithmetic
research* — a place to inspect how the digit set `{-1, 0, +1}` behaves: how
carry propagates, what a packed encoding costs, where a ternary machine
would differ from a binary one. It is **not** a competitor to ordinary
binary VMs, and no claim is made here that ternary arithmetic is faster or
more energy-efficient than binary. A *tryte* is 9 trits, holding values in
`[-9841, +9841]`; the machine is realised as a small interpreter, a
two-pass assembler with labels, a single-file C99 core, a carry/borrow
tracer and a property-test suite.

The full research write-up (arithmetic property table, tracer walkthrough,
trit-vs-bit cost comparison) lives in **[docs/lab.md](docs/lab.md)**.

## Quickstart

```bash
# 1. run a program on the reference Python VM
python vm/ternary_vm.py programs/fib.tasm
# output: [1, 1, 2, 3, 5, 8, 13, 21, 34, 55]

# 2. run the whole test suite (Python VM + assembler + C parity + property tests)
python -m unittest discover -s tests -t tests -v
# C property suite alone (plain gcc, no Python needed):
sh tests/run_property_tests.sh

# 3. build the C core and the carry/borrow tracer (gcc, no dependencies)
gcc -std=c99 -Wall -Wextra -Werror -pedantic -Iinclude -Itools tools/ttrace.c -o ttrace
./ttrace add 5 7                  # per-trit carry table
./ttrace sub -5 7                 # per-trit borrow table
./ttrace add 9841 1               # overflow demonstration (exit 3)
./ttrace --html add -5 7 > trace.html

# 4. ternary-vs-binary arithmetic cost on this host (wall clock only)
gcc -std=c99 -O2 -Wall -Wextra -Iinclude -Itools bench/bench_tri_vs_bin.c \
    -o bench_tri_vs_bin -lm && ./bench_tri_vs_bin

# 5. full speed comparison (Python VM vs C core [+ MPTC])
python bench/run_all.py
```

Programs in `programs/`: `add.tasm` (12), `mul.tasm` (6*8 = 48 via repeated
addition), `fib.tasm` (Fibonacci).

## Why balanced ternary (as a research object)

Balanced ternary is the most efficient integer base for its symbol count
(Information Theory, 1950): the fewest trits for a given range, a symmetric
digit set that makes negation a carry-free trit flip, a *signed* carry that
binary addition never produces. Those are exactly the properties worth
studying in a lab — and the ones this repository makes testable
(`tests/property_test.c`) and visible (`tools/ttrace.c`).

## Assembler

One instruction per line; `#` starts a comment. Operands are register
names `R0`..`R7` or balanced-ternary integer literals (may be negative).

```asm
LOAD  R0  5
LOAD  R1  7
ADD   R0  R1      # R0 = R0 + R1
OUT   R0          # print R0
HALT
```

Labels and branches:

```asm
LOAD  R0  0
LOAD  R1  1
LOAD  R2  10
LOOP:
OUT   R1
ADD   R0  R1
MOV   R4  R1
MOV   R1  R0
MOV   R0  R4
SUB   R2  R3
JNZ   LOOP R2
HALT
```

## Instruction set

| Opcode | Operands      | Semantics |
|--------|---------------|-----------|
| HALT   | —             | stop |
| LOAD   | R dst, imm    | R[dst] = imm |
| STORE  | R src, addr   | mem[addr] = R[src] |
| ADD    | R d, R s      | R[d] = R[d] + R[s] |
| SUB    | R d, R s      | R[d] = R[d] - R[s] |
| MUL    | R d, R s      | R[d] = R[d] * R[s] |
| NEG    | R d           | R[d] = -R[d] |
| MOV    | R d, R s      | R[d] = R[s] |
| JMP    | target        | pc = target |
| JZ     | target, R c   | if R[c]==0 pc = target |
| JNZ    | target, R c   | if R[c]!=0 pc = target |
| OUT    | R s           | append R[s] to output |
| NOP    | —             | no-op |

All arithmetic clamps to the 9-trit range (saturation). There is no
ordered comparison and no division — see the honesty section.

## C core for MCUs: `include/ternary_vm.h`

A **single-file C99 header**: no external dependencies, no dynamic
allocation, no globals (every function is `static inline`), so it runs the
same bytecode on an FPU-less MCU. The program image is a caller-owned
`const tvm_code_t[]` array (Flash/PROGMEM friendly).

```c
#include "ternary_vm.h"

static const tvm_code_t prog[] = {           /* assembled from programs/add.tasm */
    TVM_OP_LOAD, 0, 6566,                    /* LOAD R0 5                     */
    TVM_OP_LOAD, 1, 6568,                    /* LOAD R1 7                     */
    TVM_OP_ADD,  0, 1,                       /* ADD  R0 R1                    */
    TVM_OP_OUT,  0,                          /* OUT  R0                       */
    TVM_OP_HALT
};

int main(void) {
    tvm_t vm;
    tvm_init(&vm, prog, (uint16_t)(sizeof prog / sizeof prog[0]));
    if (tvm_run(&vm, 0) != TVM_OK) return 1;  /* 0 = default step limit     */
    tvm_tryte_t r0 = tvm_reg(&vm, 0);         /* 12                         */
    tvm_tryte_t o0 = tvm_out(&vm, 0);         /* 12                         */
    return (r0 == 12 && o0 == 12) ? 0 : 1;
}
```

Build and run the full example (`examples/c/demo.c`):

```bash
python tools/emit_bytecode.py        # programs/*.tasm -> examples/c/programs_generated.h
gcc -std=c99 -Wall -Wextra -Werror -pedantic -Iinclude -Iexamples/c \
    examples/c/demo.c -o demo && ./demo
```

Implemented: all 13 opcodes, 9-trit saturating arithmetic, the packed-tryte
codec (bit-identical to the Python reference, floor-division semantics
included), program load, single step, run with step limit, register/memory/
OUT read-out, fault reporting instead of exceptions. The Python VM stays
the reference implementation; `tests/test_c_parity.py` proves the C core
agrees with it field by field (output stream, registers, PC, step count,
full 256-cell memory image) on all repository programs plus 200 seeded
random programs.

## Known defect of the reference codec (reproduced on purpose)

`vm/ternary_vm.py` packs a tryte with bias `3**8 = 6561`, but a 9-trit
tryte needs `(3**9-1)/2 = 9841`. So `int_to_tryte_code` is a bijection only
on `[-6561, 9841]`; the other **3280 values** (`-9841..-6562`) encode to
codes that decode outside the tryte range:

```
encode(-6562) = 19682 -> decode = 13121
encode(-9000) = 17244 -> decode = 10683
encode(-9841) = 16403 -> decode =  9842
```

Visible from the ISA: `LOAD R0 -7000` loads `12683`, not `-7000`. No
program in `programs/` is affected. The C header reproduces this
bit-for-bit (a port that silently disagrees with its reference is worse
than one that agrees); `tests/test_reference_defects.py` and the C property
suite pin the behaviour, and an upstream one-constant fix (bias 9841) must
land on both sides in the same commit.

## The research layer

* **Carry/borrow tracer** — `tools/ttrace.c` (+ `tools/ttrace_add.h`, the
  digit-wise arithmetic it visualises): per-trit tables of operand trits,
  incoming/outgoing carry or borrow, result trit; ASCII or `--html`;
  overflow shown, not hidden (exit code 3). Example output and the
  derivation of the carry rules are in [docs/lab.md](docs/lab.md).
* **Property tests** — `tests/property_test.c`: round-trip, digit-wise
  carry/borrow correctness vs independent int64 references (exhaustive for
  widths 1..6, randomized + boundary for width 9), VM `ADD/SUB/MUL/NEG`
  saturation equality vs int64, packed-codec round-trip incl. the pinned
  defect, comparison/zero-uniqueness/negation involution. 337 761 089
  checks pass in ~5 s (gcc 14.2, -O2).
* **Trit-vs-bit comparison** — `bench/bench_tri_vs_bin.c`: ternary
  (value-domain / packed-domain / digit-wise) vs binary arithmetic on the
  same host, plus the trit/bit count facts and the codec cost. Key recorded
  finding: the VM's value-domain "ternary" add compiles to the same machine
  code as a saturating 16-bit binary add (1.01x); the digit-wise software
  emulation costs ~50-70x; the packed path pays ~57x for decode+encode.
  Numbers, methodology and limits: [docs/lab.md](docs/lab.md).

## Honesty section

* **No energy claims.** This repository contains no energy measurements and
  none are possible from wall-clock time on a host CPU.
  Упаковка троичных трайтов в биты не гарантирует энергетической
  эффективности; сравнение с бинарными системами по энергии — гипотеза,
  требующая измерений на железе. (Packing ternary trytes into bits does not
  guarantee energy efficiency; comparing ternary with binary systems by
  energy is a hypothesis that requires measurements on real hardware.)
* **No "ternary beats binary" claims.** The cost comparison exists to show
  what software emulation of digit-wise ternary costs on binary silicon —
  not to argue for the radix.
* **ISA limits**: no `DIV`/`MOD` (non-exact division cannot terminate with
  only `JZ`/`JNZ`), no ordered comparison, no memory→register load
  (memory is write-only), no debugger/disassembler/profiler, no MCU
  integration (interrupts/DMA/Flash helpers).
* **Codec defect** (above) is reproduced on purpose and pinned by tests on
  both runtimes.
* Benchmarks are host-only (x86-64, not MCU), load-dependent, and reported
  as recorded runs with their conditions; re-run them with
  `bench/run_all.py` before quoting.

## Tests

```bash
python -m unittest discover -s tests -t tests -v
```

* `tests/test_vm.py` — every program in `programs/`, trit-overflow/saturation
  boundaries, jump semantics (including the modulo wrap of targets), memory
  addressing, error paths, determinism of repeated runs.
* `tests/test_asm.py` — both assembler passes, label resolution, frozen opcode
  numbers (also matched against the C header), rejected inputs.
* `tests/test_reference_defects.py` — the measured behaviour of the reference
  runtime: the codec defect, the all-zero memory initialisation, the absent
  division, unchecked register fields.
* `tests/test_c_parity.py` — differential testing: the same bytecode image is
  executed by the Python VM and by the C core, and output stream, register
  file, PC, step count and the whole 256-cell memory image are compared.
  Covers all repository programs, boundary programs and 200 seeded random
  programs.
* `tests/test_c_build.py` — the header compiles standalone at `-O0..-O3/-Os`
  with `-Wall -Wextra -Werror -pedantic`, the demo exits 0, the generated
  bytecode header is current, and the header links in two translation units.
* `tests/test_property_c.py` (+ `tests/property_test.c`,
  `tests/run_property_tests.sh`) — the arithmetic property suite described
  above.

## Benchmark: Python VM vs C core vs MPTC

```bash
git clone https://github.com/kostyk348/MPTC ../MPTC      # optional, for the last block
python bench/run_all.py --mptc-dir ../MPTC               # add --repeat 5 for stable minima
```

What is measured: the *same bytecode images* from `programs/`, executed
repeatedly, reported as nanoseconds per executed VM instruction; plus the
tryte primitive operations (saturating add in the value domain, and
`encode + add + decode` in the packed domain — the closest analogue of
MPTC's packed add); plus the ternary-vs-binary arithmetic cost of
`bench_tri_vs_bin.c`.

Recorded run — AMD Ryzen 7 7840HS (16 cores), Linux x86_64, gcc 16.1.1,
`-std=c99 -O2 -Wall -Wextra`, CPython 3.14.7, `--repeat 5 --py-repeat 3`
(minima of the repetitions; the machine was busy: load average 11.0 12.4 9.9):

| measurement | Python (ns) | C core (ns) | C/Python |
|---|---|---|---|
| VM instruction, `add.tasm` | 2957.8 | 8.02 | 368x |
| VM instruction, `mul.tasm` | 3848.8 | 9.85 | 391x |
| VM instruction, `fib.tasm` | 2441.7 | 6.11 | 400x |
| tryte add, value domain | 457.6 | 2.79 | 164x |

| ternary primitive op | ns/op | ratio vs MPTC |
|---|---|---|
| MPTC `ternary_add8` (packed, 8 trits, range ±3280) | 64.57 | 1.00x |
| MPTC `ternary_to_int8` | 23.50 | 2.75x |
| MPTC `ternary_from_int8` | 11.71 | 5.51x |
| MPTC `ternary_dot8` | 14.30 | 4.51x |
| `ternary_vm.h` add, packed codes (9 trits, range ±9841) | 49.43 | 1.31x |
| `ternary_vm.h` add, value domain | 2.80 | 23.1x |

Honest reading of these numbers:

* The Python→C factor (~370–400x per instruction) is mostly CPython
  interpreter overhead, not algorithmic gain; an idle-machine run measured
  195–490x for the same code. Treat it as "the C core is two orders of
  magnitude faster per instruction", not as a precise constant.
* **MPTC has no VM/interpreter**, so there is no "same program on MPTC"
  measurement to make. Only the primitive layer is comparable, and MPTC's
  packed add (`ternary_add8` = unpack, add, clamp, repack) costs ~1.3x the
  packed add of `ternary_vm.h` while covering 8 trits instead of 9.
* The value-domain row is *not* the same operation class as MPTC's packed
  primitives: `ternary_vm.h` keeps a tryte as an `int16_t` and packs only for
  `STORE`, so it skips the pack/unpack round trip that MPTC pays per
  operation. That is the real design difference the table exposes.
* Both sides were compiled by the same gcc with the same flags on the same
  machine in the same process, so the comparison is as fair as the different
  data models allow. No energy measurement is claimed — this is wall-clock
  time on a host CPU, not on an MCU.

## Provenance

This repository grew from the lost-tech-museum audit of the kostyk348
portfolio and was then repositioned as a research lab by the follow-up
audit (kostyk348 deep-math & experimental audit, 2026-11-08):

* archive record: `ternary-vm` in digest **«Портфель kostyk348: что почти
  готово и что стоит дореализовать»** (slug `portfolio-review`, generated
  2026-10-14) — https://kostyk348.github.io/lost-tech-museum/digests/portfolio-review/
* recorded state there: *"2 исходника, 0 тестов, релизов нет; заявленный
  `ternary_vm.h` для MCU не реализован — это ключевая незакрытая часть. Нет
  сравнения с MPTC по скорости."*
* the follow-up audit's framing, which this repository now follows:
  *"Не нужно доказывать, что троичная VM лучше бинарной. Сделай её
  лабораторией: визуализация carry/borrow, assembler, trace,
  арифметические свойства, сравнение количества trit/bit и стоимости
  кодирования. Хороший результат — открытая reference VM для исследования
  balanced ternary и MPTC, а не конкурент обычным VM."*

Delivered against that framing: the C core, the test suite, the MPTC
comparison, CI, and now the research layer (carry/borrow tracer, property
tests, trit-vs-bit comparison, bilingual docs). Still open: the debugger
half of the toolchain, MCU integration, and an upstream fix for the codec
bias.

## License

Apache 2.0
