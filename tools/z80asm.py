from __future__ import annotations

import re
import sys

from dataclasses import dataclass, field
from typing import Optional, IO, Callable


REGISTERS = {
    "a": 0b111,
    "b": 0b000,
    "c": 0b001,
    "d": 0b010,
    "e": 0b011,
    "h": 0b100,
    "l": 0b101
}

REGPAIRS = {
    "bc": 0b00,
    "de": 0b01,
    "hl": 0b10,
    "sp": 0b11
}


def strtoi(s: str) -> int:
    s = s.lower()
    negative = s[0] == "-"
    if negative:
        s = s[1:]
    if s.startswith("0x"):
        v = int(s, base=16)
    elif s.startswith("0b"):
        v = int(s, base=2)
    elif s.startswith("0o"):
        v = int(s, base=8)
    else:
        v = int(s)
    return v * (-1 if negative else 1)


def strtoi16(s: str) -> tuple[int, int]:
    """Convert 16bit integer string to the pair (lsb, msb)"""
    s = s.lower()
    negative = s[0] == "-"
    if negative:
        s = s[1:]
    if s.startswith("0x"):
        v = int(s, base=16)
    elif s.startswith("0b"):
        v = int(s, base=2)
    elif s.startswith("0o"):
        v = int(s, base=8)
    else:
        v = int(s)
    v = v * (-1 if negative else 1)
    return (v & 0xff, v >> 8)


def t16toi(tup: tuple[int, int]) -> int:
    """Convert pair (lsb, msb) to integer"""
    return (tup[1] << 8) | tup[0]


def regtoi(s: str) -> int:
    "Register name to int"
    return REGISTERS[s.lower()]


def regptoi(s: str) -> int:
    "Register pair name to int"
    return REGPAIRS[s.lower()]


def hex8(i: int) -> str:
    return f"0x{i & 0xff:0>2x}"


DEC8_REXP = r"(2[0-5][0-5]|1[0-9][0-9]|[1-9][0-9]?)"
HEX8_REXP = r"0[xX][0-9a-fA-F]{1,2}\b"
BIN8_REXP = r"0[bB][01]{1,8}\b"
OCT_REXP = r"0[oO][0-7]+\b"
N8_REXP = rf"([+-]?(?:{DEC8_REXP}|{HEX8_REXP}|{BIN8_REXP}|{OCT_REXP}))"  # 8-bit dec/hex/bin/oct number

N16_REXP = r"([+-]?0[xX][0-9a-fA-F]{1,4}\b)"  # 16-bit hexadecimal number

AR_REXP = r"(?i)a"
IR_REXP = r"(?i)i"
RR_REXP = r"(?i)r"
REG_REXP = r"(?i)[a-fhl]\b"
REGP_REXP = r"(?i)(bc|de|hl|sp)"  # register pairs
AF_REXP = r"(?i)af"
AFA_REXP = r"(?i)af'"
SP_REXP = r"(?i)sp"
HL_REXP = r"(?i)hl"
BC_REXP = r"(?i)bc"
DE_REXP = r"(?i)de"
SPA_REXP = r"(?i)\(sp\)"
HLA_REXP = r"(?i)\(hl\)"
BCA_REXP = r"(?i)\(bc\)"
DEA_REXP = r"(?i)\(de\)"
ADD_REXP = rf"\({N16_REXP}\)"  # (nn)
IX_REXP = r"(?:ix|IX)"
IY_REXP = r"(?:iy|IY)"
IXD_REXP = rf"\({IX_REXP}\+{N8_REXP}\)"  # (ix+d)
IYD_REXP = rf"\({IY_REXP}\+{N8_REXP}\)"  # (iy+d)


