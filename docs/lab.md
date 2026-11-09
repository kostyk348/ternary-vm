# ternary-vm as a research lab

This repository is a **reference virtual machine for balanced-ternary
arithmetic research**. Its purpose is to make the arithmetic of balanced
ternary *inspectable*: what the digit set implies, how carry propagates, what
a packed encoding costs, and where a ternary machine would differ from a
binary one. It is explicitly **not** a competitor to ordinary binary VMs, and
nothing here claims that ternary is faster or more energy-efficient than
binary. The audit that commissioned this document framed it the same way: do
not try to prove that a ternary VM beats a binary one — build a laboratory:
carry/borrow visualisation, assembler, tracing, arithmetic properties, and a
trit-vs-bit cost comparison.

Everything below is derived from the code in this repository
(`vm/ternary_vm.py`, `vm/asm.py`, `include/ternary_vm.h`,
`tools/ttrace_add.h`) and from measurements taken with `bench/bench_tri_vs_bin.c`
and `tests/property_test.c`. Where a statement is a property of *this*
implementation (quirks included), the code location is named.

---

## 1. What the VM executes

The machine word is a **tryte = 9 trits**, each trit in `{-1, 0, +1}`,
holding a value in `[-9841, +9841]` (`TRYTE_MAX = (3**9 - 1) // 2`,
`vm/ternary_vm.py`; `TVM_TRYTE_MAX/MIN`, `include/ternary_vm.h`). The
bytecode is a flat list of packed trytes produced by the two-pass assembler
`vm/asm.py` (labels, register names `R0`..`R7`, decimal literals, `#`
comments). Opcode numbers are frozen — the C core executes the same images
bit-for-bit (`tests/test_c_parity.py` proves it).

| # | Opcode | Operands | Semantics (as coded) |
|---|--------|----------|----------------------|
| 0 | HALT   | —        | stop the machine |
| 1 | LOAD   | R dst, imm | `R[dst] = tryte_to_int(imm)` — decode literal into value domain |
| 2 | STORE  | R src, addr | `mem[addr % 256] = int_to_tryte_code(R[src])` — packed code stored |
| 3 | ADD    | R d, R s | `R[d] = clamp(R[d] + R[s])` |
| 4 | SUB    | R d, R s | `R[d] = clamp(R[d] - R[s])` |
| 5 | MUL    | R d, R s | `R[d] = clamp(R[d] * R[s])` |
| 6 | NEG    | R d      | `R[d] = clamp(-R[d])` |
| 7 | JMP    | target   | `pc = tryte_to_int(target) % len(prog)` (floor modulo wrap) |
| 8 | JZ     | target, R c | if `R[c] == 0` jump |
| 9 | JNZ    | target, R c | if `R[c] != 0` jump |
| 10 | OUT   | R s      | append `R[s]` to the output stream |
| 11 | NOP   | —        | nothing |
| 12 | MOV   | R d, R s | `R[d] = R[s]` |

Notes that matter for research:

* **Arithmetic is saturating, not wrapping**: every result is clamped into
  `[-9841, +9841]` (`to_tryte` / `tvm_clamp`). Overflow is silent
  saturation — a deliberate, documented choice, and one place where a
  ternary hardware design would have to make the same decision.
* **There is no ordered comparison and no division.** `JZ`/`JNZ` test only
  equality with zero. Non-exact division cannot even be programmed with
  this ISA (repeated subtraction never terminates for a non-exact dividend
  because you cannot test "is the remainder smaller than the divisor",
  only "is it zero") — see `tests/test_reference_defects.py`.
* Memory is 256 trytes, stored **packed** (as codes, not values), and is
  initialised to code `0` — which *decodes* to `-6561`, not to `0`. This
  quirk is pinned by tests on both runtimes.
* The digit-wise layer (`tools/ttrace_add.h`) is *not* executed by the VM.
  The VM runs arithmetic in the value domain; the digit layer exists for the
  tracer and the property tests (Sections 3–5).

## 2. Arithmetic properties of balanced ternary as implemented here

