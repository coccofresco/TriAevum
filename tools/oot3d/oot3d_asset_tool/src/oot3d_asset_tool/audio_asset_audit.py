from __future__ import annotations

import json
from collections import Counter
from pathlib import Path

from .actor_inventory import format_magic, sorted_nested_counter
from .binary import ParseError
from .media_asset_audit import CATEGORY_BY_EXTENSION, size_summary
from .romfs_inventory import sorted_counter

AUDIO_EXTENSIONS = (".bcsar", ".bcstm")

EXPECTED_MAGIC_BY_EXTENSION = {
    ".bcsar": "CSAR",
    ".bcstm": "CSTM",
}

EXPECTED_SECTION_MAGIC = {
    "CSAR": {
        0x2000: "STRG",
        0x2001: "INFO",
        0x2002: "FILE",
    },
    "CSTM": {
        0x4000: "INFO",
        0x4001: "SEEK",
        0x4002: "DATA",
    },
}


def audit_audio_assets(
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
    extension_counts: Counter[str] = Counter()
    category_counts: Counter[str] = Counter()
    top_level_counts: Counter[str] = Counter()
    parent_dir_counts: Counter[str] = Counter()
    size_by_extension: dict[str, list[int]] = {ext: [] for ext in AUDIO_EXTENSIONS}
    magic4_counts: dict[str, Counter[str]] = {ext: Counter() for ext in AUDIO_EXTENSIONS}
    bom_counts: Counter[str] = Counter()
    byte_order_counts: Counter[str] = Counter()
    version_counts: Counter[str] = Counter()
    header_size_counts: Counter[str] = Counter()
    section_count_counts: Counter[str] = Counter()
    section_type_counts: Counter[str] = Counter()
    section_magic_counts: Counter[str] = Counter()
    section_layout_counts: Counter[str] = Counter()
    section_size_by_type: dict[str, list[int]] = {}
    declared_size_match_count = 0
    section_declared_size_match_count = 0
    section_record_count = 0

    for path in sorted(romfs_root.rglob("*")):
        if not path.is_file():
            continue
        extension = path.suffix.lower()
        if extension not in AUDIO_EXTENSIONS:
            continue

        rel_path = path.relative_to(romfs_root)
        rel = rel_path.as_posix()
        data = path.read_bytes()
        size = len(data)
        parsed = parse_audio_container(data, rel, extension)
        record_issues = parsed.pop("issues")
        issues.extend(record_issues)

        extension_counts[extension] += 1
        category_counts[CATEGORY_BY_EXTENSION[extension]] += 1
        top_level_counts[rel_path.parts[0] if rel_path.parts else "<root>"] += 1
        parent_dir_counts[rel_path.parent.as_posix() if rel_path.parent.parts else "<root>"] += 1
        size_by_extension[extension].append(size)
        magic4_counts[extension][parsed["magic4"]] += 1
        bom_counts[str(parsed["bom_hex"])] += 1
        byte_order_counts[str(parsed["byte_order"])] += 1
        version_counts[str(parsed["version"])] += 1
        header_size_counts[str(parsed["header_size"])] += 1
        section_count_counts[str(parsed["section_count"])] += 1
        if parsed["declared_size_matches_file_size"]:
            declared_size_match_count += 1

        layout_parts: list[str] = []
        for section in parsed["sections"]:
            section_record_count += 1
            section_type = str(section["type"])
            section_magic = str(section["magic"])
            section_type_counts[section_type] += 1
            section_magic_counts[section_magic] += 1
            section_size_by_type.setdefault(section_type, []).append(int(section["size"]))
            if section["declared_size_matches_table_size"]:
                section_declared_size_match_count += 1
            layout_parts.append(f"{section_type}:{section_magic}")
        section_layout_counts[",".join(layout_parts)] += 1

        record = {
            "path": rel,
            "extension": extension,
            "category": CATEGORY_BY_EXTENSION[extension],
            "size": size,
            "top_level": rel_path.parts[0] if rel_path.parts else "<root>",
            "parent_dir": rel_path.parent.as_posix() if rel_path.parent.parts else "<root>",
            **parsed,
            "issues": record_issues,
        }
        if include_records:
            records.append(record)
        if len(sample_records) < sample_limit:
            sample_records.append(record)

    audit: dict[str, object] = {
        "format": "oot3d_audio_asset_audit_v1",
        "romfs_root": str(romfs_root),
        "file_count": sum(extension_counts.values()),
        "total_size": sum(sum(values) for values in size_by_extension.values()),
        "extension_counts": sorted_counter(extension_counts),
        "category_counts": sorted_counter(category_counts),
        "top_level_counts": sorted_counter(top_level_counts),
        "parent_dir_counts": sorted_counter(parent_dir_counts),
        "size_summary_by_extension": {
            ext: size_summary(size_by_extension[ext])
            for ext in AUDIO_EXTENSIONS
            if extension_counts[ext]
        },
        "magic4_counts": sorted_nested_counter(magic4_counts),
        "bom_counts": sorted_counter(bom_counts),
        "byte_order_counts": sorted_counter(byte_order_counts),
        "version_counts": sorted_counter(version_counts),
        "header_size_counts": sorted_counter(header_size_counts),
        "section_count_counts": sorted_counter(section_count_counts),
        "section_type_counts": sorted_counter(section_type_counts),
        "section_magic_counts": sorted_counter(section_magic_counts),
        "section_layout_counts": sorted_counter(section_layout_counts),
        "section_size_summary_by_type": {
            section_type: size_summary(section_size_by_type[section_type])
            for section_type in sorted(section_size_by_type)
        },
        "declared_size_match_count": declared_size_match_count,
        "section_record_count": section_record_count,
        "section_declared_size_match_count": section_declared_size_match_count,
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


def parse_audio_container(data: bytes, rel: str, extension: str) -> dict[str, object]:
    issues: list[dict[str, object]] = []
    if len(data) < 0x14:
        return {
            "magic": "",
            "magic4": format_magic(data[:4]),
            "bom_hex": data[4:6].hex(),
            "byte_order": "unknown",
            "header_size": None,
            "version": None,
            "declared_size": None,
            "declared_size_matches_file_size": False,
            "section_count": 0,
            "reserved": None,
            "sections": [],
            "issues": [
                {
                    "type": "short_audio_header",
                    "path": rel,
                    "size": len(data),
                }
            ],
        }

    magic = data[:4].decode("ascii", errors="replace")
    bom = data[4:6]
    byte_order = audio_byte_order(bom)
    expected_magic = EXPECTED_MAGIC_BY_EXTENSION[extension]
    if magic != expected_magic:
        issues.append(
            {
                "type": "unexpected_audio_magic",
                "path": rel,
                "expected": expected_magic,
                "actual": magic,
            }
        )
    if byte_order == "unknown":
        issues.append(
            {
                "type": "unknown_audio_bom",
                "path": rel,
                "bom_hex": bom.hex(),
            }
        )

    endian = "<" if byte_order != "big" else ">"
    header_size = read_u16(data, 0x06, endian)
    version_raw = read_u32(data, 0x08, endian)
    declared_size = read_u32(data, 0x0C, endian)
    section_count = read_u16(data, 0x10, endian) or 0
    reserved = read_u16(data, 0x12, endian)
    section_table_end = 0x14 + section_count * 12

    if header_size is None or header_size < section_table_end:
        issues.append(
            {
                "type": "audio_section_table_outside_header",
                "path": rel,
                "header_size": header_size,
                "section_table_end": section_table_end,
            }
        )
    if header_size is not None and header_size > len(data):
        issues.append(
            {
                "type": "audio_header_outside_file",
                "path": rel,
                "header_size": header_size,
                "file_size": len(data),
            }
        )
    if declared_size != len(data):
        issues.append(
            {
                "type": "audio_declared_size_mismatch",
                "path": rel,
                "declared_size": declared_size,
                "file_size": len(data),
            }
        )
    if reserved not in (0, None):
        issues.append(
            {
                "type": "audio_header_reserved_nonzero",
                "path": rel,
                "reserved": reserved,
            }
        )

    sections = []
    for index in range(section_count):
        base = 0x14 + index * 12
        if len(data) < base + 12:
            issues.append(
                {
                    "type": "audio_section_entry_truncated",
                    "path": rel,
                    "section_index": index,
                    "entry_offset": base,
                }
            )
            break
        section_type_id = read_u16(data, base, endian) or 0
        section_reserved = read_u16(data, base + 2, endian)
        section_offset = read_u32(data, base + 4, endian) or 0
        section_size = read_u32(data, base + 8, endian) or 0
        section_end = section_offset + section_size
        section_magic = ""
        section_declared_size = None
        if section_offset < len(data):
            section_magic = data[section_offset : section_offset + 4].decode(
                "ascii", errors="replace"
            )
            section_declared_size = read_u32(data, section_offset + 4, endian)

        expected_section_magic = EXPECTED_SECTION_MAGIC.get(magic, {}).get(section_type_id)
        in_bounds = section_offset <= len(data) and section_end <= len(data)
        declared_size_matches = section_declared_size == section_size

        if section_reserved not in (0, None):
            issues.append(
                {
                    "type": "audio_section_reserved_nonzero",
                    "path": rel,
                    "section_index": index,
                    "section_reserved": section_reserved,
                }
            )
        if not in_bounds:
            issues.append(
                {
                    "type": "audio_section_outside_file",
                    "path": rel,
                    "section_index": index,
                    "section_offset": section_offset,
                    "section_size": section_size,
                    "file_size": len(data),
                }
            )
        if expected_section_magic is not None and section_magic != expected_section_magic:
            issues.append(
                {
                    "type": "audio_section_magic_mismatch",
                    "path": rel,
                    "section_index": index,
                    "section_type": f"0x{section_type_id:04x}",
                    "expected": expected_section_magic,
                    "actual": section_magic,
                }
            )
        if not declared_size_matches:
            issues.append(
                {
                    "type": "audio_section_declared_size_mismatch",
                    "path": rel,
                    "section_index": index,
                    "section_type": f"0x{section_type_id:04x}",
                    "declared_size": section_declared_size,
                    "table_size": section_size,
                }
            )

        sections.append(
            {
                "index": index,
                "type": f"0x{section_type_id:04x}",
                "expected_magic": expected_section_magic,
                "magic": section_magic,
                "reserved": section_reserved,
                "offset": section_offset,
                "size": section_size,
                "end": section_end,
                "declared_size": section_declared_size,
                "declared_size_matches_table_size": declared_size_matches,
                "in_bounds": in_bounds,
            }
        )

    overlap_issues = audio_section_overlap_issues(rel, sections)
    issues.extend(overlap_issues)

    return {
        "magic": magic,
        "magic4": format_magic(data[:4]),
        "bom_hex": bom.hex(),
        "byte_order": byte_order,
        "header_size": header_size,
        "version": f"0x{version_raw:08x}" if version_raw is not None else None,
        "declared_size": declared_size,
        "declared_size_matches_file_size": declared_size == len(data),
        "section_count": section_count,
        "reserved": reserved,
        "sections": sections,
        "issues": issues,
    }


def audio_byte_order(bom: bytes) -> str:
    if bom == b"\xff\xfe":
        return "little"
    if bom == b"\xfe\xff":
        return "big"
    return "unknown"


def read_u16(data: bytes, offset: int, endian: str) -> int | None:
    if len(data) < offset + 2:
        return None
    return int.from_bytes(data[offset : offset + 2], "little" if endian == "<" else "big")


def read_u32(data: bytes, offset: int, endian: str) -> int | None:
    if len(data) < offset + 4:
        return None
    return int.from_bytes(data[offset : offset + 4], "little" if endian == "<" else "big")


def audio_section_overlap_issues(
    rel: str,
    sections: list[dict[str, object]],
) -> list[dict[str, object]]:
    issues: list[dict[str, object]] = []
    ordered = sorted(sections, key=lambda section: int(section["offset"]))
    previous_end = 0
    previous_index = None
    for section in ordered:
        offset = int(section["offset"])
        if previous_index is not None and offset < previous_end:
            issues.append(
                {
                    "type": "audio_section_overlap",
                    "path": rel,
                    "previous_section_index": previous_index,
                    "section_index": section["index"],
                    "previous_end": previous_end,
                    "section_offset": offset,
                }
            )
        previous_end = max(previous_end, int(section["end"]))
        previous_index = section["index"]
    return issues