@dataclass
class Token:
    value: str
    lineno: int
    column: int
    line: str

    def regtoi(self) -> int:
        """Interpret value as a register and return register's int value"""
        if val := REGISTERS.get(self.value.lower()):
            return val

    def strtoi8(self) -> int:
        """Interpret value as a 8bit integer an parse it"""
        s = self.value.lower()
        negative = s[0] == "-"
        if negative:
            s = s[1:]
        if s.startswith("0x"):
            v = int(s, base=16)
        elif s.startswith("0b"):
            v = int(s, base=2)
        elif s.startswith("0o"):
            v = int(s, base=8)
        else:
            v = int(s)
        return v * (-1 if negative else 1)

    def strtoi16(self) -> int:
        """Interpret value as a 16bit integer"""
        s = self.value.lower()
        negative = s[0] == "-"
        if negative:
            s = s[1:]
        if s.startswith("0x"):
            base = 16
        elif s.startswith("0b"):
            base = 2
        elif s.startswith("0o"):
            base = 8
        else:
            base = 10
        return int(s, base=base) * (-1 if negative else 1)

    def strtoip16(self) -> tuple[int, int]:
        """Interpret value as a 16bit integer and convert it to the pair (lsb, msb)"""
        val = self.strtoi16()
        return (val & 0xff, val >> 8)

    def regptoi(self) -> int:
        """Interpret value as a register pair and return its int value"""
        if val := REGPAIRS.get(self.value.lower()):
            return val