| # | Property | As implemented in THIS codebase | Where |
|---|----------|--------------------------------|-------|
| 1 | Digit set | `{-1, 0, +1}` per trit; 9 trits per tryte | `TRITS_PER_TRYTE = 9`, `vm/ternary_vm.py`; `TVM_TRITS_PER_TRYTE`, `include/ternary_vm.h` |
| 2 | Value range | `[-9841, +9841] = [-(3**9-1)/2, +(3**9-1)/2]`, symmetric — negation needs no extra bit/trit | `TRYTE_MAX`, `TVM_TRYTE_MAX` |
| 3 | In-register representation | a tryte *value* is an `int16_t`; arithmetic happens in this domain | `tvm_tryte_t`, `TVM_OP_ADD` |
| 4 | Packed representation | code = `sum(digit_i * 3**i)` over `x = v + 6561`, digits in `{0,1,2}`, floor-division semantics, code in `[0, 19682]`; least-significant trit is `3**0` | `int_to_tryte_code` / `tryte_to_int`; `tvm_encode` / `tvm_decode` |
| 5 | Known codec defect (reproduced on purpose) | the bias is `3**8 = 6561`, but 9 trits need `(3**9-1)/2 = 9841`; the codec is a bijection only on `[-6561, 9841]`; for `v in [-9841, -6562]` (3280 values) `decode(encode(v)) = v + 19683`. Measured examples: `encode(-6562)=19682 -> decode 13121`, `encode(-9000)=17244 -> 10683`, `encode(-9841)=16403 -> 9842` | header comment of `include/ternary_vm.h`; pinned by `tests/test_reference_defects.py` and Suite 5 of `tests/property_test.c` |
| 6 | Carry rule (addition, digit layer) | per position: `s = a_i + b_i + c_in ∈ [-3, 3]`; `c_out = floor((s+1)/3) ∈ {-1,0,+1}`; `r_i = s - 3*c_out` — **the carry itself is a signed trit**, so a partial sum of `-2` emits a *negative* carry | `bt_add_trace`, `tools/ttrace_add.h` |
| 7 | Borrow rule (subtraction, digit layer) | `s = a_i - b_i - b_in ∈ [-3, 2]`; `b_out = floor((1-s)/3) ∈ {-1,0,+1}`; `r_i = s + 3*b_out`. A borrow of `-1` is a carry — binary schoolbook subtraction never produces this | `bt_sub_trace` |
| 8 | VM arithmetic is *not* digit-wise | `ADD/SUB/MUL` are one integer op + clamp in the value domain; digit-wise propagation exists only in the research layer and the tracer | `TVM_OP_ADD..MUL` vs `tools/ttrace_add.h` |
| 9 | Negation | value domain: `clamp(-v)`; digit layer: flip every trit, **no carry can occur** (impossible with an asymmetric digit set); NEG is an involution — verified for all 19683 values | `TVM_OP_NEG`, `bt_neg_trace`, Suite 6 of `tests/property_test.c` |
| 10 | Zero uniqueness | exactly one representation: all-zero trits; among all 19683 packed codes exactly one (`encode(0)`) decodes to 0 — no `+0/-0` duality exists | Suite 6 of `tests/property_test.c` |
| 11 | Comparison | no ordered-compare opcode; the research layer compares MSB-first over digits and agrees with integer comparison on every tested pair | `bt_compare`, Suite 6 of `tests/property_test.c` |
| 12 | Multiplication width | `9841 * 9841 = 96845281` fits in `int32_t`, so MUL is a single 32-bit product + clamp; the digit-wise multiplier is shift-and-add over non-zero trits of B, traced step by step | `TVM_OP_MUL`, `bt_mul_trace` |
| 13 | Saturation semantics | overflow in the value domain clamps silently (`to_tryte`); in the digit layer overflow is *reported* as a non-zero final carry (`t.overflow`), i.e. the tracer shows what the VM hides | `tvm_clamp`, `bt_trace_t.overflow` |

## 3. Carry/borrow visualisation: `tools/ttrace.c`

The tracer prints the whole digit-wise computation of one operation:
operand trits, every incoming/outgoing carry (add) or borrow (sub), every
result trit. Plain ASCII by default, standalone HTML with `--html`.

