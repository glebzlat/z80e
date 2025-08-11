import unittest
import re
import subprocess as sp

from io import StringIO, BytesIO
from pathlib import Path
from typing import Optional

from z80asm import Z80AsmParser, Z80AsmLayouter, Z80AsmCompiler, Z80AsmPrinter

from z80py import Z80

TESTS_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = TESTS_DIR.parent

PROG = PROJECT_ROOT / "build" / "tests" / "z80test"
MEMFILE = TESTS_DIR / "memfile"
IOFILE = TESTS_DIR / "iofile"

TEST_TIMEOUT_SEC = 1

MEMFILE_SIZE_BYTES = 2 ** 16


def compile_asm(source: str) -> tuple[str, bytes]:
    parser = Z80AsmParser(undoc_instructions=True)

    istream = StringIO(source)
    istream.flush()
    parser.parse_stream(istream)

    layouter = Z80AsmLayouter()
    layouter.layout_program(parser.instructions)

    compiler = Z80AsmCompiler()
    compiler.compile_program(parser.instructions)

    sstream = StringIO()
    printer = Z80AsmPrinter(sstream, replace_names=True)
    printer.print_program(parser.instructions)

    ostream = BytesIO()
    compiler.emit_bytes(ostream)

    return sstream.getvalue(), ostream.getvalue()


def parse_reg_dump(s: str) -> dict[str, int]:
    registers = {}
    for i, line in enumerate(s.splitlines()):
        m = re.match(r"(?P<reg1>[a-z]+)\s+(?P<reg1_val>0b[01]+)\s+(?P<reg2>[a-z]+)'?\s+(?P<reg2_val>0b[01]+)", line)

        if m is None:
            raise AssertionError(f"dump parsing failed on line {i}: {line!r}")

        reg1, reg2 = m.group("reg1"), m.group("reg2")
        registers[reg1] = int(m.group("reg1_val"), 2)
        if i < 8 and reg2 in registers:
            reg2 = f"{reg2}_alt"
        registers[reg2] = int(m.group("reg2_val"), 2)
    return registers


def run_test_program(
    path: Path,
    memory: bytes,
    io: bytes,
    *,
    preset_regs: Optional[dict[str, int]] = None,
    dump_points: Optional[dict[int, dict[str, int]]] = None,
    memory_map: Optional[dict[str, int]] = None
) -> tuple[dict[str, int], bytearray]:
    # Fill the file to 64KiB with zeros.
    memory_array: bytearray = bytearray(memory) + bytearray(b"\0" * (MEMFILE_SIZE_BYTES - len(memory)))
    if memory_map is not None:
        # Preset memory contents.
        for addr, byte in memory_map.items():
            assert addr < len(memory_array)
            memory_array[addr] = byte
    memory = bytes(memory_array)

    def memread(addr: int) -> int:
        return memory_array[addr]

    def memwrite(addr: int, byte: int):
        memory_array[addr] = byte

    def ioread(addr: int, byte: int) -> int:
        return 0

    def iowrite(addr: int, byte: int):
        pass

    cpu = Z80(memread, memwrite, ioread, iowrite)

    if preset_regs is not None:
        for reg, val in preset_regs.items():
            cpu.set_register(reg, val)

    while not cpu.halted:
        cpu.instruction()

    return cpu.dump(), memory_array


def test_memory(case: unittest.TestCase, memory_array: bytearray, memmap: dict[int, int]):
    for addr, val in memmap.items():
        b = memory_array[addr]
        case.assertEqual(b, val, f"byte on 0x{addr:04X}: expected 0x{val:02X}, got 0x{b:02X}")
