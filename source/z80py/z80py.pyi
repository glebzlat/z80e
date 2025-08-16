from typing import Callable


class Z80:

    def __init__(
        self,
        memread: Callable[[int], int],
        memwrite: Callable[[int, int], None],
        ioread: Callable[[int, int], int],
        iowrite: Callable[[int, int], None]
    ) -> None:
        ...

    def instruction(self) -> int:
        ...

    def dump(self) -> dict[str, int]:
        ...

    def set_register(self, register: str, value: int) -> None:
        ...

    def get_register(self, register: str) -> int:
        ...

    def reset(self) -> None:
        ...

    @property
    def halted(self) -> bool:
        ...