```bash
gcc -std=c99 -O2 -Wall -Wextra -Werror -pedantic -Iinclude -Itools \
    tools/ttrace.c -o ttrace
./ttrace add 5 7          # per-trit table, width 9
./ttrace sub -5 7         # borrow view
./ttrace add 9841 1       # overflow demonstration (exit code 3)
./ttrace --width 6 add 40 -40
./ttrace --html add -5 7 > trace.html
```

Real output of `./ttrace add 5 7` (recorded 2026-11-09, this repository):

```
ttrace: add 5 + 7 (width 9)
trits: + = +1, 0 = 0, - = -1 (position i has weight 3^i; left = most significant)
row \ pos   8 7 6 5 4 3 2 1 0
A           0 0 0 0 0 0 + - -
B           0 0 0 0 0 0 + - +
carry in    0 0 0 0 0 0 - 0 0
result      0 0 0 0 0 0 + + 0
carry out   0 0 0 0 0 0 0 - 0
check: 5 + 7 = 12 (exact); digit row + carry *3^9 = 12 -- matches
VM semantics (include/ternary_vm.h): tvm_clamp(12) = 12 -- fits in one tryte [-9841, +9841]
```

Read row `carry out` at position 1: `5 + 7` emits a carry of **`-1`** out of
the `3^1` position (`(-1) + (-1) = -2` → result trit `+1`, carry `-1`). This
is the property of balanced ternary that a binary tracer cannot show: the
carry is itself a signed trit, and it is consumed two positions later. The
final line always cross-checks the traced digits against exact integer
arithmetic, and against what the VM would store (`tvm_clamp`).

Overflow is shown, not hidden:

```
$ ./ttrace add 9841 1
row \ pos   8 7 6 5 4 3 2 1 0
A           + + + + + + + + +
B           0 0 0 0 0 0 0 0 +
carry in    + + + + + + + + 0
result      - - - - - - - - -
carry out   + + + + + + + + +
check: 9841 + 1 = 9842 (exact); digit row + carry *3^9 = 9842 -- matches
!! OVERFLOW: the exact result 9842 does not fit in 9 trits ([-9841, +9841]); the VM would saturate instead: tvm_clamp -> 9841
```

(`exit code 3`; the tracer prints the trace anyway — overflow is shown, not hidden.)

