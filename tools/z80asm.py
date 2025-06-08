#!/usr/bin/env python3
from __future__ import annotations

import re
import sys

from functools import wraps
from typing import Optional, Callable, Any, TextIO, BinaryIO
from io import StringIO
from dataclasses import dataclass, field
from enum import Enum, auto
from contextlib import contextmanager


class OperandKind(Enum):
    Int8 = auto()
    Int16 = auto()
    Reg = auto()
    RegPair = auto()
    IX = auto()
    IY = auto()
    Addr = auto()
    IXDAddr = auto()
    IYDAddr = auto()
    Const = auto()
    Flag = auto()
    Label = auto()


class Opcode(Enum):
    LD = auto()
    PUSH = auto()
    POP = auto()
    EX = auto()
    EXX = auto()
    LDI = auto()
    LDIR = auto()
    LDD = auto()
    LDDR = auto()
    CPI = auto()
    CPIR = auto()
    CPD = auto()
    CPDR = auto()
    ADD = auto()
    ADC = auto()
    SUB = auto()
    SBC = auto()
    AND = auto()
    OR = auto()
    XOR = auto()
    CP = auto()
    INC = auto()
    DEC = auto()
    JP = auto()


class DirectiveKind(Enum):
    org = auto()
    equ = auto()


@dataclass
class ParseInfo:
    lineno: Optional[int] = field(default=None, repr=False, kw_only=True)
    column: Optional[int] = field(default=None, repr=False, kw_only=True)
    line: Optional[str] = field(default=None, repr=False, kw_only=True)
    filename: Optional[str] = field(default=None, repr=False, kw_only=True)


@dataclass
class Operand(ParseInfo):
    """Class representing an instruction operand

    Name is an optional field valid only for Label and Const operands
    after the layout resolved their values.
    """
    kind: OperandKind
    value: Optional[int | str] = None
    name: Optional[str] = None

    def __str__(self) -> str:
        if self.value:
            return f"{self.kind.name}:{self.value}"
        return self.kind.name


@dataclass
class Statement(ParseInfo):
    pass


@dataclass
class Instruction(Statement):
    opcode: Opcode
    operands: list[Operand] = field(default_factory=list)
    op_bytes: Optional[Callable | tuple[int, ...]] = field(default=None, repr=False)
    length: Optional[int] = field(default=None, repr=False)
    addr: Optional[int] = None

    def __str__(self) -> str:
        operands = ", ".join(str(op) for op in self.operands)
        addr = f"{self.addr:04X} " if self.addr is not None else ""
        return f"{addr}Instruction:{self.opcode.name} {operands}"


@dataclass
class Directive(Statement):
    kind: DirectiveKind
    operands: list[Operand] = field(default_factory=list)

    def __str__(self) -> str:
        operands = ", ".join(str(op) for op in self.operands)
        return f"Directive:{self.kind.name} {operands}"


@dataclass
class Label(Statement):
    name: str
    addr: Optional[int] = None

    def __str__(self) -> str:
        addr = f" ({self.addr:04X})" if self.addr is not None else ""
        return f"Label:{self.name}{addr}"


def ceil_pow2(v: int) -> int:
    """Ceil the value to the nearest power of 2"""
    p = 1
    while v > p:
        p = p << 1
    return p


def memoize(fn: Callable):
    """Packrat parser memoize wrapper"""

    @wraps(fn)
    def wrapper(self, *args):
        pos = self.mark()
        key = (fn, args, pos)
        memo = self.memos.get(key)
        if memo is None:
            result = fn(self, *args)
            self.memos[key] = memo = (result, self.mark())
        else:
            self.reset(memo[1])
        return memo[0]

    return wrapper


class Z80Error(Exception):

    def __str__(self) -> str:
        stream = StringIO()
        for err in self.args:
            print(err, file=stream)
        return stream.getvalue().rstrip()