INSTRUCTIONS = {
    "ld": {
        # 8-bit load group
        (REG_REXP, REG_REXP): lambda dst, src: (0x40 | (regtoi(dst) << 3) | regtoi(src),),
        (REG_REXP, N8_REXP): lambda dst, src: (0x40 | (regtoi(dst) << 3) | strtoi(src),),
        (REG_REXP, HLA_REXP): lambda dst, src: (0x40 | (regtoi(dst) << 3) | 0x06,),
        (REG_REXP, IXD_REXP): lambda dst, src: (0xdd, 0x40 | (regtoi(dst) << 3) | 0x06, strtoi(src)),
        (REG_REXP, IYD_REXP): lambda dst, src: (0xfd, 0x40 | (regtoi(dst) << 3) | 0x06, strtoi(src)),
        (HLA_REXP, REG_REXP): lambda dst, src: (regtoi(src) | 0x70,),
        (IXD_REXP, REG_REXP): lambda dst, src: (0xdd, 0x70 | regtoi(src), strtoi(dst)),
        (IYD_REXP, REG_REXP): lambda dst, src: (0xfd, 0x70 | regtoi(src), strtoi(dst)),
        (HLA_REXP, N8_REXP): lambda dst, src: (0x36, strtoi(src)),
        (IXD_REXP, N8_REXP): lambda dst, src: (0xdd, 0x36, regtoi(dst), strtoi(src)),
        (IYD_REXP, N8_REXP): lambda dst, src: (0xfd, 0x36, regtoi(dst), strtoi(src)),
        (AR_REXP, BCA_REXP): (0x0a,),
        (AR_REXP, DEA_REXP): (0x1a,),
        (AR_REXP, ADD_REXP): lambda dst, src: (0x3a, *strtoi16(src)),
        (BCA_REXP, AR_REXP): (0x02,),
        (DEA_REXP, AR_REXP): (0x12,),
        (ADD_REXP, AR_REXP): lambda dst, src: (0x32, *strtoi16(dst)),
        (AR_REXP, IR_REXP): (0xed, 0x57),
        (AR_REXP, RR_REXP): (0xed, 0x5f),
        (IR_REXP, AR_REXP): (0xed, 0x47),
        (RR_REXP, AR_REXP): (0xed, 0x4f),

        # 16-bit load group
        (REGP_REXP, N16_REXP): lambda dst, src: (0x01 | (regptoi(dst) << 4), *strtoi16(src)),
        (IX_REXP, N16_REXP): lambda dst, src: (0xdd, 0x21, *strtoi(src)),
        (IY_REXP, N16_REXP): lambda dst, src: (0xfd, 0x21, strtoi16(src)),
        (HL_REXP, ADD_REXP): lambda dst, src: (0x2a, strtoi16(src)),
        (REGP_REXP, ADD_REXP): lambda dst, src: (0xed, 0x4b | (regptoi(dst) << 4), *strtoi16(src)),
        (IX_REXP, ADD_REXP): lambda dst, src: (0xdd, 0x2a, *strtoi16(src)),
        (IY_REXP, ADD_REXP): lambda dst, src: (0xfd, 0x2a, *strtoi16(src)),
        (ADD_REXP, HL_REXP): lambda dst, src: (0x22, strtoi16(dst)),
        (ADD_REXP, REGP_REXP): lambda dst, src: (0xed, 0x43 | (regptoi(src) << 4), *strtoi16(dst)),
        (ADD_REXP, IX_REXP): lambda dst, src: (0xdd, 0x22, *strtoi16(dst)),
        (ADD_REXP, IY_REXP): lambda dst, src: (0xfd, 0x22, *strtoi16(dst)),
        (SP_REXP, HL_REXP): (0xf9,),
        (SP_REXP, IX_REXP): (0xdd, 0xf9),
        (SP_REXP, IY_REXP): (0xfd, 0xf9),
    },
    "push": {
        (REGP_REXP,): lambda r: (0xc5 | (regptoi(r) << 4),),
        (IX_REXP,): (0xdd, 0xe5),
        (IY_REXP,): (0xfd, 0xe5),
    },
    "pop": {
        (REGP_REXP,): lambda r: (0xc1 | (regptoi(r) << 4),),
        (IX_REXP,): (0xdd, 0xe1),
        (IY_REXP,): (0xfd, 0xe1),
    },
    "ex": {
        (DE_REXP, HL_REXP): (0xeb,),
        (AF_REXP, AFA_REXP): (0x08,),
        (SPA_REXP, HL_REXP): (0xe3,),
        (SPA_REXP, IX_REXP): (0xdd, 0xe3),
        (SPA_REXP, IY_REXP): (0xfd, 0xe3),
    },
    "exx": {
        (): (0xd9,)
    },
    "ldi": {
        (): (0xed, 0xa0)
    },
    "ldir": {
        (): (0xed, 0xb0)
    },
    "ldd": {
        (): (0xed, 0xa8)
    },
    "lddr": {
        (): (0xed, 0xb8)
    },
    "cpi": {
        (): (0xed, 0xa1)
    },
    "cpir": {
        (): (0xed, 0xb1)
    },
    "cpd": {
        (): (0xed, 0xa9)
    },
    "cpdr": {
        (): (0xed, 0xb9)
    },
    "add": {
        (AR_REXP, REG_REXP): lambda r: (0x80 | regtoi(r),),
        (AR_REXP, N8_REXP): lambda n: (0xc6, strtoi(n)),
        (AR_REXP, HLA_REXP): (0x86,),
        (AR_REXP, IXD_REXP): lambda n: (0xdd, 0x86, strtoi(n)),
        (AR_REXP, IYD_REXP): lambda n: (0xfd, 0x86, strtoi(n)),
    },
    "adc": {
        (AR_REXP, REG_REXP): lambda r: (0x88 | regtoi(r),),
        (AR_REXP, N8_REXP): lambda n: (0xce, strtoi(n)),
        (AR_REXP, HLA_REXP): (0x8e,),
        (AR_REXP, IXD_REXP): lambda d: (0xdd, 0x8e, strtoi(d)),
        (AR_REXP, IYD_REXP): lambda d: (0xfd, 0x8e, strtoi(d)),
    },
    "sub": {
        (REG_REXP,): lambda r: (0x90 | regtoi(r),),
        (N8_REXP,): lambda n: (0xd6, strtoi(n)),
        (HLA_REXP,): (0x96,),
        (IXD_REXP,): lambda d: (0xdd, 0x96, strtoi(d)),
        (IYD_REXP,): lambda d: (0xfd, 0x96, strtoi(d)),
    },
    "sbc": {
        (REG_REXP,): lambda r: (0x98 | regtoi(r),),
        (N8_REXP,): lambda n: (0xde, strtoi(n)),
        (HLA_REXP,): (0x9e,),
        (IXD_REXP,): lambda d: (0xdd, 0x9e, strtoi(d)),
        (IYD_REXP,): lambda d: (0xfd, 0x9e, strtoi(d)),
    },
    "and": {
        (REG_REXP,): lambda r: (0xa0 | regtoi(r),),
        (N8_REXP,): lambda n: (0xe6, strtoi(n)),
        (HLA_REXP,): (0xa6,),
        (IXD_REXP,): lambda d: (0xdd, 0xa6, strtoi(d)),
        (IYD_REXP,): lambda d: (0xfd, 0xa6, strtoi(d)),
    },
    "or": {
        (REG_REXP,): lambda r: (0xc0 | regtoi(r),),
        (N8_REXP,): lambda n: (0xf6, strtoi(n)),
        (HLA_REXP,): (0xb6,),
        (IXD_REXP,): lambda d: (0xdd, 0xb6, strtoi(d)),
        (IYD_REXP,): lambda d: (0xfd, 0xb6, strtoi(d)),
    },
    "xor": {
        (REG_REXP,): lambda r: (0xb8 | regtoi(r),),
        (N8_REXP,): lambda n: (0xee, strtoi(n)),
        (HLA_REXP,): (0xae,),
        (IXD_REXP,): lambda d: (0xdd, 0xae, strtoi(d)),
        (IYD_REXP,): lambda d: (0xfd, 0xae, strtoi(d)),
    },
    "cp": {
        (REG_REXP,): lambda r: (0xc8 | regtoi(r),),
        (N8_REXP,): lambda n: (0xfe, strtoi(n)),
        (HLA_REXP,): (0xbe,),
        (IXD_REXP,): lambda d: (0xdd, 0xbe, strtoi(d)),
        (IYD_REXP,): lambda d: (0xfd, 0xbe, strtoi(d)),
    },
    "inc": {
        (REG_REXP,): lambda r: (0x04 | regtoi(r),),
        (HLA_REXP,): (0x34,),
        (IXD_REXP,): lambda d: (0xdd, 0x34, strtoi(d)),
        (IYD_REXP,): lambda d: (0xfd, 0x34, strtoi(d)),
    },
    "dec": {
        (REG_REXP,): lambda r: (0x05 | regtoi(r),),
        (HLA_REXP,): (0x35,),
        (IXD_REXP,): lambda d: (0xdd, 0x35, strtoi(d)),
        (IYD_REXP,): lambda d: (0xfd, 0x35, strtoi(d)),
    },
    # Page 172
    "jp": {
        (r"([a-zA-Z_][a-zA-Z0-9_]*)",): lambda n: (0xc3, LabelRef(value=n))
    }
}


