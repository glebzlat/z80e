import unittest
import subprocess as sp
import re

import yaml

from io import StringIO, BytesIO
from pathlib import Path
from typing import Optional

from z80asm import Z80AsmParser, Z80AsmLayouter, Z80AsmCompiler

FILE_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = FILE_DIR.parent

PROG = PROJECT_ROOT / "build" / "tests" / "z80test"
INSTRUCTIONS = FILE_DIR / "instructions.yaml"
MEMFILE = FILE_DIR / "memfile"
IOFILE = FILE_DIR / "iofile"

TEST_TIMEOUT_SEC = 1


def compile_asm(source: str) -> bytes:
    parser = Z80AsmParser(undoc_instructions=True)

    istream = StringIO(source)
    istream.flush()
    parser.parse_stream(istream)

    layouter = Z80AsmLayouter()
    layouter.layout_program(parser.instructions)

    compiler = Z80AsmCompiler()
    compiler.compile_program(parser.instructions)

    ostream = BytesIO()
    compiler.emit_bytes(ostream)

    return ostream.getvalue()


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


def run_test_program(path: Path, memory: bytes, io: bytes) -> dict[str, int]:
    with open(MEMFILE, "wb") as fout:
        fout.write(memory)
    with open(IOFILE, "wb") as fout:
        fout.write(io)

    cmd = [path, MEMFILE, IOFILE]
    proc = sp.run(cmd, timeout=TEST_TIMEOUT_SEC, stdout=sp.PIPE, stderr=sp.PIPE, text=True, encoding="UTF-8")

    if proc.returncode != 0:
        raise AssertionError(f"process returned code {proc.returncode}: {proc.stderr}")

    return parse_reg_dump(proc.stdout)


def try_find_desc_line(desc: str) -> Optional[int]:
    with open(INSTRUCTIONS, "r") as fin:
        for i, line in enumerate(fin, 1):
            if desc in line:
                return i
    return None


def create_exception(desc: str, what: AssertionError | str) -> AssertionError:
    lineno = try_find_desc_line(desc)
    lineno = f"{lineno}:" if lineno is not None else ""
    msg = f"{INSTRUCTIONS}:{lineno} test {desc!r} failed: {what}"
    return AssertionError(msg)


class InstructionTestMeta(type):

    def __new__(cls, name, bases, attrs):
        with open(INSTRUCTIONS, "r") as fin:
            tests = yaml.load(fin, yaml.Loader)

        def compare_registers_fn(self: unittest.TestCase, result: dict[str, int], clue: dict[str, int]):
            for res_reg, res_val in result.items():
                if (val := clue.get(res_reg)) is not None:
                    self.assertEqual(res_val, val, f"expected register {res_reg} = 0x{val:X}, instead = 0x{res_val:X}")
                else:
                    self.assertEqual(res_val, 0, f"expected register {res_reg} = 0x0, instead = 0x{res_val:X}")

        attrs["compare_registers"] = compare_registers_fn

        for i, test in enumerate(tests["tests"]):

            def test_fn(self: unittest.TestCase, i=i, test=test):
                if (reason := test.get("skip")) is not None:
                    self.skipTest(reason)
                source, registers = test["source"], test["regs"]
                try:
                    encoded = compile_asm(source)
                    result_registers = run_test_program(PROG, encoded, b"")
                    self.compare_registers(result_registers, registers)
                except AssertionError as e:
                    raise create_exception(test["desc"], e) from None
                except sp.TimeoutExpired:
                    raise create_exception(test["desc"], "timeout expired") from None

            fn_name = f"test_instruction_{i}"
            test_fn.__name__ = fn_name
            attrs[fn_name] = test_fn

        return super().__new__(cls, name, bases, attrs)


bases = (unittest.TestCase,)
InstructionTest = InstructionTestMeta("InstructionTest", bases, {})
