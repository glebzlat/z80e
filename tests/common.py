import datetime as dt

from io import StringIO, BytesIO
from pathlib import Path
from typing import Optional, Callable

from z80asm import Z80AsmParser, Z80AsmLayouter, Z80AsmCompiler, Z80AsmPrinter

from z80py import Z80, InvalidDAAValueError, InvalidOpcodeError

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


class TestError(Exception):
    pass


class Tester:

    def __init__(
        self,
        encoded_program: bytes,
        *,
        expected_registers: dict[str, int] = None,
        preset_registers: Optional[dict[str, int]] = None,
        preset_memory: Optional[dict[int, int]] = None,
        memory_checkpoints: Optional[dict[int, int]] = None,
        io_inputs: Optional[dict[int, list[int]]] = None,
        io_outputs: Optional[dict[int, list[int]]] = None,
    ):
        assert len(encoded_program) < 0x10000, "the length of the program exceeds addressable memory"

        self.expected_registers = expected_registers
        self.encoded_program = encoded_program
        self.preset_registers = preset_registers
        self.preset_memory = preset_memory
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
        if self.preset_memory is not None:
            for addr, byte in self.preset_memory.items():
                assert byte.bit_length() <= 8
                assert addr < len(self.memory)
                self.memory[addr] = byte

    def run_test(self) -> dict[str, int]:
        memread, memwrite, ioread, iowrite = self._get_io_funcs()

        cpu = Z80(memread, memwrite, ioread, iowrite)

        if self.preset_registers is not None:
            for reg, val in self.preset_registers.items():
                cpu.set_register(reg, val)

        time_started = dt.datetime.now()
        timeout = dt.timedelta(seconds=1)
        try:
            while not cpu.halted:
                if dt.datetime.now() - time_started > timeout:
                    raise TestError("timeout expired")
                cpu.instruction()
        except (InvalidOpcodeError, InvalidDAAValueError) as e:
            raise TestError(f"exception {type(e)} is raised: {e}")

        registers = cpu.dump()

        if self.expected_registers is not None:
            self._assert_registers(registers)

        self._assert_io()
        self._assert_memory()

        return registers

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
            assert isinstance(seq, list)
            if count == len(seq):
                raise TestError(f"Attempted read from port {port:#x}, while there is no more data")
            current = seq[count]
            self.io_inputs[port] = [seq, count + 1]
            return current

        def iowrite(addr: int, byte: int) -> int:
            port = addr & 0xff
            if port not in self.io_outputs:
                raise TestError(f"no IO port with port address: {port:#x}")
            seq, count = self.io_outputs[port]
            assert isinstance(seq, list)
            if count == len(seq):
                raise TestError(f"Attempted write to port {port:#x}, while there is no more data expected")
            if byte != seq[count]:
                raise TestError(f"IO port {port:#x}: at {count}: byte {seq[count]:#x} != {byte:#x}")
            self.io_outputs[port] = [seq, count + 1]

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
                left_count = len(seq) - count
                left_bytes = " ".join(f"{i:#04x}" for i in seq[count:])
                raise TestError(f"IO port {port:#04x} input: {left_count} bytes left: {left_bytes}")
        for port, (seq, count) in self.io_outputs.items():
            if count < len(seq):
                left_count = len(seq) - count
                left_bytes = " ".join(f"{i:#04x}" for i in seq[count:])
                raise TestError(f"IO port {port:#04x} output: {left_count} bytes left: {left_bytes}")

    def _assert_memory(self):
        if self.memory_checkpoints is None:
            return
        for addr, byte in self.memory_checkpoints.items():
            if self.memory[addr] != byte:
                raise TestError(f"at {addr:#06x}: expected {byte:#x}, got {self.memory[addr]:#x}")
