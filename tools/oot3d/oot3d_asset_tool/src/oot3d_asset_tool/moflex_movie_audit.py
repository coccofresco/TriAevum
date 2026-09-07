from __future__ import annotations

import json
import re
from collections import Counter
from pathlib import Path

from .actor_inventory import format_magic
from .binary import ParseError
from .media_asset_audit import CATEGORY_BY_EXTENSION, hint_numbering_summary, size_summary
from .romfs_inventory import sorted_counter

HINT_MOVIE_RE = re.compile(r"^hint(\d+)\.moflex$", re.IGNORECASE)

EXPECTED_MOFLEX_MAGIC = b"\x4c\x32\xaa\xab"


def audit_moflex_movies(
    romfs_root: Path,
    output_path: Path | None = None,
    *,
    sample_limit: int = 100,
    include_records: bool = True,
) -> dict[str, object]:
    if not romfs_root.is_dir():
        raise ParseError(f"{romfs_root}: expected an extracted OOT3D RomFS directory")

    records: list[dict[str, object]] = []
    sample_records: list[dict[str, object]] = []
    issues: list[dict[str, object]] = []
    size_values: list[int] = []
    hint_movie_numbers: list[int] = []
    top_level_counts: Counter[str] = Counter()
    parent_dir_counts: Counter[str] = Counter()
    magic4_counts: Counter[str] = Counter()
    signature16_counts: Counter[str] = Counter()
    dimension_candidate_counts: Counter[str] = Counter()
    header_field_counts: dict[str, Counter[str]] = {
        "u32_04": Counter(),
        "u32_08": Counter(),
        "u32_0c": Counter(),
        "u16_10": Counter(),
        "u16_12": Counter(),
        "u16_14": Counter(),
        "u16_1a": Counter(),
        "u32_1c": Counter(),
        "u16_20": Counter(),
        "u16_22": Counter(),
    }
    timing_profile_counts: Counter[str] = Counter()
    size_class_counts: Counter[str] = Counter()

    for path in sorted(romfs_root.rglob("*.moflex")):
        if not path.is_file():
            continue
        rel_path = path.relative_to(romfs_root)
        rel = rel_path.as_posix()
        data = path.read_bytes()
        size = len(data)
        header = parse_moflex_header(data)
        match = HINT_MOVIE_RE.match(path.name)
        hint_movie_number = int(match.group(1)) if match else None
        if hint_movie_number is not None:
            hint_movie_numbers.append(hint_movie_number)

        if data[:4] != EXPECTED_MOFLEX_MAGIC:
            issues.append(
                {
                    "type": "moflex_bad_magic",
                    "path": rel,
                    "magic4": header["magic4"],
                }
            )
        if rel_path.parent.as_posix() != "misc/hint/movie":
            issues.append(
                {
                    "type": "moflex_unexpected_parent_dir",
                    "path": rel,
                    "parent_dir": rel_path.parent.as_posix(),
                }
            )
        if hint_movie_number is None:
            issues.append(
                {
                    "type": "moflex_unnumbered_hint_movie",
                    "path": rel,
                }
            )

        size_values.append(size)
        top_level_counts[rel_path.parts[0] if rel_path.parts else "<root>"] += 1
        parent_dir_counts[rel_path.parent.as_posix() if rel_path.parent.parts else "<root>"] += 1
        magic4_counts[str(header["magic4"])] += 1
        signature16_counts[str(header["signature16"])] += 1
        dimension_candidate_counts[
            f"{header['width_candidate']}x{header['height_candidate']}"
        ] += 1
        for field in header_field_counts:
            header_field_counts[field][str(header[field])] += 1
        timing_profile_counts[
            f"{header['timing_numerator_candidate']}/{header['timing_denominator_candidate']}"
        ] += 1
        size_class_counts[moflex_size_class(size)] += 1

        record = {
            "path": rel,
            "extension": ".moflex",
            "category": CATEGORY_BY_EXTENSION[".moflex"],
            "size": size,
            "size_class": moflex_size_class(size),
            "top_level": rel_path.parts[0] if rel_path.parts else "<root>",
            "parent_dir": rel_path.parent.as_posix() if rel_path.parent.parts else "<root>",
            "hint_movie_number": hint_movie_number,
            "header": header,
        }
        if include_records:
            records.append(record)
        if len(sample_records) < sample_limit:
            sample_records.append(record)

    duplicate_numbers = sorted(
        number for number, count in Counter(hint_movie_numbers).items() if count > 1
    )
    for number in duplicate_numbers:
        issues.append(
            {
                "type": "duplicate_hint_movie_number",
                "hint_movie_number": number,
            }
        )

    audit: dict[str, object] = {
        "format": "oot3d_moflex_movie_audit_v1",
        "romfs_root": str(romfs_root),
        "file_count": len(size_values),
        "total_size": sum(size_values),
        "extension_counts": {".moflex": len(size_values)} if size_values else {},
        "category_counts": {"moflex_movie": len(size_values)} if size_values else {},
        "top_level_counts": sorted_counter(top_level_counts),
        "parent_dir_counts": sorted_counter(parent_dir_counts),
        "size_summary": size_summary(size_values),
        "size_class_counts": sorted_counter(size_class_counts),
        "magic4_counts": sorted_counter(magic4_counts),
        "signature16_counts": sorted_counter(signature16_counts),
        "dimension_candidate_counts": sorted_counter(dimension_candidate_counts),
        "header_field_counts": {
            field: sorted_counter(counter)
            for field, counter in sorted(header_field_counts.items())
        },
        "timing_profile_counts": sorted_counter(timing_profile_counts),
        "hint_movie_numbering": hint_numbering_summary(hint_movie_numbers),
        "duplicate_hint_movie_numbers": duplicate_numbers,
        "issue_count": len(issues),
        "issues": issues,
        "sample_records": sample_records,
        "records": records if include_records else [],
    }

    if output_path is not None:
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(
            json.dumps(audit, indent=2) + "\n",
            encoding="utf-8",
            newline="\n",
        )
    return audit


def parse_moflex_header(data: bytes) -> dict[str, object]:
    return {
        "magic4": format_magic(data[:4]),
        "signature16": data[:16].hex(" "),
        "u32_04": u32be(data, 0x04),
        "u32_08": u32be(data, 0x08),
        "u32_0c": u32be(data, 0x0C),
        "u16_10": u16be(data, 0x10),
        "u16_12": u16be(data, 0x12),
        "u16_14": u16be(data, 0x14),
        "width_candidate": u16be(data, 0x16),
        "height_candidate": u16be(data, 0x18),
        "u16_1a": u16be(data, 0x1A),
        "u32_1c": u32be(data, 0x1C),
        "u16_20": u16be(data, 0x20),
        "u16_22": u16be(data, 0x22),
        "u32_24": u32be(data, 0x24),
        "timing_numerator_candidate": u16be(data, 0x12),
        "timing_denominator_candidate": u16be(data, 0x14),
    }


def u16be(data: bytes, offset: int) -> int | None:
    if len(data) < offset + 2:
        return None
    return int.from_bytes(data[offset : offset + 2], "big")


def u32be(data: bytes, offset: int) -> int | None:
    if len(data) < offset + 4:
        return None
    return int.from_bytes(data[offset : offset + 4], "big")


def moflex_size_class(size: int) -> str:
    if size >= 1_500_000:
        return "large"
    if size >= 1_100_000:
        return "medium"
    return "small"
