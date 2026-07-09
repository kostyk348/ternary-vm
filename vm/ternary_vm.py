"""
ternary_vm.py -- a minimal balanced-ternary virtual machine.

A tryte is 9 trits, each in {-1, 0, +1}, giving a value range of
[- (3**9 - 1)/2, + (3**9 - 1)/2] = [-9841, 9841].

This VM is the spiritual runtime for the "Multi-Phase Ternary Computing"
paradigm described in MPTC, realised here as a clean reference interpreter.
The assembler (asm.py) turns human-readable tryte programs into the flat
bytecode executed below.

Registers: R0..R7 (trytes). Memory: 256 trytes. PC: program counter.
"""
from __future__ import annotations

import sys
from typing import List

TRITS_PER_TRYTE = 9
TRYTE_MAX = (3 ** TRITS_PER_TRYTE - 1) // 2   # 9841
MEM_SIZE = 256

# Opcodes
OP = {
    "HALT": 0, "LOAD": 1, "STORE": 2, "ADD": 3, "SUB": 4, "MUL": 5,
    "NEG": 6, "JMP": 7, "JZ": 8, "JNZ": 9, "OUT": 10, "NOP": 11, "MOV": 12,
}


def to_tryte(value: int) -> int:
    """Clamp an integer into the 9-trit range."""
    return max(-TRYTE_MAX, min(TRYTE_MAX, value))


def tryte_to_int(code: int) -> int:
    """Decode a packed 9-trit tryte (each trit 0/1/2) to signed int.

    Mirrors int_to_tryte_code: the least-significant trit (code % 3) is the
    3**0 place, so we accumulate digit_i * 3**i.
    """
    val = 0
    for i in range(TRITS_PER_TRYTE):
        val += (code % 3) * (3 ** i)
        code //= 3
    # 9 trits, bias 3**8 = 6561
    return val - (3 ** 8)


def int_to_tryte_code(value: int) -> int:
    """Encode a signed int into a packed 9-trit tryte (each trit 0/1/2)."""
    v = to_tryte(value) + (3 ** 8)
    code = 0
    for i in range(TRITS_PER_TRYTE):
        code += (v % 3) * (3 ** i)
        v //= 3
    return code


class TernaryVM:
    def __init__(self, program: List[int], mem_size: int = MEM_SIZE):
        self.prog = program
        self.mem = [0] * mem_size
        self.reg = [0] * 8
        self.pc = 0
        self.output: List[int] = []

    def _fetch(self) -> int:
        if self.pc >= len(self.prog):
            return OP["HALT"]
        op = self.prog[self.pc]
        self.pc += 1
        return op

    def step(self) -> bool:
        """Execute one instruction. Return False on HALT."""
        op = self._fetch()
        if op == OP["HALT"]:
            return False
        if op == OP["LOAD"]:
            r = self._fetch(); v = tryte_to_int(self._fetch())
            self.reg[r] = v
        elif op == OP["STORE"]:
            r = self._fetch(); addr = tryte_to_int(self._fetch())
            self.mem[addr % MEM_SIZE] = int_to_tryte_code(self.reg[r])
        elif op == OP["ADD"]:
            r1 = self._fetch(); r2 = self._fetch()
            self.reg[r1] = to_tryte(self.reg[r1] + self.reg[r2])
        elif op == OP["SUB"]:
            r1 = self._fetch(); r2 = self._fetch()
            self.reg[r1] = to_tryte(self.reg[r1] - self.reg[r2])
        elif op == OP["MUL"]:
            r1 = self._fetch(); r2 = self._fetch()
            self.reg[r1] = to_tryte(self.reg[r1] * self.reg[r2])
        elif op == OP["NEG"]:
            r = self._fetch()
            self.reg[r] = to_tryte(-self.reg[r])
        elif op == OP["JMP"]:
            self.pc = tryte_to_int(self._fetch()) % (len(self.prog) or 1)
        elif op == OP["JZ"]:
            target = tryte_to_int(self._fetch()); r = self._fetch()
            if self.reg[r] == 0:
                self.pc = target % (len(self.prog) or 1)
        elif op == OP["JNZ"]:
            target = tryte_to_int(self._fetch()); r = self._fetch()
            if self.reg[r] != 0:
                self.pc = target % (len(self.prog) or 1)
        elif op == OP["OUT"]:
            r = self._fetch()
            self.output.append(self.reg[r])
        elif op == OP["NOP"]:
            pass
        elif op == OP["MOV"]:
            r1 = self._fetch(); r2 = self._fetch()
            self.reg[r1] = self.reg[r2]
        else:
            raise ValueError(f"unknown opcode {op} at pc {self.pc - 1}")
        return True

    def run(self, max_steps: int = 100000) -> None:
        steps = 0
        while self.step():
            steps += 1
            if steps > max_steps:
                raise RuntimeError("step limit exceeded (possible infinite loop)")


def assemble_source(src: str) -> List[int]:
    """Assemble a .tasm source string into a flat bytecode list."""
    from asm import assemble
    return assemble(src)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("usage: python ternary_vm.py program.tasm")
        sys.exit(1)
    with open(sys.argv[1]) as fh:
        code = assemble_source(fh.read())
    vm = TernaryVM(code)
    vm.run()
    print("output:", vm.output)
