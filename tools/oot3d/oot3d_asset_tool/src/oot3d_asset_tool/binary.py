from __future__ import annotations

import struct
from dataclasses import dataclass


class ParseError(ValueError):
    """Raised when an input asset does not match the expected binary format."""


@dataclass(frozen=True)
class BinaryView:
    data: bytes
    source: str = "<memory>"

    def require(self, offset: int, size: int) -> None:
        if offset < 0 or size < 0 or offset + size > len(self.data):
            raise ParseError(
                f"{self.source}: read outside file at 0x{offset:x} size 0x{size:x}"
            )

    def bytes(self, offset: int, size: int) -> bytes:
        self.require(offset, size)
        return self.data[offset : offset + size]

    def cstr(self, offset: int, size: int) -> str:
        raw = self.bytes(offset, size).split(b"\0", 1)[0]
        return raw.decode("ascii", errors="replace")

    def u8(self, offset: int) -> int:
        self.require(offset, 1)
        return self.data[offset]

    def s8(self, offset: int) -> int:
        self.require(offset, 1)
        return struct.unpack_from("<b", self.data, offset)[0]

    def u16(self, offset: int) -> int:
        self.require(offset, 2)
        return struct.unpack_from("<H", self.data, offset)[0]

    def s16(self, offset: int) -> int:
        self.require(offset, 2)
        return struct.unpack_from("<h", self.data, offset)[0]

    def u32(self, offset: int) -> int:
        self.require(offset, 4)
        return struct.unpack_from("<I", self.data, offset)[0]

    def s32(self, offset: int) -> int:
        self.require(offset, 4)
        return struct.unpack_from("<i", self.data, offset)[0]

    def f32(self, offset: int) -> float:
        self.require(offset, 4)
        return struct.unpack_from("<f", self.data, offset)[0]


def align(value: int, alignment: int) -> int:
    return (value + alignment - 1) // alignment * alignment