@dataclass
class OpcodeBase:
    lineno: Optional[int] = field(default=None, repr=False, kw_only=True)
    column: Optional[int] = field(default=None, repr=False, kw_only=True)
    line: Optional[str] = field(default=None, repr=False, kw_only=True)


@dataclass
class Operand(OpcodeBase):
    value: Optional[int] = field(default=None, kw_only=True)


@dataclass
class Instruction(OpcodeBase):
    name: str
    operands: tuple[int | Operand, ...]

    def __str__(self) -> str:
        operands = " ".join(hex8(b) if isinstance(b, int) else b for b in self.operands)
        return f"instruction: {self.name}, {operands}"


@dataclass
class Directive(OpcodeBase):
    name: str
    operands: tuple[int | Operand, ...]

    def __str__(self) -> str:
        operands = " ".join(hex8(b) if isinstance(b, int) else b for b in self.operands)
        return f"  directive: {self.name}, {operands}"


@dataclass
class OrgDirective(OpcodeBase):
    value: Optional[int] = field(default=None, kw_only=True)


@dataclass
class EquDirective(OpcodeBase):
    """Represents .equ directive"""
    name: str
    value: Optional[int] = field(default=None, kw_only=True)


@dataclass
class Label(OpcodeBase):
    """A label"""
    name: str

    def __str__(self) -> str:
        return f"      label: {self.name}"


@dataclass
class Const(Operand):
    """Represents a constant operand in the instruction"""
    name: str
    value: Optional[int] = field(default=None, kw_only=True)


@dataclass
class LabelRef(Operand):
    """Represents an operand referencing to a label"""
    name: str


