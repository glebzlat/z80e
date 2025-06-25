import sys

from contextlib import contextmanager

DAA = {
    0x00: {
        (0x00, 0x99): 0x00,
        (0x0a, 0x9f): 0x06,
        (0xa0, 0xf9): 0x60,
        (0xaa, 0xff): 0x66
    },
    0x01: {
        (0x00, 0x99): 0x60,
        (0x0a, 0x9f): 0x66,
        (0xa0, 0xf9): 0x60,
        (0xaa, 0xff): 0x66
    },
    0x02: {
        (0x00, 0x99): 0x00,
        (0x0a, 0x9f): 0x00,
        (0xa0, 0xf9): 0x00,
        (0xaa, 0xff): 0x00
    },
    0x03: {
        (0x00, 0x99): 0xa0,
        (0x0a, 0x9f): 0xa0,
        (0xa0, 0xf9): 0xa0,
        (0xaa, 0xff): 0xa0
    },
    0x10: {
        (0x00, 0x99): 0x06,
        (0x0a, 0x9f): 0x06,
        (0xa0, 0xf9): 0x66,
        (0xaa, 0xff): 0x66
    },
    0x11: {
        (0x00, 0x99): 0x66,
        (0x0a, 0x9f): 0x66,
        (0xa0, 0xf9): 0x66,
        (0xaa, 0xff): 0x66
    },
    0x12: {
        (0x00, 0x99): 0xfa,
        (0x0a, 0x9f): 0xfa,
        (0xa0, 0xf9): 0xfa,
        (0xaa, 0xff): 0xfa
    },
    0x13: {
        (0x00, 0x99): 0x9a,
        (0x0a, 0x9f): 0x9a,
        (0xa0, 0xf9): 0x9a,
        (0xaa, 0xff): 0x9a
    },
}

FLAGS = {
    1 << 4: "Half-carry",
    1 << 1: "Subtract",
    1 << 0: "Carry"
}


def is_in_range(v: int, n: int, m: int):
    return ((v & 0xf0) >= (n & 0xf0) and (v & 0x0f) >= (n & 0x0f) and
            (v & 0xf0) <= (m & 0xf0) and (v & 0x0f) <= (m & 0x0f))


def compute_row(ranges: dict[tuple[int, int], int]) -> list[int]:
    row = []
    for i in range(0x0, 0x100):
        res = None
        for (n, m), v in ranges.items():
            if is_in_range(i, n, m):
                res = v
        assert res is not None
        row.append(res)
    return row


def get_flag_names(flags: int) -> str:
    names = []
    for bitmask, name in FLAGS.items():
        if flags & bitmask:
            names.append(name)
    if not names:
        return "None"
    return " + ".join(names)


def compute_daa() -> list[tuple[str, list[int]]]:
    array: list[tuple[str, list[int]]] = []
    for flags, ranges in DAA.items():
        flag_names, row = get_flag_names(flags), compute_row(ranges)
        array.append((flag_names, row))
    return array


class Printer:

    def __init__(self, file):
        self.file = file
        self.indent_level = ""
        self.new_line = True

    def print_daa(self, daa: list[tuple[str, list[int]]]):
        with self.line():
            self.put("static u8 computed_daa[8][256] = {")
        last_idx = len(daa) - 1
        for i, (flag_names, row) in enumerate(daa):
            with self.line():
                self.print_row(i != last_idx, flag_names, row)
        with self.line():
            self.put("};")

    def print_row(self, delimiter: bool, flag_names: str, row: list[int]):
        self.put(f"  {self.format_row(row)}")
        if delimiter:
            self.put(",", separate=False)
        self.put(f"  /* {flag_names} */")

    def format_row(self, row: list[int]) -> str:
        ints = ", ".join(f"0x{i:02x}" for i in row)
        return f"{{{ints}}}"

    @contextmanager
    def line(self):
        try:
            yield
        finally:
            self.put("\n", separate=False)
            self.new_line = True

    def put(self, *args, separate=True):
        if separate and not self.new_line:
            print(" ", end="", file=self.file)
        print(*args, end="", file=self.file)
        self.new_line = False


if __name__ == "__main__":
    daa = compute_daa()
    printer = Printer(sys.stdout)
    printer.print_daa(daa)
