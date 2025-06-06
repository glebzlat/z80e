#!/usr/bin/env python3
import re
import sys

from typing import Optional, Any
from enum import Enum, auto
from dataclasses import dataclass, field
from contextlib import contextmanager


class OperandType(Enum):
    Int = auto()
    Reg = auto()
    RegPair = auto()  # rr
    Addr = auto()     # (nn)
    RegAddr = auto()  # (rr)
    IXDAddr = auto()   # (ix+d)
    IYDAddr = auto()   # (iy+d)


@dataclass
class Operand:
    type: OperandType
    value: Any

    def __str__(self) -> str:
        return f"{self.type.name}:{self.value}"


class Opcode(Enum):
    LD = auto()


@dataclass
class Instruction:
    opcode: Opcode
    operands: list[Operand] = field(default_factory=list)

    def __str__(self) -> str:
        operands = ", ".join(str(op) for op in self.operands)
        return f"{self.opcode.name} {operands}"


class Z80Asm:

    registers = {
        "a": 0b111,
        "b": 0b000,
        "c": 0b001,
        "d": 0b010,
        "e": 0b011,
        "h": 0b100,
        "l": 0b101
    }

    def __init__(self, verbose: bool = True):
        self.current_line: str
        self.original_line: str
        self.lineno: int = 0
        self.current_filename: str

        self.verbose = verbose

        self.bt_stack: list = []

    def parse_file(self, file: str):
        self.current_filename = file
        with open(file, "r", encoding="UTF-8") as fin:
            for line in fin:
                print(self.parse_line(line))

    def parse_line(self, line: str) -> Instruction:
        self.current_line = self.original_line = line.rstrip()
        self.lineno += 1
        self.skip()
        if self.consume(r"$"):
            # There is nothing to parse on this line.
            return
        return self.parse_instruction()

    def parse_instruction(self) -> Optional[Instruction]:
        self.push()
        if self.parse_identifier("ld"):
            self.push()
            if r := self.parse_register():
                self.expect_comma()
                self.push()
                if i := self.parse_int():
                    self.pop()
                    return Instruction(Opcode.LD, [r, i])
                self.backtrack()
                if a := self.parse_addr():
                    self.pop()
                    return Instruction(Opcode.LD, [r, a])
                self.backtrack()
                if a := self.parse_ix_d_addr():
                    self.pop()
                    return Instruction(Opcode.LD, [r, a])
                self.backtrack()
                if a := self.parse_iy_d_addr():
                    self.pop()
                    return Instruction(Opcode.LD, [r, a])
                self.backtrack_error()
            self.backtrack()
            self.pop()
            if a := self.parse_addr():
                self.expect_comma()
                if rp := self.parse_hl():
                    print(a)
                    return Instruction(Opcode.LD, [a, rp])
            self.backtrack()
            # self.pop()
        self.backtrack_error()

    def parse_identifier(self, m: str) -> Optional[re.Match]:
        if m := self.consume(rf"{m}\b"):
            return m
        return None

    def parse_operand(self, operands, index, last_index, length, args):
        if index == length:
            return True
        with self.backtrack():
            if op := operands[index]():
                if index != last_index:
                    self.expect_comma()
                args.append(op)
                return self.parse_operand(operands, index + 1, last_index, length, args)
            self.backtrack_error()

    def parse_register(self) -> Optional[Operand]:
        """Parse a register"""
        if m := self.expect(r"[a-ehl]\b", "a register"):
            return Operand(OperandType.Reg, m[0])
        return None

    def parse_register_pair(self) -> Optional[Operand]:
        if m := self.consume(r"(?:bc|de|hl|sp)\b", "a register pair"):
            return Operand(OperandType.RegPair, m[0])
        return None

    def parse_hl(self) -> Optional[Operand]:
        if m := self.parse_identifier("hl"):
            return Operand(OperandType.RegPair, m[0])
        return None

    def parse_addr(self) -> Optional[Operand]:
        if self.consume(r"\("):
            if addr := self.parse_int():
                if self.consume(r"\)"):
                    return Operand(OperandType.Addr, addr.value)
        return None

    def parse_register_addr(self) -> Optional[Operand]:
        if self.consume(r"\("):
            if op := self.parse_register_pair():
                if self.consume(r"\)"):
                    return Operand(OperandType.RegAddr, op.value)

    def parse_ix_d_addr(self) -> Optional[Operand]:
        if self.consume(r"\("):
            if m := self.expect(r"ix\s*([+-])\s*\b"):
                self.current_line = m[1] + self.current_line
                if op := self.parse_int():
                    if self.consume(r"\)"):
                        return Operand(OperandType.IXDAddr, op.value)

    def parse_iy_d_addr(self) -> Optional[Operand]:
        if self.expect(r"\("):
            if m := self.expect(r"iy\s*([+-])\s*\b"):
                self.current_line = m[1] + self.current_line
                if op := self.parse_int():
                    if self.consume(r"\)"):
                        return Operand(OperandType.IYDAddr, op.value)

    def parse_int(self) -> Optional[Operand]:
        """Parse 8-bit integer"""
        negative = False
        if m := self.consume(r"[+-]", skip=False):
            negative = m[0] == "-"
        base = 10
        digits_pattern = r"[1-9][0-9]*\b"
        if self.consume(r"0x", skip=False):
            base = 16
            digits_pattern = r"[0-9a-f]+\b"
        elif self.consume(r"0b", skip=False):
            base = 2
            digits_pattern = r"[01_]+\b"
        elif self.consume(r"0o", skip=False):
            base = 8
            digits_pattern = r"[0-7]+\b"
        if m := self.expect(digits_pattern):
            value = int(m[0].replace("_", ""), base)
            value = value * (-1 if negative else 1)
            return Operand(OperandType.Int, value)
        return None

    def skip(self):
        """Skip over whitespace and comments"""
        while True:
            if self.consume(r"\s+", skip=False):
                continue
            if self.consume(r"(?s);.*", skip=False):
                continue
            break

    def expect_comma(self):
        self.expect(r",", "comma")

    def expect(self, pattern: str, expected: Optional[str] = None, skip: bool = True) -> re.Match:
        """Expect the pattern match at the current position in the string"""
        if m := self.consume(pattern, skip=skip):
            return m
        if self.bt_stack:
            *_, line_errors = self.bt_stack[-1]
            col = self.column()
            if col not in line_errors:
                line_errors[col] = []
            line_errors[col].append(expected or f"'{pattern}'")
            return

        if expected:
            error_message = f"expected {expected}"
        else:
            error_message = f"expected '{pattern}'"
        self.error(error_message)

    def consume(self, pattern: str, skip: bool = True) -> Optional[re.Match]:
        """Try to match a given pattern and optionally skip whitespace if match successful"""
        if m := re.match(pattern, self.current_line):
            self.current_line = self.current_line[len(m[0]):]
            if skip:
                self.skip()
            return m
        return None

    def push(self):
        print("push", self.current_line, self.column())
        self.bt_stack.append((self.current_line, self.column(), {}))

    def backtrack(self):
        line, *_ = self.bt_stack[-1]
        self.current_line = line

    def pop(self):
        print("pop")
        self.bt_stack.pop()

    def backtrack_error(self):
        """Error: no alternative is matched on current position"""
        last_line, last_col, line_expects = self.bt_stack[-1]
        farthest_pos = max(line_expects, key=line_expects.get)
        expects = line_expects[farthest_pos]
        print("backtrack_error", last_col)
        if len(expects) == 1:
            print(f"error: expected {expects[0]}", file=sys.stderr)
        else:
            print("error: expected one of")
            for e in expects:
                print(f"    {e}")
        print(f"at {self.current_filename}:{self.lineno}:{last_col}", file=sys.stderr)
        print(self.original_line, file=sys.stderr)
        print(f"{' ' * last_col}^", file=sys.stderr)
        sys.exit(1)

    def column(self) -> int:
        """Calculate the column of the line"""
        return len(self.original_line) - len(self.current_line)

    def error(self, message: str, *args):
        """Print pretty formatted error message and exit"""
        col = self.column()
        print(f"error: {message.format(*args)}", file=sys.stderr)
        print(f"at {self.current_filename}:{self.lineno}:{col}", file=sys.stderr)
        print(self.original_line, file=sys.stderr)
        print(f"{' ' * col}^", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    from argparse import ArgumentParser

    argparser = ArgumentParser()
    argparser.add_argument("inputs", metavar="INPUT", nargs="*")

    ns = argparser.parse_args()

    asm = Z80Asm()
    for i in ns.inputs:
        asm.parse_file(i)
