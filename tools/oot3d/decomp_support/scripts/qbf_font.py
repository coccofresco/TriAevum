"""Bounded OOT3D QBF coverage reader; native metrics remain authoritative.

Evidence: native 002d2674 (directory/glyph lookup), 002d2664 (fallback),
002da7b8/002da7d8/002da7c8 (dimensions), 002d2504 (A4 nibble order).
Only the verified format-2, 4-bpp Latin coverage encoding is accepted here.
"""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
import struct


MAX_FONT_BYTES = 32 * 1024 * 1024


@dataclass(frozen=True)
class QbfCharacter:
    code: int
    glyph_index: int
    # Preserve all four bytes, including the byte not exported by 002d2674.
    # Their individual layout meaning is deliberately not inferred here.
    metrics: bytes


@dataclass(frozen=True)
class QbfFont:
    data: bytes
    characters: tuple[QbfCharacter, ...]
    glyph_count: int
    fallback_code: int
    width: int
    height: int
    glyph_offset: int

    @property
    def glyph_bytes(self) -> int:
        return self.width * self.height // 2

    @property
    def sha256(self) -> str:
        return hashlib.sha256(self.data).hexdigest()

    def glyph_coverage(self, index: int) -> bytes:
        """Linear alpha, top row first; no PICA swizzle or geometry changes."""
        if not 0 <= index < self.glyph_count:
            raise ValueError("QBF glyph index outside bitmap table")
        start = self.glyph_offset + index * self.glyph_bytes
        result = bytearray(self.width * self.height)
        for offset, value in enumerate(self.data[start:start + self.glyph_bytes]):
            # Native 002d2504 swaps nibbles when writing PICA A4 tiles.
            result[2 * offset] = (value >> 4) * 17
            result[2 * offset + 1] = (value & 15) * 17
        return bytes(result)


def parse_qbf(data: bytes) -> QbfFont:
    if not 16 <= len(data) <= MAX_FONT_BYTES or data[:4] != b"QBF1":
        raise ValueError("invalid QBF header or size")
    count, glyphs, fallback = struct.unpack_from("<HHH", data, 4)
    bpp, width, height, encoding = struct.unpack_from("4B", data, 12)
    if bpp != 4 or encoding != 2:
        raise ValueError("unsupported QBF coverage encoding (expected format 2/A4)")
    if not width or not height or width % 8 or height % 8:
        raise ValueError("QBF cell dimensions must be nonzero multiples of eight")
    offset = 16 + count * 8
    expected = offset + glyphs * width * height // 2
    if not count or not glyphs or expected != len(data):
        raise ValueError("QBF directory/bitmap size mismatch")
    records = []
    previous = -1
    for index in range(count):
        position = 16 + index * 8
        code, glyph = struct.unpack_from("<HH", data, position)
        if code <= previous:
            raise ValueError("QBF character directory is not strictly ordered")
        if glyph >= glyphs:
            raise ValueError("QBF character references an absent glyph")
        records.append(QbfCharacter(code, glyph, data[position + 4:position + 8]))
        previous = code
    if fallback not in {record.code for record in records}:
        raise ValueError("QBF fallback character is absent")
    return QbfFont(bytes(data), tuple(records), glyphs, fallback, width, height, offset)


def pair_font_coverage(native: QbfFont, replacement: QbfFont) -> dict:
    """Map coverage by character, never by presumed matching glyph indices.

    The logical cell and every metric come from the original. Source dimensions
    are independent; unequal or fractional densities need a different consumer.
    """
    if (replacement.width % native.width or replacement.height % native.height or
            replacement.width // native.width != replacement.height // native.height):
        raise ValueError("QBF replacement needs a uniform integral coverage density")
    density = replacement.width // native.width
    if density < 1:
        raise ValueError("QBF replacement has lower coverage density")
    by_code = {entry.code: entry for entry in replacement.characters}
    mappings = []
    for entry in native.characters:
        source = by_code.get(entry.code)
        if source is None:
            raise ValueError(f"QBF replacement is missing native character U+{entry.code:04X}")
        mappings.append({"code": entry.code, "native_glyph": entry.glyph_index,
                         "coverage_glyph": source.glyph_index,
                         "native_metrics_hex": entry.metrics.hex()})
    # A native glyph pointer can represent several character codes. The future
    # blit observer must not guess between different HD bitmaps for that pointer.
    pointer_map: dict[int, bytes] = {}
    for mapping in mappings:
        coverage = replacement.glyph_coverage(mapping["coverage_glyph"])
        previous = pointer_map.setdefault(mapping["native_glyph"], coverage)
        if previous != coverage:
            raise ValueError("QBF native glyph alias maps to conflicting HD coverage")
    return {"native_sha256": native.sha256, "coverage_sha256": replacement.sha256,
            "logical_cell": [native.width, native.height],
            "coverage_cell": [replacement.width, replacement.height],
            "density": density, "fallback_code": native.fallback_code,
            "characters": mappings,
            "replacement_metric_differences": sum(
                entry.metrics != by_code[entry.code].metrics for entry in native.characters)}
