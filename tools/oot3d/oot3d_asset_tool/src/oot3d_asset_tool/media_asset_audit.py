from __future__ import annotations

import json
import re
from collections import Counter
from pathlib import Path

from .actor_inventory import format_magic, sorted_nested_counter
from .binary import ParseError
from .romfs_inventory import sorted_counter

MEDIA_EXTENSIONS = (
    ".moflex",
    ".bcsar",
    ".bcstm",
    ".qan",
    ".qcl",
    ".qly",
    ".qsp",
    ".qbf",
    ".qbr",
    ".qhm",
    ".ctxb",
)

CATEGORY_BY_EXTENSION = {
    ".moflex": "moflex_movie",
    ".bcsar": "audio_archive",
    ".bcstm": "audio_stream",
    ".qan": "layout_animation",
    ".qcl": "layout_color",
    ".qly": "layout",
    ".qsp": "sprite",
    ".qbf": "bitmap_font",
    ".qbr": "boss_rush_metadata",
    ".qhm": "hint_metadata",
    ".ctxb": "external_texture",
}

HINT_MOVIE_RE = re.compile(r"^hint(\d+)\.moflex$", re.IGNORECASE)


def audit_media_assets(
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
    extension_counts: Counter[str] = Counter()
    category_counts: Counter[str] = Counter()
    top_level_counts: Counter[str] = Counter()
    parent_dir_counts: Counter[str] = Counter()
    size_by_extension: dict[str, list[int]] = {ext: [] for ext in MEDIA_EXTENSIONS}
    magic4_counts: dict[str, Counter[str]] = {ext: Counter() for ext in MEDIA_EXTENSIONS}
    signature16_counts: dict[str, Counter[str]] = {ext: Counter() for ext in MEDIA_EXTENSIONS}
    moflex_dimension_candidate_counts: Counter[str] = Counter()
    hint_movie_numbers: list[int] = []

    for path in sorted(romfs_root.rglob("*")):
        if not path.is_file():
            continue
        extension = path.suffix.lower()
        if extension not in CATEGORY_BY_EXTENSION:
            continue

        rel_path = path.relative_to(romfs_root)
        rel = rel_path.as_posix()
        size = path.stat().st_size
        head = read_prefix(path, 64)
        magic4 = format_magic(head[:4])
        signature16 = head[:16].hex(" ")
        category = CATEGORY_BY_EXTENSION[extension]

        extension_counts[extension] += 1
        category_counts[category] += 1
        top_level_counts[rel_path.parts[0] if rel_path.parts else "<root>"] += 1
        parent_dir_counts[rel_path.parent.as_posix() if rel_path.parent.parts else "<root>"] += 1
        size_by_extension[extension].append(size)
        magic4_counts[extension][magic4] += 1
        signature16_counts[extension][signature16] += 1

        metadata: dict[str, object] = {}
        if extension == ".moflex":
            width_candidate = u16be(head, 0x16)
            height_candidate = u16be(head, 0x18)
            if width_candidate is not None and height_candidate is not None:
                moflex_dimension_candidate_counts[f"{width_candidate}x{height_candidate}"] += 1
            match = HINT_MOVIE_RE.match(path.name)
            if match:
                hint_movie_numbers.append(int(match.group(1)))
            metadata = {
                "header_word_0c_be": u32be(head, 0x0C),
                "header_word_10_be": u32be(head, 0x10),
                "width_candidate": width_candidate,
                "height_candidate": height_candidate,
                "hint_movie_number": int(match.group(1)) if match else None,
            }
        elif extension in {".bcsar", ".bcstm"}:
            metadata = {
                "bom": head[4:6].hex(),
                "header_size_candidate_be": u16be(head, 6),
                "section_count_candidate_be": u16be(head, 12),
                "declared_size_candidate_le": u32le(head, 0x10),
            }
        elif extension == ".qbf":
            metadata = {
                "magic": head[:4].decode("ascii", errors="replace"),
                "glyph_count_candidate_le": u16le(head, 8),
                "tile_width_candidate": head[12] if len(head) > 12 else None,
                "tile_height_candidate": head[13] if len(head) > 13 else None,
            }

        record = {
            "path": rel,
            "extension": extension,
            "category": category,
            "size": size,
            "magic4": magic4,
            "signature16": signature16,
            "metadata": metadata,
        }
        if include_records:
            records.append(record)
        if len(sample_records) < sample_limit:
            sample_records.append(record)

    audit: dict[str, object] = {
        "format": "oot3d_media_asset_audit_v1",
        "romfs_root": str(romfs_root),
        "file_count": sum(extension_counts.values()),
        "total_size": sum(sum(values) for values in size_by_extension.values()),
        "extension_counts": sorted_counter(extension_counts),
        "category_counts": sorted_counter(category_counts),
        "top_level_counts": sorted_counter(top_level_counts),
        "parent_dir_counts": sorted_counter(parent_dir_counts),
        "size_summary_by_extension": {
            ext: size_summary(size_by_extension[ext])
            for ext in MEDIA_EXTENSIONS
            if extension_counts[ext]
        },
        "magic4_counts": sorted_nested_counter(magic4_counts),
        "signature16_counts": sorted_nested_counter(signature16_counts),
        "moflex_dimension_candidate_counts": sorted_counter(
            moflex_dimension_candidate_counts
        ),
        "moflex_hint_numbering": hint_numbering_summary(hint_movie_numbers),
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


def read_prefix(path: Path, size: int) -> bytes:
    with path.open("rb") as file:
        return file.read(size)


def u16be(data: bytes, offset: int) -> int | None:
    if len(data) < offset + 2:
        return None
    return int.from_bytes(data[offset : offset + 2], "big")


def u16le(data: bytes, offset: int) -> int | None:
    if len(data) < offset + 2:
        return None
    return int.from_bytes(data[offset : offset + 2], "little")


def u32be(data: bytes, offset: int) -> int | None:
    if len(data) < offset + 4:
        return None
    return int.from_bytes(data[offset : offset + 4], "big")


def u32le(data: bytes, offset: int) -> int | None:
    if len(data) < offset + 4:
        return None
    return int.from_bytes(data[offset : offset + 4], "little")


def size_summary(values: list[int]) -> dict[str, int]:
    if not values:
        return {
            "count": 0,
            "min": 0,
            "max": 0,
            "total": 0,
            "unique_size_count": 0,
        }
    return {
        "count": len(values),
        "min": min(values),
        "max": max(values),
        "total": sum(values),
        "unique_size_count": len(set(values)),
    }


def hint_numbering_summary(numbers: list[int]) -> dict[str, object]:
    if not numbers:
        return {
            "count": 0,
            "unique_count": 0,
            "min": None,
            "max": None,
            "missing_count": 0,
            "missing_numbers": [],
        }
    unique = sorted(set(numbers))
    all_numbers = set(range(unique[0], unique[-1] + 1))
    missing = sorted(all_numbers - set(unique))
    return {
        "count": len(numbers),
        "unique_count": len(unique),
        "min": unique[0],
        "max": unique[-1],
        "missing_count": len(missing),
        "missing_numbers": missing,
    }
