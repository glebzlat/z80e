import unittest

import yaml

from typing import Optional
from io import StringIO

from tests.common import TESTS_DIR, compile_asm, Tester, TestError

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

        for i, test in enumerate(tests["tests"]):

            def test_fn(self: unittest.TestCase, i=i, test=test):
                if (reason := test.get("skip")) is not None:
                    self.skipTest(reason)

                source: str = test["source"]
                registers: dict[str, int] = test["regs"]
                preset: dict[str, dict[str | int, int]] = test.get("preset", {})
                io: dict[str, dict[int, list[int]]] = preset.get("io", {})

                try:
                    listing, encoded = compile_asm(source)
                    tst = Tester(
                        encoded_program=encoded,
                        expected_registers=registers,
                        preset_registers=preset.get("regs"),
                        preset_memory=preset.get("mem"),
                        memory_checkpoints=test.get("mem"),
                        io_inputs=io.get("in"),
                        io_outputs=io.get("out")
                    )
                    tst.run_test()
                except (AssertionError, TestError) as e:
                    raise create_exception(test["desc"], e, listing) from None

            fn_name = f"test_instruction_{i}"
            test_fn.__name__ = fn_name
            attrs[fn_name] = test_fn

        return super().__new__(cls, name, bases, attrs)


bases = (unittest.TestCase,)
InstructionTest = InstructionTestMeta("InstructionTest", bases, {})
