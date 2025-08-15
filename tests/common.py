import unittest
import re

from io import StringIO, BytesIO
from pathlib import Path
from typing import Optional, Callable

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


class TestError(Exception):
    pass


class Tester:

    def __init__(
        self,
        encoded_program: bytes,
        *,
        expected_registers: dict[str, int],
        preset_regs: Optional[dict[str, int]] = None,
        memory_checkpoints: Optional[dict[int, int]] = None,
        io_inputs: Optional[dict[int, list[int]]] = None,
        io_outputs: Optional[dict[int, list[int]]] = None
    ):
        if len(encoded_program) > 0xffff:
            raise TestError("the length of the program exceeds 0xffff")

        self.expected_registers = expected_registers
        self.encoded_program = encoded_program
        self.preset_regs = preset_regs
        self.memory_checkpoints = memory_checkpoints
        self.io_inputs = io_inputs
        self.io_outputs = io_outputs

        if self.io_inputs is not None:
            self.io_inputs = {port: [seq, 0] for port, seq in self.io_inputs.items()}
        else:
            self.io_inputs = {}

        if self.io_outputs is not None:
            self.io_outputs = {port: [seq, 0] for port, seq in self.io_outputs.items()}
        else:
            self.io_outputs = {}

        self.memory = bytearray(self.encoded_program) + bytearray(b"\0" * (0x10000 - len(self.encoded_program)))

    def run_test(self):
        memread, memwrite, ioread, iowrite = self._get_io_funcs()

        cpu = Z80(memread, memwrite, ioread, iowrite)

        if self.preset_regs is not None:
            for reg, val in self.preset_regs.items():
                cpu.set_register(reg, val)

        while not cpu.halted():
            cpu.instruction()

        registers = cpu.dump()
        self._assert_registers(registers)

        self._assert_io()
        self._assert_memory()

    def _get_io_funcs(self) -> tuple[Callable, Callable, Callable, Callable]:

        def memread(addr: int) -> int:
            if addr >= len(self.memory):
                raise TestError(f"address {addr:#x} is out of memory bound")
            return self.memory[addr]

        def memwrite(addr: int, byte: int):
            if addr >= len(self.memory):
                raise TestError(f"address {addr:#x} is out of memory bound")
            self.memory[addr] = byte

        def ioread(addr: int, byte: int) -> int:
            port = addr & 0xff
            if port not in self.io_inputs:
                raise TestError(f"no IO port with port address: {port:#x}")
            seq, count = self.io_inputs[port]
            if count == len(seq):
                raise TestError(f"Attempted read from port {port:#x}, while there is no more data")
            current = seq[count]
            self.io_inputs[port] = [seq, count + 1]
            return current

        def iowrite(addr: int, byte: int) -> int:
            port = addr & 0xff
            if port not in self.io_inputs:
                raise TestError(f"no IO port with port address: {port:#x}")
            seq, count = self.io_inputs[port]
            if count == len(seq):
                raise TestError(f"Attempted write to port {port:#x}, while there is no more data")
            if byte != seq[count]:
                raise TestError(f"IO port {port:#x}: at {count}: byte {seq[count]:#x} != {byte:#x}")

        return memread, memwrite, ioread, iowrite

    def _assert_registers(self, registers: dict[str, int]):
        for reg, val in registers.items():
            clue = self.expected_registers.get(reg, 0)
            if val == clue:
                continue
            raise TestError(f"register {reg} expected {clue:#x}, got {val:#x}")

    def _assert_io(self):
        for port, (seq, count) in self.io_inputs.items():
            if count < len(seq):
                raise TestError(f"port IO {port:#x}: not all bytes are outputted")
        for port, (seq, count) in self.io_outputs.items():
            if count < len(seq):
                raise TestError(f"IO port {port:#x}: not all bytes are received")

    def _assert_memory(self):
        for addr, byte in self.memory_checkpoints.items():
            if self.memory[addr] != byte:
                raise TestError(f"at {addr:#04x}: expected {byte:#x}, got {self.memory[addr]:#x}")


def run_test_program(
    path: Path,
    memory: bytes,
    io: bytes,
    *,
    preset_regs: Optional[dict[str, int]] = None,
    dump_points: Optional[dict[int, dict[str, int]]] = None,
    memory_map: Optional[dict[str, int]] = None,
    io_input: Optional[dict[int, int]] = None,
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
