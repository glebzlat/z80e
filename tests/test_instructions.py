import unittest
import subprocess as sp

import yaml

from typing import Optional
from io import StringIO

from tests.common import TESTS_DIR, PROG, compile_asm, run_test_program, test_memory

INSTRUCTIONS = TESTS_DIR / "instructions.yaml"


def try_find_desc_line(desc: str) -> Optional[int]:
    with open(INSTRUCTIONS, "r") as fin:
        for i, line in enumerate(fin, 1):
            if desc in line:
                return i
    return None


def create_exception(desc: str, what: AssertionError | str, listing: str) -> AssertionError:
    lineno = try_find_desc_line(desc)
    lineno = f"{lineno}:" if lineno is not None else ""
    stream = StringIO()
    print(f"{INSTRUCTIONS}:{lineno} test {desc!r} failed: {what}", file=stream)
    print(file=stream)
    print("Assembly listing:", file=stream)
    print(listing, file=stream)
    return AssertionError(stream.getvalue())


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

                source: str = test["source"]
                registers: dict[str, int] = test["regs"]
                mem: dict[int, int] = test.get("mem")
                preset: dict[str, dict[str | int, int]] = test.get("preset")

                try:
                    listing, encoded = compile_asm(source)
                    result_registers, memory = run_test_program(
                        PROG,
                        encoded,
                        b"",
                        preset_regs=preset.get("regs") if preset else None,
                        memory_map=preset.get("mem") if preset else None
                    )

                    self.compare_registers(result_registers, registers)

                    if mem is not None:
                        test_memory(self, memory, mem)
                except AssertionError as e:
                    raise create_exception(test["desc"], e, listing) from None
                except sp.TimeoutExpired:
                    raise create_exception(test["desc"], "timeout expired", listing) from None

            fn_name = f"test_instruction_{i}"
            test_fn.__name__ = fn_name
            attrs[fn_name] = test_fn

        return super().__new__(cls, name, bases, attrs)


bases = (unittest.TestCase,)
InstructionTest = InstructionTestMeta("InstructionTest", bases, {})