class Z80AsmParser:

    @dataclass
    class InstructionData:
        """Helper class to store the instruction byte length and convertion function

        Convertion function takes instruction arguments as integers and returns a complete
        instruction code.
        """
        instruction_bytes: int
        op_bytes: Callable

    def definitions(self):
        """Instruction and directive definitions"""

        # Shortcuts
        _ = lambda name: Opcode[name].name
        S = lambda s: self.expect_str(s)
        D = self.InstructionData

        REG = self.parse_register
        I8 = self.parse_i8_op
        I16 = self.parse_i16_op
        DE = self.parse_de
        HL = self.parse_hl
        SP = self.parse_sp
        AF = self.parse_af
        ABC = lambda: self.parse_addr_combine(self.parse_bc)
        ADE = lambda: self.parse_addr_combine(self.parse_de)
        AHL = lambda: self.parse_addr_combine(self.parse_hl)
        ASP = lambda: self.parse_addr_combine(self.parse_sp)
        ADD = lambda: self.parse_addr_combine(self.parse_i16_op)
        IX = self.parse_ix
        IY = self.parse_iy
        IXD = self.parse_ixd_addr
        IYD = self.parse_iyd_addr
        AR = lambda: self.parse_register_name("a")
        IR = lambda: self.parse_register_name("i")
        RR = lambda: self.parse_register_name("r")
        REP = self.parse_regpair
        BIT = self.parse_bit_pos

        LBL = self.parse_label_ref
        ZF = self.parse_zero_flag
        NZ = self.parse_unset_zero_flag

        # Parselet   Expression   Convertion
        # IXD        (ix+<int>)   int
        # IYD        (iy+<int>)   int
        # ADD        (<int>)      int
        # REG        <reg-name>   int
        # REGP       <reg-pair>   int
        # BIT        [0-7]        int
        # I8         <int>        int
        # I16        <int>        (lsb, msb)
        # LBL        <id>         (lsb, msb)
        # CONST      <id>         int
        #
        # Parselets not mentioned in the above table are not converted and
        # a _ placeholder should be used.

        self.mnemonics: dict[Opcode, dict[tuple[Callable, ...], self.InstructionData | tuple[int, ...]]] = {
            _("LD"): {
                # 8-bit load group
                (REG, REG): D(1, lambda d, s: (0x40 | (d << 3) | s,)),
                (REG, I8): D(2, lambda d, s: (0x06 | (d << 3), s)),
                (REG, AHL): D(1, lambda d, _: (0x46 | (d << 3),)),
                (REG, IXD): D(3, lambda d, n: (0xdd, 0x46 | (d << 3), n)),
                (REG, IYD): D(3, lambda d, n: (0xfd, 0x46 | (d << 3), n)),
                (AHL, REG): D(1, lambda _, r: (0x70 | r,)),
                (IXD, REG): D(3, lambda n, r: (0xdd, 0x70 | r, n)),
                (IYD, REG): D(3, lambda n, r: (0xfd, 0x70 | r, n)),
                (AHL, I8): D(2, lambda _, n: (0x36, n)),
                (IXD, I8): D(4, lambda d, n: (0xdd, 0x36, d, n)),
                (IYD, I8): D(4, lambda d, n: (0xfd, 0x36, d, n)),
                (AR, ABC): (0x0a,),
                (AR, ADE): (0x1a,),
                (AR, ADD): D(3, lambda _, n: (0x3a, n[0], n[1])),
                (ABC, AR): (0x02,),
                (ADE, AR): (0x12,),
                (ADD, AR): D(3, lambda n, _: (0x32, n[0], n[1])),
                (AR, IR): (0xed, 0x57),
                (AR, RR): (0xed, 0x5f),
                (IR, AR): (0xed, 0x47),
                (RR, AR): (0xed, 0x4f),

                # 16-bit load group
                (REP, I16): D(3, lambda d, n: (0x01 | (d << 4), n[0], n[1])),
                (IX, I16): D(4, lambda _, n: (0xdd, 0x21, n[0], n[1])),
                (IY, I16): D(4, lambda _, n: (0xfd, 0x21, n[0], n[1])),
                (HL, I16): D(3, lambda _, n: (0x2a, n[0], n[1])),
                (REP, ADD): D(4, lambda d, n: (0xed, 0x4b | (d << 4), n[0], n[1])),
                (IX, ADD): D(4, lambda _, n: (0xdd, 0x2a, n[0], n[1])),
                (IY, ADD): D(4, lambda _, n: (0xfd, 0x2a, n[0], n[1])),
                (ADD, HL): D(3, lambda n, _: (0x22, n[0], n[1])),
                (ADD, REP): D(4, lambda n, r: (0xed, 0x43 | (r << 4), n[0], n[1])),
                (ADD, IX): D(4, lambda n, _: (0xdd, 0x22, n[0], n[1])),
                (ADD, IY): D(4, lambda n, _: (0xfd, 0x22, n[0], n[1])),
                (SP, HL): (0xf9,),
                (SP, IX): (0xdd, 0xf9),
                (SP, IY): (0xfd, 0xf9),
            },
            _("PUSH"): {
                (REP,): D(1, lambda rr: (0xc5 | (rr << 4),)),
                (IX,): (0xdd, 0xe5),
                (IY,): (0xfd, 0xe5),
            },
            _("POP"): {
                (REP,): D(1, lambda rr: (0xc1 | (rr << 4),)),
                (IX,): (0xdd, 0xe1),
                (IY,): (0xfd, 0xe1),
            },
            _("EX"): {
                (DE, HL): (0xeb,),
                (AF,): (0x08,),     # ex af, af'
                (ASP, HL): (0xe3,),
                (ASP, IX): (0xdd, 0xe3),
                (ASP, IY): (0xfd, 0xe3),
            },
            _("EXX"): {
                (): (0xd9,),
            },
            _("LDI"): {
                (): (0xed, 0xa0),
            },
            _("LDIR"): {
                (): (0xed, 0xb0),
            },
            _("LDD"): {
                (): (0xed, 0xa8),
            },
            _("LDDR"): {
                (): (0xed, 0xb8),
            },
            _("CPI"): {
                (): (0xed, 0xa1),
            },
            _("CPIR"): {
                (): (0xed, 0xb1),
            },
            _("CPD"): {
                (): (0xed, 0xa9)
            },
            _("CPDR"): {
                (): (0xed, 0xb9)
            },
            _("ADD"): {
                (AR, REG): D(1, lambda _, r: (0x80 | r,)),
                (AR, I8): D(2, lambda _, n: (0xc6, n)),
                (AR, AHL): (0x86,),
                (AR, IXD): D(3, lambda _, d: (0xdd, 0x86, d)),
                (AR, IYD): D(3, lambda _, d: (0xfd, 0x86, d)),

                # 16-bit
                (AHL, REP): D(1, lambda rp: (0x09 | (rp << 4),)),
                (IX, REP): D(2, lambda _, rp: (0xdd, 0x09 | (rp << 4))),
                (IY, REP): D(2, lambda _, rp: (0xfd, 0x09 | (rp << 4))),
            },
            _("ADC"): {
                (AR, REG): D(1, lambda _, r: (0x88 | r,)),
                (AR, I8): D(2, lambda _, n: (0xce, n)),
                (AR, AHL): (0x8e,),
                (AR, IXD): D(3, lambda _, d: (0xdd, 0x8e, d)),
                (AR, IYD): D(3, lambda _, d: (0xfd, 0x8e, d)),

                # 16-bit
                (AHL, REP): D(2, lambda rp: (0xed, 0x4a | (rp << 4))),
            },
            _("SUB"): {
                (REG,): D(1, lambda r: (0x90 | r,)),
                (I8,): D(2, lambda n: (0xd9, n)),
                (AHL,): (0x96,),
                (IXD,): D(3, lambda d: (0xdd, 0x96, d)),
                (IYD,): D(3, lambda d: (0xfd, 0x96, d)),
            },
            _("SBC"): {
                (AR, REG): D(1, lambda _, r: (0x98 | r,)),
                (AR, I8): D(2, lambda _, n: (0xde, n)),
                (AR, AHL): (0x9e,),
                (AR, IXD): D(3, lambda _, d: (0xdd, 0x9e, d)),
                (AR, IYD): D(3, lambda _, d: (0xfd, 0x9e, d)),

                # 16-bit
                (AHL, REP): D(2, lambda rp: (0xed, 0x42 | (rp << 4))),
            },
            _("AND"): {
                (REG,): D(1, lambda r: (0xa0 | r,)),
                (I8,): D(2, lambda n: (0xe6, n)),
                (AHL,): (0xa6,),
                (IXD,): D(3, lambda d: (0xdd, 0xa6, d)),
                (IYD,): D(3, lambda d: (0xfd, 0xa6, d)),
            },
            _("OR"): {
                (REG,): D(1, lambda r: (0xc0 | r,)),
                (I8,): D(2, lambda n: (0xf6, n)),
                (AHL,): (0xb6,),
                (IXD,): D(3, lambda d: (0xdd, 0xb6, d)),
                (IYD,): D(3, lambda d: (0xfd, 0xb6, d)),
            },
            _("XOR"): {
                (REG,): D(1, lambda r: (0xb8 | r,)),
                (I8,): D(2, lambda n: (0xee, n)),
                (AHL,): (0xae,),
                (IXD,): D(3, lambda d: (0xdd, 0xae, d)),
                (IYD,): D(3, lambda d: (0xfd, 0xae, d)),
            },
            _("CP"): {
                (REG,): D(1, lambda r: (0xc8 | r,)),
                (I8,): D(2, lambda n: (0xfe, n)),
                (AHL,): (0xbe,),
                (IXD,): D(3, lambda d: (0xdd, 0xbe, d)),
                (IYD,): D(3, lambda d: (0xfd, 0xbe, d)),
            },
            _("INC"): {
                (REG,): D(1, lambda r: (0x04 | r,)),
                (AHL,): (0x34,),
                (IXD,): D(3, lambda d: (0xdd, 0x34, d)),
                (IYD,): D(3, lambda d: (0xfd, 0x34, d)),

                # 16-bit
                (REP,): D(1, lambda rp: (0x03 | (rp << 4),)),
                (IX,): (0xdd, 0x23),
                (IY,): (0xfd, 0x23),
            },
            _("DEC"): {
                (REG,): D(1, lambda r: (0x05 | r,)),
                (AHL,): (0x35,),
                (IXD,): D(3, lambda d: (0xdd, 0x35, d)),
                (IYD,): D(3, lambda d: (0xfd, 0x35, d)),

                # 16-bit
                (REP,): D(1, lambda rp: (0x0b | (rp << 4),)),
                (IX,): (0xdd, 0x2b),
                (IY,): (0xfd, 0x2b),
            },
            _("DAA"): {
                (): (0x27,),
            },
            _("CPL"): {
                (): (0x2f,),
            },
            _("NEG"): {
                (): (0xed, 0x44),
            },
            _("CCF"): {
                (): (0x3f,),
            },
            _("SCF"): {
                (): (0x37,),
            },
            _("NOP"): {
                (): (0x00,),
            },
            _("HALT"): {
                (): (0x76,),
            },
            _("DI"): {
                (): (0xf3,),
            },
            _("EI"): {
                (): (0xfb,),
            },
            _("IM"): {
                (S("0"),): (0xed, 0x46),
                (S("1"),): (0xed, 0x56),
                (S("2"),): (0xed, 0x5e),
            },
            _("RLCA"): {
                (): (0x07,),
            },
            _("RLA"): {
                (): (0x17,),
            },
            _("RRCA"): {
                (): (0x0f,),
            },
            _("RRA"): {
                (): (0x1f,),
            },
            _("RLC"): {
                (REG,): D(2, lambda r: (0xcb, 0x00 | r)),
                (AHL,): (0xcb, 0x06),
                (IXD,): D(4, lambda d: (0xdd, 0xcb, d, 0x06)),
                (IYD,): D(4, lambda d: (0xfd, 0xcb, d, 0x06)),
            },
            _("RL"): {
                (REG,): D(2, lambda r: (0xcb, 0x10 | r)),
                (AHL,): (0xcb, 0x16),
                (IXD,): D(4, lambda d: (0xdd, 0xcb, d, 0x16)),
                (IYD,): D(4, lambda d: (0xfd, 0xcb, d, 0x16)),
            },
            _("RRC"): {
                (REG,): D(2, lambda r: (0xcb, 0x08 | r)),
                (AHL,): (0xcb, 0x0e),
                (IXD,): D(4, lambda d: (0xdd, 0xcb, d, 0x0e)),
                (IYD,): D(4, lambda d: (0xfd, 0xcb, d, 0x0e)),
            },
            _("RR"): {
                # XXX: RR reg and RRC reg opcodes are the same??
                (REG,): D(2, lambda r: (0xcb, 0x08 | r)),
                (AHL,): (0xcb, 0x1e),
                (IXD,): D(4, lambda d: (0xdd, 0xcb, d, 0x1e)),
                (IYD,): D(4, lambda d: (0xfd, 0xcb, d, 0x1e)),
            },
            _("SLA"): {
                (REG,): D(2, lambda r: (0xcb, 0x20 | r)),
                (AHL,): (0xcb, 0x26),
                (IXD,): D(4, lambda d: (0xdd, 0xcb, d, 0x26)),
                (IYD,): D(4, lambda d: (0xfd, 0xcb, d, 0x26)),
            },
            _("SRA"): {
                (REG,): D(2, lambda r: (0xcb, 0x28 | r)),
                (AHL,): (0xcb, 0x2e),
                (IXD,): D(4, lambda d: (0xdd, 0xcb, d, 0x2e)),
                (IYD,): D(4, lambda d: (0xfd, 0xcb, d, 0x2e)),
            },
            _("SRL"): {
                (REG,): D(2, lambda r: (0xcb, 0x38 | r)),
                (AHL,): (0xcb, 0x3e),
                (IXD,): D(4, lambda d: (0xdd, 0xcb, d, 0x3e)),
                (IYD,): D(4, lambda d: (0xfd, 0xcb, d, 0x3e)),
            },
            _("RLD"): {
                (): (0xed, 0x6f),
            },
            _("RRD"): {
                (): (0xed, 0x67),
            },
            _("BIT"): {
                (BIT, REG): D(2, lambda b, r: (0xcb, 0x40 | (b << 3) | r)),
                (BIT, AHL): D(2, lambda b, _: (0xcb, 0x46 | (b << 3))),
                (BIT, IXD): D(4, lambda b, d: (0xdd, 0xcb, d, 0x46 | (b << 3))),
                (BIT, IYD): D(4, lambda b, d: (0xfd, 0xcb, d, 0x46 | (b << 3))),
            },
            _("SET"): {
                (BIT, REG): D(2, lambda b, r: (0xcb, 0xc0 | (b << 3) | r)),
                (BIT, AHL): D(2, lambda b, _: (0xcb, 0xc6 | (b << 3))),
                (BIT, IXD): D(4, lambda b, d: (0xdd, 0xcb, d, 0xc6 | (b << 3))),
                (BIT, IYD): D(4, lambda b, d: (0xfd, 0xcb, d, 0xc6 | (b << 3))),
            },
            _("RES"): {
                (BIT, REG): D(2, lambda b, r: (0xcb, 0x80 | (b << 3) | r)),
                (BIT, AHL): D(2, lambda b, _: (0xcb, 0x86 | (b << 3))),
                (BIT, IXD): D(4, lambda b, d: (0xdd, 0xcb, d, 0x86 | (b << 3))),
                (BIT, IYD): D(4, lambda b, d: (0xfd, 0xcb, d, 0x86 | (b << 3))),
            },
            _("JP"): {
                (ZF, I16): D(3, lambda _, n: (0xca, n[0], n[1])),
                (ZF, LBL): D(3, lambda _, n: (0xca, n[0], n[1])),
            }
        }

        self.directives = {
            "org": [self.parse_i16_op],
            "equ": [self.parse_const, self.parse_i8_op]
        }

    def __init__(self):
        self.definitions()

        # Memo table used for backtracking.
        self.memos = {}

        # Dictionary column -> list of failed expects. This
        # is used by error and error_from_last_expect functions
        # to provide a position where parsing failed along with
        # user-friendly messages.
        self.expects = {}

        self.current_line: str
        self.lineno: int = 0
        self.pos: int = 0
        self.current_filename: str

        self.instructions: list[Statement] = []
        self.errors = []

    def parse_file(self, file: str):
        self.current_filename = file
        with open(file, "r", encoding="UTF-8") as fin:
            self.parse_stream(fin)

    def parse_stream(self, stream: TextIO):
        for line in stream:
            self.current_line = line.rstrip()
            self.lineno += 1
            self.skip()
            if self.eol():
                # Nothing to parse, skip the line.
                self.pos = 0
                continue
            try:
                # parse_directive and parse_instruction are two main functions
                # and they behave different than other parse_ functions: they
                # do not return actual parsing product, but only a boolean indicating
                # whether the parsing was successful.
                if not self.parse_directive() and not self.parse_instruction():
                    self.error("syntax error")
            except Z80Error as e:
                self.errors.append(e.args[0])
                continue
            finally:
                # Memo table and expects are cleared for each line. This
                # levels out one of the main packrat parsing's drawbacks:
                # linear memory consumption.
                self.pos = 0
                self.memos.clear()
                self.expects.clear()
        if self.errors:
            raise Z80Error(*self.errors)

    def print_instructions(self, stream: TextIO):
        for i in self.instructions:
            print(i, file=stream)

    @memoize
    def parse_instruction(self) -> bool:
        """Parse `[<identifier>:] [<identifier> [<arg1> [<arg2>]]]` """
        parse = False
        if label := self.parse_label():
            self.instructions.append(label)
            parse = True

        pos = self.mark()
        if mnemonic := self.expect_identifier():
            parselet_alts = self.mnemonics.get(mnemonic.upper())
            if not parselet_alts:
                self.reset(pos)
                self.error("unknown mnemonic: {}", mnemonic)
            pos = self.mark()
            for (alt, data) in parselet_alts.items():
                # Instruction may have several operand type alternatives,
                # e.g. `ld <register> <8-bit-int>` and `ld <addr> <register-pair>`,
                # so backtrack until we found a match or there is no alternative left.
                if args := self.parse_instruction_args(alt):
                    if isinstance(data, self.InstructionData):
                        byte_len, op_bytes = data.instruction_bytes, data.op_bytes
                    else:
                        byte_len, op_bytes = len(data), data
                    opcode = Opcode[mnemonic.upper()]
                    instr = Instruction(opcode, args, op_bytes=op_bytes, length=byte_len)
                    self.instructions.append(instr)
                    if not self.eol():
                        # Expect that there is nothing left on the line
                        self.error("unexpected text")
                    parse = True
                    break
                self.reset(pos)
            else:
                self.error_from_last_expect()

        return parse

    def parse_instruction_args(self, parselets: list[Callable]) -> Optional[list[Any]]:
        """Try to parse instruction args using this sequence of parselets"""
        last_parselet_idx = len(parselets) - 1
        args = []
        for i, parselet in enumerate(parselets):
            if arg := parselet():
                if i != last_parselet_idx and not self.expect_comma():
                    # Ensure args are separated by comma
                    self.error_from_last_expect()
                args.append(arg)
            else:
                return None
        return args

    @memoize
    def parse_directive(self) -> bool:
        """Parse `.<identifier> [...]` """
        if self.expect(r"\."):
            pos = self.mark()
            if name := self.expect_identifier():
                parselets = self.directives.get(name)
                if parselets is None:
                    self.reset(pos)
                    self.error("unknown directive: {}", name)

                args = self.parse_directive_args(parselets)

                if not self.eol():
                    # Expect that there is nothing left on the line
                    self.error("unexpected text")

                directive_kind = DirectiveKind[name]
                directive = self.parseinfo(Directive(directive_kind, args), pos)
                self.instructions.append(directive)

                return True
        return False

    def parse_directive_args(self, parselets: list[Callable]) -> list[Any]:
        """Parse arguments expected by the directive"""
        args = []
        last_parselet_idx = len(parselets) - 1
        for i, parselet in enumerate(parselets):
            if arg := parselet():
                args.append(arg)
            else:
                # Directives have no alternatives, so if the parselet failed,
                # throw an error.
                self.error_from_last_expect()
            if i != last_parselet_idx and not self.expect_comma():
                # Ensure arguments are separated by comma
                self.error_from_last_expect()
        return args

    @memoize
    def parse_label(self) -> Optional[Label]:
        """Parse `<identifier>:` """
        pos = self.mark()
        if (name := self.expect_identifier()) and self.expect_colon():
            return self.parseinfo(Label(name), pos)
        self.reset(pos)
        return None

    @memoize
    def parse_register(self) -> Optional[Operand]:
        pos = self.mark()
        if m := self.expect(r"[a-ehl]\b", "register"):
            return self.parseinfo(Operand(OperandKind.Reg, m[0]), pos)
        self.reset(pos)
        return None

    @memoize
    def parse_register_name(self, reg: str) -> Optional[Operand]:
        pos = self.mark()
        if r := self.expect_str(reg):
            return self.parseinfo(Operand(OperandKind.Reg, r), pos)
        self.reset(pos)
        return None

    @memoize
    def parse_bc(self) -> Optional[Operand]:
        """Parse BC register pair"""
        pos = self.mark()
        if rp := self.expect_str("bc"):
            return self.parseinfo(Operand(OperandKind.RegPair, rp), pos)
        self.reset(pos)
        return None

    @memoize
    def parse_de(self) -> Optional[Operand]:
        """Parse DE register pair"""
        pos = self.mark()
        if rp := self.expect_str("de"):
            return self.parseinfo(Operand(OperandKind.RegPair, rp), pos)
        self.reset(pos)
        return None

    @memoize
    def parse_hl(self) -> Optional[Operand]:
        """Parse HL register pair"""
        pos = self.mark()
        if rp := self.expect_str("hl"):
            return self.parseinfo(Operand(OperandKind.RegPair, rp), pos)
        self.reset(pos)
        return None

    @memoize
    def parse_sp(self) -> Optional[Operand]:
        """Parse SP register pair"""
        pos = self.mark()
        if rp := self.expect_str("sp"):
            return self.parseinfo(Operand(OperandKind.RegPair, rp), pos)
        self.reset(pos)
        return None

    @memoize
    def parse_af(self) -> Optional[Operand]:
        """Parse AF register pair"""
        pos = self.mark()
        if rp := self.expect_str("af"):
            return self.parseinfo(Operand(OperandKind.RegPair, rp), pos)
        self.reset(pos)
        return None

    @memoize
    def parse_addr_combine(self, parselet: Callable) -> Optional[Operand]:
        """Parse anything enclosed in braces"""
        pos = self.mark()
        if self.expect(r"\("):
            if op := parselet():
                if self.expect(r"\)"):
                    return self.parseinfo(Operand(OperandKind.Addr, op.value), pos)
        self.reset(pos)
        return None

    @memoize
    def parse_regpair(self) -> Optional[Operand]:
        """Parse register pair: BC, DE, HL, or SP"""
        pos = self.mark()
        if rp := self.parse_bc():
            return rp
        if rp := self.parse_de():
            return rp
        if rp := self.parse_hl():
            return rp
        if rp := self.parse_sp():
            return rp
        self.reset(pos)
        return None

    @memoize
    def parse_ix(self) -> Optional[Operand]:
        """Parse IX index register"""
        pos = self.mark()
        if self.expect_str("ix"):
            return self.parseinfo(Operand(OperandKind.IX), pos)
        self.reset(pos)
        return None

    @memoize
    def parse_iy(self) -> Optional[Operand]:
        """Parse IY index register"""
        pos = self.mark()
        if self.expect_str("iy"):
            return self.parseinfo(Operand(OperandKind.IY), pos)
        self.reset(pos)
        return None

    @memoize
    def parse_ixd_addr(self) -> Optional[Operand]:
        """Parse IX+d address"""
        pos = self.mark()
        if self.expect(r"\("):
            if self.parse_ix():
                # Ensure a sign here, but do not consume it.
                pos1 = self.mark()
                if self.expect(r"[+-]"):
                    self.reset(pos1)
                    if d := self.expect_integer(8):
                        if self.expect(r"\)"):
                            return self.parseinfo(Operand(OperandKind.IXDAddr, d), pos)
        self.reset(pos)
        return None

    @memoize
    def parse_iyd_addr(self) -> Optional[Operand]:
        """Parse IY+d address"""
        pos = self.mark()
        if self.expect(r"\("):
            if self.parse_iy():
                # Ensure a sign here, but do not consume it.
                pos1 = self.mark()
                if self.expect(r"[+-]"):
                    self.reset(pos1)
                    if d := self.expect_integer(8):
                        if self.expect(r"\)"):
                            return self.parseinfo(Operand(OperandKind.IYDAddr, d), pos)
        self.reset(pos)
        return None

    @memoize
    def parse_const(self) -> Optional[Operand]:
        pos = self.mark()
        if name := self.expect_identifier():
            return self.parseinfo(Operand(OperandKind.Const, name), pos)
        self.reset(pos)
        return None

    @memoize
    def parse_zero_flag(self) -> Optional[Operand]:
        return self.parse_flag("z")

    @memoize
    def parse_unset_zero_flag(self) -> Optional[Operand]:
        return self.parse_flag("nz")

    @memoize
    def parse_carry_flag(self) -> Optional[Operand]:
        return self.parse_flag("c")

    @memoize
    def parse_unset_carry_flag(self) -> Optional[Operand]:
        return self.parse_flag("nc")

    @memoize
    def parse_sign_flag(self) -> Optional[Operand]:
        return self.parse_flag("m")

    @memoize
    def parse_unset_sign_flag(self) -> Optional[Operand]:
        return self.parse_flag("p")

    @memoize
    def parse_pv_flag(self) -> Optional[Operand]:
        return self.parse_flag("pe")

    @memoize
    def parse_unset_pv_flag(self) -> Optional[Operand]:
        return self.parse_flag("po")

    @memoize
    def parse_flag(self, s: str) -> Optional[Operand]:
        pos = self.mark()
        if f := self.expect_str(s):
            return self.parseinfo(Operand(OperandKind.Flag, f), pos)
        self.reset(pos)
        return None

    @memoize
    def parse_label_ref(self) -> Optional[Operand]:
        pos = self.mark()
        if name := self.expect_identifier():
            return self.parseinfo(Operand(OperandKind.Label, name), pos)
        self.reset(pos)
        return None

    @memoize
    def expect_str(self, s: str) -> Optional[str]:
        pos = self.mark()
        if m := self.expect(fr"{s}\b"):
            return m[0]
        self.reset(pos)
        return None

    @memoize
    def expect_identifier(self) -> Optional[str]:
        pos = self.mark()
        if m := self.expect(r"[a-z_][a-z0-9_]*\b", "identifier"):
            return m[0]
        self.reset(pos)
        return None

    @memoize
    def parse_i8_op(self) -> Optional[Operand]:
        """Parse 8 bit integer into an Int Operand"""
        pos = self.mark()
        if i := self.expect_integer(8):
            return self.parseinfo(Operand(OperandKind.Int8, i), pos)
        self.reset(pos)
        return None

    @memoize
    def parse_i16_op(self) -> Optional[Operand]:
        """Parse 16 bit integer into an Int Operand"""
        pos = self.mark()
        if i := self.expect_integer(16):
            return self.parseinfo(Operand(OperandKind.Int16, i), pos)
        self.reset(pos)
        return None

    @memoize
    def parse_bit_pos(self) -> Optional[Operand]:
        """Parse bit position integer (from 0 to 7 inclusively)"""
        pos = self.mark()
        if i := self.expect_integer(8):
            if i < 0 or i > 7:
                self.error("bit position must be in range [0, 7], got {}", i)
            return self.parseinfo(Operand(OperandKind.Int8, i), pos)
        self.reset(pos)
        return None

    @memoize
    def expect_integer(self, bits: int = 8) -> Optional[int]:
        """Parses n-bit integer in decimal, hexadecimal, octal, or binary format"""
        pos = self.mark()
        negative = False
        pattern, base = r"[1-9][0-9]*", 10
        if m := self.expect(r"[+-]"):
            negative = m[0] == "-"
        if m := self.expect(r"0x"):
            pattern, base = r"[0-9a-fA-F]+", 16
        elif m := self.expect(r"0b"):
            pattern, base = r"[01_]+", 2
        elif m := self.expect(r"0o"):
            pattern, base = r"[0-7]+", 8

        if m := self.expect(pattern, "integer"):
            val = int(m[0], base) * (-1 if negative else 1)
            if val.bit_length() > bits:
                self.reset(pos)
                pow2bits = ceil_pow2(val.bit_length())
                self.error(f"expected {bits}-bit integer, got {pow2bits}-bit instead")
            return val

        self.reset(pos)
        return None

    @memoize
    def expect_colon(self) -> Optional[re.Match]:
        pos = self.mark()
        if m := self.expect(":", "colon"):
            return m
        self.reset(pos)
        return None

    @memoize
    def expect_comma(self) -> Optional[re.Match]:
        pos = self.mark()
        if m := self.expect(",", "comma"):
            return m
        self.reset(pos)
        return None

    @memoize
    def expect(self, pattern: str, descr: Optional[str] = None, skip: bool = True) -> Optional[re.Match]:
        """Try to match a pattern and optionally skip whitespace

        If pattern matching failed, then adds an expectation record for the current
        parsing position, which can be used to construct an error later. Description
        is an optional string describing the pattern.

        Caches parsing result.
        """
        pos = self.mark()
        if m := self.consume(pattern, skip=skip):
            return m
        column = self.mark()
        expects_list = self.expects.get(column)
        if expects_list is None:
            expects_list = self.expects[column] = []
        expects_list.append(descr or pattern)
        self.reset(pos)
        return None

    def skip(self):
        """Skip over whitespace and comments"""
        while True:
            if self.consume(r"\s+", skip=False):
                continue
            if self.consume(r"(?s);.*", skip=False):
                continue
            break

    def consume(self, pattern: str, skip: bool = True) -> Optional[re.Match]:
        """Try to match a pattern and optionally skip whitespace"""
        p = re.compile(pattern)
        if m := p.match(self.current_line, pos=self.pos):
            self.pos += len(m[0])
            if skip:
                self.skip()
            return m
        return None

    def error(self, message: str, *args):
        """Raise an error"""
        col = self.mark()
        stream = StringIO()
        print(f"error: {message.format(*args)}", file=stream)
        print(f"at: {self.current_filename}:{self.lineno}:{col}", file=stream)
        print(self.current_line, file=stream)
        print(f"{' ' * col}^", file=stream)
        raise Z80Error(stream.getvalue())

    def error_from_last_expect(self):
        """Raise an error for the farthest parsed position"""
        max_col = max(self.expects)
        expect = self.expects[max_col][-1]
        stream = StringIO()
        print(f"error: expected {expect}", file=stream)
        print(f"at: {self.current_filename}:{self.lineno}:{max_col}", file=stream)
        print(self.current_line, file=stream)
        print(f"{' ' * max_col}^", file=stream)
        raise Z80Error(stream.getvalue())

    def mark(self) -> int:
        """Get current parsing position"""
        return self.pos

    def reset(self, pos: int):
        """Backtrack"""
        self.pos = pos

    def eol(self) -> int:
        """Is there an End Of a Line?"""
        return self.pos == len(self.current_line)

    def parseinfo(self, obj: Statement, pos: int) -> Statement:
        """Augment a statement with the parse info"""
        obj.lineno = self.lineno
        obj.column = pos
        obj.line = self.current_line
        obj.filename = self.current_filename
        return obj


