# ternary-vm

[![CI](https://github.com/kostyk348/ternary-vm/actions/workflows/ci.yml/badge.svg)](https://github.com/kostyk348/ternary-vm/actions/workflows/ci.yml)


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

[![CI](https://github.com/kostyk348/ternary-vm/actions/workflows/ci.yml/badge.svg)](https://github.com/kostyk348/ternary-vm/actions/workflows/ci.yml)

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

All arithmetic clamps to the 9-trit range (saturation). A future C port
(`ternary_vm.h`) is planned to run the same bytecode on FPU-less MCUs.

## License

Apache 2.0
