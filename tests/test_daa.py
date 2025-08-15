import sys

from io import StringIO
from enum import IntEnum, auto
from typing import Optional

from tests.common import PROG, compile_asm, Tester, TestError


class Operation(IntEnum):
    ADD = auto()
    SUB = auto()


class Model:

    def __init__(self):
        self.signed = False
        self.zero = False
        self.yf = False
        self.half_carry = False
        self.xf = False
        self.parity_overflow = False
        self.operation: Optional[Operation] = None
        self.carry = False

    def add(self, a: int, b: int) -> int:
        """Calculate a + b and set flags"""

        res = (a + b) % 0x100

        self.signed = Model.is_negative(res)
        self.zero = res == 0
        self.yf = bool(res & (1 << 5))
        self.half_carry = Model.is_half_carry(a, b)
        self.xf = bool(res & (1 << 3))
        self.parity_overflow = Model.is_carry(a, b)
        self.operation = Operation.ADD
        self.carry = Model.is_carry(a, b)

        return res

    def sub(self, a: int, b: int) -> int:
        """Calculate a - b and set flags"""
        assert Model.is_8bit(a) and Model.is_8bit(b)

        res = (a - b) % 0x100

        self.signed = Model.is_negative(res)
        self.zero = res == 0
        self.yf = bool(res & (1 << 5))
        self.half_carry = Model.is_half_borrow(a, b)
        self.xf = bool(res & (1 << 3))
        self.parity_overflow = Model.is_carry(a, b)
        self.operation = Operation.SUB
        self.carry = Model.is_borrow(a, b)

        return res

    def daa(self, a: int) -> int:
        r = NibbleRange

        # Taken from The Undocumented Z80 Documented v0.91
        daa = {
            # CF, HF, high nibble, low nibble -> correction, CF'
            (0, 0, r(0x0, 0x9), r(0x0, 0x9)): (0x00, 0),
            (0, 1, r(0x0, 0x9), r(0x0, 0x9)): (0x06, 0),
            (0, 0, r(0x0, 0x8), r(0xa, 0xf)): (0x06, 0),
            (0, 1, r(0x0, 0x8), r(0xa, 0xf)): (0x06, 0),
            (0, 0, r(0xa, 0xf), r(0x0, 0x9)): (0x60, 1),
            (1, 0, r(0x0, 0xf), r(0x0, 0x9)): (0x60, 1),
            (1, 1, r(0x0, 0xf), r(0x0, 0x9)): (0x66, 1),
            (1, 0, r(0x0, 0xf), r(0xa, 0xf)): (0x66, 1),
            (1, 1, r(0x0, 0xf), r(0xa, 0xf)): (0x66, 1),
            (0, 0, r(0x9, 0xf), r(0xa, 0xf)): (0x66, 1),
            (0, 1, r(0x9, 0xf), r(0xa, 0xf)): (0x66, 1),
            (0, 1, r(0xa, 0xf), r(0x0, 0x9)): (0x66, 1)
        }

        half_carry = {
            # NF, HF, low nibble -> HF'
            (0, 0, r(0x0, 0x9)): 0,
            (0, 0, r(0xa, 0xf)): 1,
            (0, 1, r(0xa, 0xf)): 1,
            (1, 0, r(0x0, 0xf)): 0,
            (1, 1, r(0x6, 0xf)): 0,
            (1, 1, r(0x0, 0x5)): 1
        }

        for (cf, hf, high, low), (corr, res_cf) in daa.items():
            if self.carry == cf and self.half_carry == hf and high_nibble(a) in high and low_nibble(a) in low:
                res_hf = 0
                for (nf, _hf, _low), hf in half_carry.items():
                    if self.nf == nf and self.half_carry == _hf and low_nibble(a) in _low:
                        res_hf = hf
                        break
                else:
                    raise RuntimeError(f"unreachable: 0x{a:02x}")

                corr = (-corr) if self.operation == Operation.SUB else corr
                res = (a + corr) % 0x100

                self.signed = Model.is_negative(res)
                self.zero = res == 0
                self.yf = bool(res & (1 << 5))
                self.half_carry = bool(res_hf)
                self.xf = bool(res & (1 << 3))
                self.parity_overflow = Model.is_even_parity(res)
                # self.operation - unchanged
                self.carry = bool(res_cf)

                return res
        raise RuntimeError(f"unreachable: 0x{a:02x}")

    def flags(self) -> list[bool]:
        nf = True if self.operation == Operation.SUB else False
        return [self.signed, self.zero, self.yf, self.half_carry, self.xf, self.parity_overflow, nf, self.carry]

    def int_flags(self) -> int:
        val = 0
        for i, f in enumerate(reversed(self.flags())):
            val |= int(f) << i
        return val

    @property
    def cf(self) -> int:
        return int(self.carry)

    @property
    def hf(self) -> int:
        return int(self.half_carry)

    @property
    def nf(self) -> int:
        return 1 if self.operation == Operation.SUB else 0

    @staticmethod
    def is_half_carry(a: int, b: int) -> bool:
        return (a & 0x0f) + (b & 0x0f) > 0x0f

    @staticmethod
    def is_carry(a: int, b: int) -> bool:
        return a + b > 0xff

    @staticmethod
    def is_half_borrow(a: int, b: int) -> bool:
        return (a & 0x0f) < (b & 0x0f)

    @staticmethod
    def is_borrow(a: int, b: int) -> bool:
        return a < b

    @staticmethod
    def is_negative(i: int) -> bool:
        return bool(i & 0x80)

    @staticmethod
    def is_8bit(v: int) -> bool:
        return v <= 0xff

    @staticmethod
    def is_even_parity(v: int) -> bool:
        n = 0
        for i in range(0x9):
            n += bool(v & (1 << i))
        return n % 2 == 0