class Z80AsmLayouter:

    def __init__(self, program: list[Statement]):
        self.program = program
        self.errors: list[Z80Error] = []

        self.addr = 0
        self.labels: dict[int, Label] = {}
        self.label_refs: list[Operand] = []
        self.consts: dict[str, int] = {}
        self.const_refs: list[Operand] = []

    def layout_program(self):
        for i, inst in enumerate(self.program):
            self.layout_instruction(i, inst)
        self.assign_label_addrs()
        self.resolve_labels()
        self.resolve_constants()
        if self.errors:
            raise Z80Error(*self.errors)

    def layout_instruction(self, i: int, inst):
        if isinstance(inst, Directive):
            if inst.kind == DirectiveKind.org:
                self.addr = inst.operands[0].value
            elif inst.kind == DirectiveKind.equ:
                self.add_const(inst)
        elif isinstance(inst, Label):
            self.labels[i] = inst
        elif isinstance(inst, Instruction):
            inst.addr = self.addr
            self.addr += inst.length

            for op in inst.operands:
                if op.kind == OperandKind.Label:
                    self.label_refs.append(op)
                elif op.kind == OperandKind.Const:
                    self.const_refs.append(op)

    def assign_label_addrs(self):
        for idx, label in self.labels.items():
            if (addr := self.get_next_addr(idx, self.program)) is not None:
                label.addr = addr

    def get_next_addr(self, idx: int, program: list) -> Optional[int]:
        """Get the address of the next instruction"""
        for i in range(idx, len(program)):
            if isinstance(program[i], Instruction):
                return program[i].addr
        return None

    def resolve_labels(self):
        labels = {label.name: label.addr for label in self.labels.values()}
        for op in self.label_refs:
            if (addr := labels.get(op.value)) is not None:
                op.name = op.value
                op.value = addr
                # Assembly instructions usually may have only one label operand
                break
            else:
                self.error("reference to an undefined label {}", op, op.value)

    def resolve_constants(self):
        for op in self.const_refs:
            if (value := self.consts[op.value]) is not None:
                op.name = op.value
                op.value = value
                break
            else:
                self.error("undefined name {}", op, op.value)

    def add_const(self, obj: Directive):
        assert obj.kind == DirectiveKind.equ
        name, value = obj.operands
        if name.value in self.consts:
            self.error("name {} is redefined", name)
        self.consts[name.value] = value

    def error(self, message: str, stmt: Optional[Statement], *args):
        stream = StringIO()
        print(f"error: {message.format(*args)}", file=stream)
        if stmt is None:
            return
        print(f"{stmt.filename}:{stmt.lineno}:{stmt.column}", file=stream)
        print(stmt.line, file=stream)
        print(f"{' ' * stmt.column}^", file=stream)
        self.errors.append(Z80Error(stream.getvalue()))


