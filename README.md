# ternary-vm

[![CI](https://github.com/kostyk348/ternary-vm/actions/workflows/ci.yml/badge.svg)](https://github.com/kostyk348/ternary-vm/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/kostyk348/ternary-vm?sort=semver&color=blue)](https://github.com/kostyk348/ternary-vm/releases)
[![License](https://img.shields.io/badge/license-Apache-2.0-green)](https://github.com/kostyk348/ternary-vm/blob/main/LICENSE)
[![Last commit](https://img.shields.io/github/last-commit/kostyk348/ternary-vm?branch=main)](https://github.com/kostyk348/ternary-vm/commits/main)

**A balanced-ternary virtual machine and assembler.**

A *tryte* is 9 trits (each in {-1, 0, +1}), holding values in
`[-9841, +9841]`. This is a clean reference runtime for the *Multi-Phase
Ternary Computing* paradigm introduced in
[MPTC](https://github.com/kostyk348/MPTC), realised here as a small
interpreter plus a two-pass assembler with labels.

## Why ternary?

Balanced ternary is the most efficient integer base for its symbol count
(Information Theory, 1950): it needs the fewest trits to represent a given
range, and the symmetric digit set makes negation and sign handling
trivial. It is the natural fit for MPTC's wave/superposition model.

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

## Run

```bash
python vm/ternary_vm.py programs/fib.tasm
# output: [1, 1, 2, 3, 5, 8, 13, 21, 34, 55]

```

Programs in `programs/`: `add.tasm` (12), `mul.tasm` (6*8 = 48 via repeated
addition), `fib.tasm` (Fibonacci).

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

All arithmetic clamps to the 9-trit range (saturation).

## C core for MCUs: `include/ternary_vm.h`

The C port is no longer a plan: `include/ternary_vm.h` is a **single-file C99
header** with no external dependencies, no dynamic allocation and no globals
(every function is `static inline`), so it can run the same bytecode on an
FPU-less MCU. The program image is a caller-owned `const tvm_code_t[]` array
(Flash/PROGMEM friendly) — nothing is copied and nothing is allocated.

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

Build and run the full example (`examples/c/demo.c`) — no dependencies
beyond `gcc`:

```bash
python tools/emit_bytecode.py        # programs/*.tasm -> examples/c/programs_generated.h
gcc -std=c99 -Wall -Wextra -Werror -pedantic -Iinclude -Iexamples/c \
    examples/c/demo.c -o demo && ./demo
```

### What is implemented

| Area | Status |
|---|---|
| All 13 opcodes (`HALT LOAD STORE ADD SUB MUL NEG MOV JMP JZ JNZ OUT NOP`) | yes, same numbers as `vm/asm.py` |
| 9-trit tryte arithmetic with saturation | yes (`tvm_clamp`) |
| packed-tryte codec (`tvm_encode` / `tvm_decode` / `tvm_trit_at`) | yes, bit-identical to Python (floor-division semantics included) |
| program load, single step, full run with step limit | yes (`tvm_init`, `tvm_step`, `tvm_run`) |
| register read-out, memory read-out, OUT stream | yes (`tvm_reg`, `tvm_mem_read`, `tvm_out`, `tvm_out_total`) |
| memory write with `addr % MEM_SIZE` wrap (as in Python) | yes (`TVM_OP_STORE`) |
| PC wrap modulo program length (floor modulo, as in Python) | yes (`tvm_wrap`) |
| fault reporting instead of exceptions | yes (`TVM_ERR_BAD_OPCODE`, `TVM_ERR_BAD_REG`, `TVM_ERR_STEP_LIMIT`) |

### What is *not* implemented (honest list)

* **No memory→register load.** The ISA has no such instruction in the Python
  reference either: memory is write-only today, so the C core mirrors that.
* **No `DIV`/`MOD`.** Division is not in the instruction set; only *exact*
  division is expressible (repeated subtraction — see
  `tests/test_reference_defects.py`). Non-exact division cannot terminate,
  because `JZ`/`JNZ` only test equality with zero.
* **No debugger, disassembler or profiler**, as the museum pitch mentions for
  the toolchain half of the work; the C core is the execution half only.
* **No MCU-specific integration**: no interrupt hooks, no DMA, no EEPROM/Flash
  programming helper, no energy measurement on real silicon. The core is pure
  computation — it builds for any C99 target and nothing else is claimed.
* **Not a replacement for the Python VM**: `vm/ternary_vm.py` stays the
  reference implementation. The C header is an addition that executes the same
  images, and `tests/test_c_parity.py` proves they agree.

### Known defect of the reference codec (reproduced on purpose)

`vm/ternary_vm.py` uses a bias of `3**8 = 6561` when packing a tryte, but a
9-trit tryte needs `(3**9-1)/2 = 9841`. As a result
`int_to_tryte_code` is a bijection only on `[-6561, 9841]`; the other **3280
values** (`-9841..-6562`) encode to codes that decode to values *outside* the
tryte range:

```
encode(-6562) = 19682 -> decode = 13121
encode(-9000) = 17244 -> decode = 10683
encode(-9841) = 16403 -> decode =  9842
```

Visible from the ISA: `LOAD R0 -7000` loads `12683`, not `-7000`. No program
in `programs/` is affected (they all stay inside the exact sub-range).

`include/ternary_vm.h` reproduces this behaviour bit-for-bit, because a port
that silently disagrees with its reference is worse than one that agrees.
`tests/test_reference_defects.py` pins the current behaviour; a one-constant
fix upstream in `vm/ternary_vm.py` (bias 9841) must be applied to the C side in
the same commit.

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
  runtime that the audit did not mention: the codec defect above, the all-zero
  memory initialisation, the absent division, unchecked register fields.
* `tests/test_c_parity.py` — differential testing: the same bytecode image is
  executed by the Python VM and by the C core, and output stream, register
  file, PC, step count and the whole 256-cell memory image are compared.
  Covers all repository programs, boundary programs and 200 seeded random
  programs.
* `tests/test_c_build.py` — the header compiles standalone at `-O0..-O3/-Os`
  with `-Wall -Wextra -Werror -pedantic`, the demo exits 0, the generated
  bytecode header is current, and the header links in two translation units.

## Benchmark: Python VM vs C core vs MPTC

```bash
git clone https://github.com/kostyk348/MPTC ../MPTC      # optional, for the last block
python bench/run_all.py --mptc-dir ../MPTC               # add --repeat 5 for stable minima
```

What is measured: the *same bytecode images* from `programs/`, executed
repeatedly, reported as nanoseconds per executed VM instruction; plus the tryte
primitive operations (saturating add in the value domain, and `encode + add +
decode` in the packed domain — the closest analogue of MPTC's packed add).

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

This work closes the gap recorded in the lost-tech-museum audit of the
kostyk348 portfolio:

* archive record: `ternary-vm` in digest **«Портфель kostyk348: что почти
  готово и что стоит дореализовать»** (slug `portfolio-review`, generated
  2026-10-14) — https://kostyk348.github.io/lost-tech-museum/digests/portfolio-review/
* recorded state: *"2 исходника, 0 тестов, релизов нет; заявленный
  `ternary_vm.h` для MCU не реализован — это ключевая незакрытая часть. Нет
  сравнения с MPTC по скорости."*
* pitch it grew from: *"Довести до пары MPTC: троичный байткод, который
  исполняется на MCU, плюс тулчейн (ассемблер + отладчик). Тогда троичная
  линия перестаёт быть исследованием и становится инструментом для
  энергоэффективных вычислений."*

Delivered against that pitch: the C core, the test suite, the MPTC comparison
and CI. Still open: the debugger half of the toolchain, MCU integration, and
an upstream fix for the codec bias.

## License

Apache 2.0