`sub` uses the signed-borrow view (Table row #7), `mul` prints the
shift-and-add ladder (one row per non-zero trit of B, each row being one
addition whose own carry table can be traced at width 18), `neg` shows the
carry-free trit flip.

## 4. Trit vs bit: counts and costs

### Counts (computed)

* A 9-trit tryte spans 19683 values ≈ `log2(19683)` = **14.265 bits** of
  information; the smallest binary integer type that holds it is `int16`
  (65536 codes, of which the tryte uses 30.0%).
* Example: values up to 10^6 fit in **14 trits** (`3^14 = 4782969`) versus
  **21 bits** (`2^20 < 2·10^6+1 ≤ 2^21`) — the balanced-ternary density
  advantage of the radix itself, before any hardware question.

### Costs measured on this host

`bench/bench_tri_vs_bin.c` measures ns per operation, wall clock,
`CLOCK_MONOTONIC`, gcc 14.2.0 `-O2`, x86-64, `TRI_ITER = 2 000 000` per
loop; the table shows the minimum of 5 runs (recorded 2026-11-09 on an
Intel Xeon, 2 cores, Linux, otherwise busy — single-run values fluctuate by
up to ~2x on a loaded host, so treat sub-2x ratios as noise).

| operation | ns/op |
|---|---|
| ternary add, value domain (`tvm_clamp(acc + x)`) | 1.24 |
| binary add, 16-bit storage, saturating (identical code shape) | 1.22 |
| binary add, 16-bit storage, wrapping | 1.97 |
| binary add, 32-bit wrapping | 1.96 |
| binary add, 64-bit wrapping | 2.01 |
| ternary add, packed domain (decode + add + clamp + encode) | 70.7 |
| ternary add, digit-wise, no trace record | 81.7 |
| ternary add, digit-wise with full trace record | 62.1 |
| codec `tvm_encode` alone | 13.7 |
| codec `tvm_decode` alone | 16.2 |
| ternary mul, value domain / binary mul 16-bit saturating | 2.53 / 2.52 |

What these numbers actually say (and the tool prints this itself):

1. **The VM's "ternary" value-domain arithmetic compiles to the same machine
   code as saturating 16-bit binary arithmetic — measured ratio 1.01x.**
   This is the honest baseline: as long as ternary values live in binary
   registers, the radix of the representation does not show up in the
   value-domain instruction stream at all.
2. The **digit-wise** adder (what a real ternary ALU would do in hardware,
   and what the tracer displays) costs ~50–70x a binary add *in software on
   binary silicon* — that multiplier measures the software emulation, not
   the hypothetical hardware.
3. The **packed-code** path pays a ~57x tax per operation for
   `decode + encode` (13.7 + 16.2 ns of codec alone). Packing trits into
   bits buys storage density, not compute speed.
4. **Host x86-64, не MCU; энергетические выводы из этих чисел делать
   нельзя.** Упаковка троичных трайтов в биты не гарантирует энергетической
   эффективности; сравнение с бинарными системами по энергии — гипотеза,
   требующая измерений на железе. ("Host x86-64, not an MCU; no energy
   conclusions can be drawn from these numbers. Packing ternary trytes into
   bits does not guarantee energy efficiency; comparing ternary with binary
   systems by energy is a hypothesis that requires measurements on real
   hardware.")

The count half is a fact of the radix; the cost half above is a fact of this
compiler and this host; the energy question is open and needs silicon.

## 5. Property tests: the arithmetic contract, checked

`tests/property_test.c` (run via `sh tests/run_property_tests.sh`, or via
`python -m unittest discover -s tests -t tests` which builds and executes
the same binary) verifies the contract against **independent int64
reference implementations** written in the test itself:

* round-trip `value -> digits -> value` for every tryte value (19683),
  exhaustive for widths 1..6, sampled to width 18; digits always in
  `{-1, 0, +1}`; zero unique;
* digit-wise carry correctness vs int64: exhaustive widths 1..6
  (597 870 pairs at width 6), width 9 with all boundary pairs
  (`±9841, ±6562, ±6561, ...`) plus 2 000 000 randomized pairs; every carry
  and borrow asserted to lie in `{-1, 0, +1}` and the carry chain to connect;
* `ADD/SUB/MUL/NEG` of the VM header vs the int64 reference over 1 000 000
  randomized pairs + boundary cross products (saturation equality);
* packed codec round-trip: exact on `[-6561, 9841]` (16 403 values), defect
  shape `v + 19683` pinned on `[-9841, -6562]` (3 280 values), plus the
  measured examples above;
* comparison/equality: digit comparator vs integer comparison (500 000
  randomized pairs + boundaries), single zero code among 19683, NEG
  involution.

Tail of a real run (2026-11-09, gcc 14.2.0, `-O2`):

```
PROPERTY carry_vs_int64 width=9 boundary=289 random=2000000 pairs=2000289 ok
PROPERTY mul_vs_int64 width=9 boundary=225 random=500000 pairs=500225 ok
PROPERTY vm_arithmetic_vs_int64 boundary=900 random=1000000 ok
PROPERTY packed_codec exact=16403 defective_pinned=3280 ok
PROPERTY comparison_vs_int64 boundary=81 random=500000 ok
PROPERTY comparison_equality zero=19683_codes neg_involution=19683 ok
ALL PROPERTY TESTS PASSED (337761089 checks)
```

## 6. What this lab deliberately does not claim

* No claim that ternary is faster than binary on any existing hardware —
  the cost table above says the opposite for software emulation, and the
  value-domain equality for in-register arithmetic.
* **No energy claims whatsoever.** Wall-clock time on a host CPU is not an
  energy measurement; the balanced-ternary energy hypothesis is only
  testable on real ternary or ternary-inspired hardware, which does not
  exist here.
* The VM is a reference interpreter for a 13-opcode teaching/research ISA:
  no division, no ordered comparison, no debugger, no MCU peripherals.
  The honest list of what is *not* implemented lives in the README.
