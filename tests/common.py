import unittest
import re
import subprocess as sp

from io import StringIO, BytesIO
from pathlib import Path
from typing import Optional

from z80asm import Z80AsmParser, Z80AsmLayouter, Z80AsmCompiler, Z80AsmPrinter

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
    dump_points: Optional[dict[int, dict[str, int]]] = None
) -> list[dict[str, int]]:
    with open(MEMFILE, "wb") as fout:
        # Fill the file to 64KiB with zeros.
        memory += b"\0" * (MEMFILE_SIZE_BYTES - len(memory))
        fout.write(memory)
    with open(IOFILE, "wb") as fout:
        fout.write(io)

    preset = []
    if preset_regs is not None:
        for reg, val in preset_regs.items():
            reg = f"-r{reg}=0x{val:04x}"
            preset.append(reg)

    inspects = []
    if dump_points is not None:
        for pc, regs in dump_points.items():
            dump = f"-dump=0x{pc:04x}"
            inspects.append(dump)

    cmd = [path, MEMFILE, IOFILE, *preset, *inspects]
    proc = sp.run(cmd, timeout=TEST_TIMEOUT_SEC, stdout=sp.PIPE, stderr=sp.PIPE, text=True, encoding="UTF-8")

    if proc.returncode != 0:
        cmd_cat = " ".join(str(i) for i in cmd)
        raise AssertionError(f'process "{cmd_cat}" returned code {proc.returncode}: {proc.stderr}')

    dumps = []
    for chunk in proc.stdout.split("\n\n"):
        dump = parse_reg_dump(chunk)
        dumps.append(dump)

    assert len(dumps) == len(inspects) + 1

    return dumps


def test_memory(case: unittest.TestCase, memmap: dict[int, int]):
    with open(MEMFILE, "rb") as file:
        for addr, val in memmap.items():
            file.seek(addr)
            b = int.from_bytes(file.read(1))
            case.assertEqual(b, val, f"byte on 0x{addr:04X}: expected 0x{val:02X}, got 0x{b:02X}")

