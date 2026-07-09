"""
asm.py -- assembler for the balanced-ternary VM.

Syntax (one instruction per line, '#' starts a comment):

    LOAD  R0  5        ; R0 = 5
    LOAD  R1  7        ; R1 = 7
    ADD   R0  R1       ; R0 = R0 + R1
    OUT   R0           ; print R0
    HALT

Operands are balanced-ternary literals (may be negative) for LOAD,
STORE addr, JMP/JZ/JNZ target, or register names R0..R7.

Produces a flat bytecode list of packed 9-trit trytes; each instruction is
(opcode, [operands...]) where every field is one tryte.
"""
from __future__ import annotations

from typing import Dict, List

OPCODES = {
    "HALT": 0, "LOAD": 1, "STORE": 2, "ADD": 3, "SUB": 4, "MUL": 5,
    "NEG": 6, "JMP": 7, "JZ": 8, "JNZ": 9, "OUT": 10, "NOP": 11, "MOV": 12,
}

REG = {f"R{i}": i for i in range(8)}


def _operand(token: str) -> int:
    """Return the tryte-code for a register name or integer literal."""
    from ternary_vm import int_to_tryte_code
    t = token.strip()
    if t.upper() in REG:
        return REG[t.upper()]
    val = int(t)
    if val < -(3 ** 9 - 1) // 2 or val > (3 ** 9 - 1) // 2:
        raise ValueError(f"operand {val} out of 9-trit range")
    return int_to_tryte_code(val)


def assemble(src: str) -> List[int]:
    out: List[int] = []
    labels: Dict[str, int] = {}
    lines = []
    addr = 0
    for raw in src.splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        mnemonic = line.split()[0].upper()
        if mnemonic.endswith(":"):
            labels[mnemonic[:-1]] = addr
            continue
        lines.append(line)
        nops = len(line.split()) - 1
        addr += 1 + nops

    for line in lines:
        parts = line.split()
        mnem = parts[0].upper()
        if mnem not in OPCODES:
            raise ValueError(f"unknown mnemonic {mnem}")
        out.append(OPCODES[mnem])
        for op in parts[1:]:
            if op in labels:
                from ternary_vm import int_to_tryte_code
                out.append(int_to_tryte_code(labels[op]))
            else:
                out.append(_operand(op))
    return out


if __name__ == "__main__":
    import sys
    for code in assemble(sys.stdin.read()):
        print(code)