class NibbleRange:

    def __init__(self, low: int, high: int):
        self.low = low
        self.high = high

    def __hash__(self) -> int:
        return hash((self.low, self.high))

    def __contains__(self, val: int) -> bool:
        return val >= self.low and val <= self.high


class ErrorCounter:

    def __init__(self):
        self.n_errors = 0

    def add(self):
        self.n_errors += 1

    def __len__(self) -> int:
        return self.n_errors

    def __bool__(self) -> bool:
        return bool(self.n_errors)


class ProgressPrinter:

    def __init__(self, max_n: int):
        self.max_n = max_n
        self.cur_n = 0

    def print(self):
        self.cur_n += 1
        print(f"{self.cur_n}/{self.max_n}", end='\r', file=sys.stderr)


def high_nibble(i: int) -> int:
    return (i >> 4) & 0x0f


def low_nibble(i: int) -> int:
    return i & 0x0f


def format_flags(flags: int) -> str:

    def bit(n: int) -> int:
        return int(flags & (1 << n) != 0)

    return f"(S={bit(7)}, Z={bit(6)}, Y={bit(5)}, H={bit(4)}, X={bit(3)}, P={bit(2)}, N={bit(1)}, C={bit(0)})"


def test_flags(m: Model, flags: int):
    if m.int_flags() != flags:
        stream = StringIO()
        print("flag register value does not match:", file=stream)
        print(f"  expected flags {format_flags(m.int_flags())}", file=stream)
        print(f"    result flags {format_flags(flags)}", file=stream)

        raise AssertionError(stream.getvalue())


def test_addition(m: Model, errors: ErrorCounter, printer: ProgressPrinter):
    for j in range(0x100):
        try:
            r = m.add(0, j)
            r = m.daa(r)
            source = f"""
                ld b, {j}
                add a, b
                daa
                halt
            """
            _, encoded = compile_asm(source)
            tst = Tester(encoded_program=encoded)
            registers = tst.run_test()
            assert r == registers["a"], f"expected A register == 0x{r:02x}, got 0x{registers["a"]:02x}, daa=0x{r:02x}"
            test_flags(m, registers["f"])
        except AssertionError as e:
            msg = f"test add+daa, a=0x{j:02x}, error: {e}"
            print(msg)
            errors.add()
        printer.print()


def test_subtraction(m: Model, errors: ErrorCounter, printer: ProgressPrinter):
    for i in range(0x100):
        for j in range(0x100):
            try:
                r = m.sub(i, j)
                r = m.daa(r)
                source = f"""
                ld a, {i}
                ld b, {j}
                sub b
                daa
                halt
                """
                _, encoded = compile_asm(source)
                tst = Tester(encoded_program=encoded)
                registers = tst.run_test()
                assert r == registers["a"], f"expected A register == 0x{r:02x}, got 0x{registers["a"]:02x}"
                test_flags(m, registers["f"])
            except AssertionError as e:
                msg = f"test a-b, daa, a=0x{i:02x}, b=0x{j:02x}, error: {e}"
                errors.add()
                print(msg)
            printer.print()


if __name__ == "__main__":
    m = Model()
    printer = ProgressPrinter(255 + 255 * 255)
    errors = ErrorCounter()
    test_addition(m, errors, printer)
    test_subtraction(m, errors, printer)
    if errors:
        print(f"{len(errors)} tests of {printer.max_n} failed")
        exit(1)
    else:
        print("DAA test successful")