class Z80Asm:

    N16_REXP_IGNORECASE = r"(?i)" + N16_REXP

    def __init__(self, verbose: bool = False):
        self.labels: dict[str, int] = {}
        self.instructions: list[OpcodeBase] = []

        self.current_filename = None
        self.current_line = None
        self.original_line = None
        self.lineno = 0

        self.verbose = verbose

    def parse_file(self, filename: str):
        self.current_filename = filename
        with open(filename, "r", encoding="UTF-8") as fin:
            self.parse_stream(fin)

    def parse_stream(self, stream: IO):
        if self.current_filename is None:
            self.current_filename = "<stream>"
        for line in stream:
            self.current_line = line.rstrip()
            self.original_line = self.current_line
            self.lineno += 1
            self.parse_line()

    def dump(self, file):
        for i in self.instructions:
            print(i, file=file)

    def parse_line(self):
        self.log("line: {!r}", self.current_line)
        self.skip()
        if self.consume(r"\."):
            self.parse_directive()
        else:
            self.parse_instruction()

    def parse_instruction(self):
        self.parse_label()
        if self.consume(r"\n*$", skip=False):
            return
        # Consume the instruction name
        if m := self.consume(r"[a-zA-Z]+\b"):
            if instruction := INSTRUCTIONS.get(m[0].lower()):
                self.log("instruction: {}", m[0])
                operands = self.parse_operands(instruction)
                self.instructions.append(
                    Instruction(name=m[0], operands=operands, lineno=self.lineno, column=self.column(),
                                line=self.original_line))
                return
            self.error("unknown instruction {}", m[0])
        self.error("syntax error")

    def parse_operands(self, instruction: dict) -> tuple[int | str, ...]:
        """Tokenize and parse operands"""
        # Save the original line for backtracking.
        line_save = self.current_line
        # Save the line which is parsed farther to report the position
        # of an erroneous operand more accurately.
        farthest_str = line_save
        for operands, opcode in instruction.items():
            last_index = len(operands) - 1
            args = []
            # If instruction does not take operands, it is parsed.
            parsed = not operands
            for i, op in enumerate(operands):
                if m := self.consume(op):
                    self.log("{} operand: {}", i, m[0])
                    if i != last_index and not self.consume(r","):
                        # Ensure operands are delimited by comma
                        self.error("expected comma")
                    args.append(m)
                    parsed = True
                else:
                    if len(farthest_str) > len(self.current_line):
                        farthest_str = self.current_line
                    self.current_line = line_save
                    parsed = False
                    break

            if parsed:
                return self.get_opcode(opcode, args)

        self.current_line = farthest_str
        self.error("unrecognized operand/syntax error")

    def get_opcode(self, opcode: tuple[int, ...] | Callable, args: list[re.Match]) -> tuple[int | str, ...]:
        """Parse opcode arguments and return instruction bytes"""
        if callable(opcode):
            arg_strings = [m[0] if len(m.groups()) == 0 else m[1] for m in args]
            op = opcode(*arg_strings)
        else:
            op = opcode
        if self.verbose:
            hex_opcode = " ".join(hex(a & 0xff) for a in op)
            self.log("opcode: {}", hex_opcode)
        return op

    def parse_directive(self):
        if self.consume(r"org"):
            if m := self.consume(self.N16_REXP_IGNORECASE):
                addr = t16toi(strtoi16(m[0]))
                self.instructions.append(
                    OrgDirective(value=addr, lineno=self.lineno, column=self.column(), line=self.original_line))

    def parse_label(self):
        if m := self.consume(r"([a-z_][a-z0-9_]*)\s*:"):
            self.log("label: {}", m[1])
            if m[1] in self.labels:
                self.error("label {} is redefined", m[1])
            self.instructions.append(
                Label(name=m[1], lineno=self.lineno, column=self.column(), line=self.original_line))

    def skip(self):
        """Skip over whitespace and comments"""
        while True:
            if self.consume(r"\s+", skip=False):
                continue
            if self.consume(r"(?s);.*", skip=False):
                continue
            break

    def consume(self, pattern: str, skip: bool = True) -> Optional[re.Match]:
        """Try to match a given pattern and optionally skip whitespace if match successful"""
        column = self.column()
        if m := re.match(pattern, self.current_line):
            self.current_line = self.current_line[len(m[0]):]
            if skip:
                self.skip()
            return m
        return None

    def error(self, message: str, *args):
        consumed = self.column()
        print(f"error: {message.format(*args)}", file=sys.stderr)
        print(f"at {self.current_filename}:{self.lineno}", file=sys.stderr)
        print(self.original_line, file=sys.stderr)
        print(f"{' ' * consumed}^", file=sys.stderr)
        sys.exit(1)

    def column(self):
        return len(self.original_line) - len(self.current_line)

    def log(self, message: str, *args):
        if self.verbose:
            print(f"({self.lineno})", message.format(*args))


if __name__ == "__main__":
    from io import StringIO

    asm = Z80Asm(verbose=False)

    sio = StringIO()
    sio.write(".org 0x8000\n")
    sio.write("start:\n")
    sio.write("  ld a, b\n")
    sio.write("  ld bc, 0xdead\n")
    sio.write("  ld bc, 0xdead\n")
    sio.write("  exx\n")
    sio.write("jp start\n")
    sio.flush()
    sio.seek(0)

    asm.parse_stream(sio)
    asm.dump(sys.stdout)