class Z80AsmPrinter:
    """Pretty print an assembly program

    If `with_addr` is True, displays a 16-bit hex address for each line.

    If `replace_names` is True, replaces Label and Const references by their
    values.
    """

    def __init__(self, file: TextIO, with_addr: bool = True, replace_names: bool = False):
        self.file = file
        self.with_addr = with_addr
        self.replace_names = replace_names
        self.addr: int = 0

        self.indent_level = ""
        self.print_end = "\n"

    def print_program(self, instructions: list[Statement]):
        for inst in instructions:
            self.print_item(inst)

    def print_item(self, obj: Statement | Operand):
        if isinstance(obj, Directive):
            if obj.kind == DirectiveKind.org:
                self.addr = obj.operands[0].value
            with self.line():
                self.print(f".{obj.kind.name}")
                self.print_list(obj.operands)
        elif isinstance(obj, Instruction):
            with self.indent():
                with self.line():
                    self.print(f"{obj.opcode.name.lower():<6}", end="")
                    self.print_list(obj.operands)
            self.addr += obj.length
        elif isinstance(obj, Label):
            with self.line():
                self.print(f"{obj.name}:")
        elif isinstance(obj, Operand):
            self.print_operand(obj)

    def print_operand(self, obj: Operand):
        if obj.kind == OperandKind.Int8:
            self.print(f"0x{obj.value:02X}")
        elif obj.kind == OperandKind.Int16:
            self.print(f"0x{obj.value:04X}")
        elif obj.kind == OperandKind.IX:
            self.print("ix")
        elif obj.kind == OperandKind.IY:
            self.print("iy")
        elif obj.kind == OperandKind.Addr:
            if isinstance(obj.value, str):
                self.print(f"({obj.value})")
            else:
                self.print(f"(0x{obj.value:04X})")
        elif obj.kind == OperandKind.IXDAddr:
            self.print(f"(ix{obj.value:+})")
        elif obj.kind == OperandKind.IYDAddr:
            self.print(f"(iy{obj.value:+})")
        elif obj.kind == OperandKind.Label or obj.kind == OperandKind.Const:
            if self.replace_names:
                self.print(f"0x{obj.value:04X}")
            else:
                self.print(obj.name)
        else:
            self.print(obj.value)

    def print_list(self, seq: list | tuple, sep=", "):
        """Pretty print a sequence of items delimiting them with the given separator"""
        save_end = self.print_end
        self.print_end = ""
        last_el_idx = len(seq) - 1
        for i, el in enumerate(seq):
            self.print_item(el)
            if i != last_el_idx:
                self.print(sep)
        self.print_end = save_end

    @contextmanager
    def indent(self):
        """Increase indentation level for the following lines"""
        save_indent = self.indent_level
        self.indent_level += "    "
        try:
            yield
        finally:
            self.indent_level = save_indent

    @contextmanager
    def line(self):
        """Put the subsequent prints on one line"""
        save_end = self.print_end
        self.print_end = " "
        if self.with_addr:
            self.print(f"{self.addr:04X}:")
        self.print(self.indent_level, end="")
        try:
            yield
        finally:
            self.print_end = save_end
            self.print()

    def print(self, *args, sep=" ", end=None):
        end = self.print_end if end is None else end
        print(*args, sep=sep, end=end, file=self.file)


class Z80AsmCompiler:

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

    def __init__(self, program: list[Statement]):
        self.program = program

        self.compiled: dict[int, tuple[int, ...]] = {}

    def compile_program(self):
        for inst in self.program:
            self.compile_instruction(inst)

    def compile_instruction(self, inst: Statement):
        """Compile instruction objects into sequences of bytes"""
        if not isinstance(inst, Instruction):
            return
        inst: Instruction
        if isinstance(inst.op_bytes, tuple):
            op_bytes = inst.op_bytes
        else:
            args = []
            for op in inst.operands:
                if op.kind == OperandKind.Int16:
                    args.append(self.i16top(op.value))
                elif op.kind == OperandKind.Label:
                    args.append(self.i16top(op.value))
                elif op.kind == OperandKind.Reg:
                    args.append(self.regtoi(op.value))
                elif op.kind == OperandKind.RegPair:
                    args.append(self.regptoi(op.value))
                elif op.kind == OperandKind.Addr:
                    if isinstance(op.value, int):
                        args.append(self.i16top(op.value))
                    else:
                        args.append(None)
                elif op.kind == OperandKind.Flag:
                    args.append(None)
                else:
                    assert isinstance(op.value, int)
                    args.append(op.value)
            op_bytes = inst.op_bytes(*args)
            assert isinstance(op_bytes, tuple)
            assert all(b.bit_length() <= 8 for b in op_bytes)
        self.compiled[inst.addr] = tuple((i & 0xff) for i in op_bytes)

    def pretty_print(self, file: TextIO, with_addr: bool = True):
        """Pretty print program byte representation"""
        for addr, inst in self.compiled.items():
            if with_addr:
                print(f"{addr:04X} ", end="", file=file)
            bytes_str = " ".join(f"{i & 0xff:02X}" for i in inst)
            print(bytes_str, file=file)

    def emit_bytes(self, file: BinaryIO):
        """Emit compiled program as bytes"""
        for inst in self.compiled.values():
            byte_seq = self.tuptobytes(inst)
            file.write(byte_seq)
        file.flush()

    def i16top(self, i: int) -> tuple[int, int]:
        """Convert 16-bit integer to pair (lsb, msb)"""
        return (i & 0xff, (i >> 8) & 0xff)

    def regtoi(self, reg: str) -> int:
        """Convert register name to its integer representation"""
        return self.REGISTERS[reg]

    def regptoi(self, regp: str) -> int:
        """Convert register pair to its integer representation"""
        return self.REGPAIRS[regp]

    def tuptobytes(self, tup: tuple[int, ...]) -> bytes:
        return b"".join(i.to_bytes() for i in tup)


if __name__ == "__main__":
    from argparse import ArgumentParser

    argparser = ArgumentParser()
    argparser.add_argument("inputs", metavar="INPUT", nargs="*")

    ns = argparser.parse_args()

    asm = Z80AsmParser()
    ltr = Z80AsmLayouter(asm.instructions)
    printer = Z80AsmPrinter(file=sys.stdout, with_addr=True)
    compiler = Z80AsmCompiler(program=asm.instructions)
    for i in ns.inputs:
        try:
            asm.parse_file(i)
            ltr.layout_program()
            printer.print_program(asm.instructions)
            print()
            compiler.compile_program()
            compiler.pretty_print(file=sys.stdout)
        except Z80Error as e:
            print(e)
