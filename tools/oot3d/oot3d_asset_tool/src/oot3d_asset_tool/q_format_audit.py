from __future__ import annotations

import json
from collections import Counter, defaultdict
from pathlib import Path

from .actor_inventory import format_magic, sorted_nested_counter
from .binary import ParseError
from .media_asset_audit import (
    CATEGORY_BY_EXTENSION,
    read_prefix,
    size_summary,
    u16le,
    u32le,
)
from .romfs_inventory import sorted_counter

Q_FORMAT_EXTENSIONS = (
    ".qan",
    ".qcl",
    ".qly",
    ".qsp",
    ".qbf",
    ".qbr",
    ".qhm",
)

EXPECTED_ROLES_BY_GROUP_KIND = {
    "message_system": (
        "animation",
        "color_table",
        "eu_ltn16_font",
        "layout",
        "sprite",
        "sys8_font",
    ),
    "screen_layout_set": (
        "animation",
        "bottom_layout",
        "sprite",
        "top_layout",
    ),
    "metadata_singleton": ("metadata",),
}


def audit_q_format_assets(
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
    qbf_font_records: list[dict[str, object]] = []
    issues: list[dict[str, object]] = []
    grouped_paths: dict[str, dict[str, list[str]]] = defaultdict(lambda: defaultdict(list))
    group_metadata: dict[str, dict[str, object]] = {}
    extension_counts: Counter[str] = Counter()
    category_counts: Counter[str] = Counter()
    top_level_counts: Counter[str] = Counter()
    parent_dir_counts: Counter[str] = Counter()
    role_counts: Counter[str] = Counter()
    language_counts: Counter[str] = Counter()
    screen_counts: Counter[str] = Counter()
    group_kind_counts: Counter[str] = Counter()
    size_by_extension: dict[str, list[int]] = {ext: [] for ext in Q_FORMAT_EXTENSIONS}
    magic4_counts: dict[str, Counter[str]] = {ext: Counter() for ext in Q_FORMAT_EXTENSIONS}
    signature16_counts: dict[str, Counter[str]] = {
        ext: Counter() for ext in Q_FORMAT_EXTENSIONS
    }

    for path in sorted(romfs_root.rglob("*")):
        if not path.is_file():
            continue
        extension = path.suffix.lower()
        if extension not in Q_FORMAT_EXTENSIONS:
            continue

        rel_path = path.relative_to(romfs_root)
        rel = rel_path.as_posix()
        size = path.stat().st_size
        head = read_prefix(path, 64)
        magic4 = format_magic(head[:4])
        signature16 = head[:16].hex(" ")
        category = CATEGORY_BY_EXTENSION[extension]
        classification = classify_q_asset(rel_path)
        metadata = q_format_metadata(extension, head)

        extension_counts[extension] += 1
        category_counts[category] += 1
        top_level_counts[rel_path.parts[0] if rel_path.parts else "<root>"] += 1
        parent_dir_counts[rel_path.parent.as_posix() if rel_path.parent.parts else "<root>"] += 1
        role_counts[str(classification["role"])] += 1
        if classification["language"] is not None:
            language_counts[str(classification["language"])] += 1
        if classification["screen"] is not None:
            screen_counts[str(classification["screen"])] += 1
        size_by_extension[extension].append(size)
        magic4_counts[extension][magic4] += 1
        signature16_counts[extension][signature16] += 1

        group_id = str(classification["group_id"])
        role = str(classification["role"])
        grouped_paths[group_id][role].append(rel)
        group_metadata.setdefault(
            group_id,
            {
                "group_id": group_id,
                "group_kind": classification["group_kind"],
                "language": classification["language"],
                "screen": classification["screen"],
            },
        )

        if classification["group_kind"] == "unknown":
            issues.append(
                {
                    "type": "unknown_q_format_path",
                    "path": rel,
                    "extension": extension,
                }
            )
        if extension == ".qbf" and head[:4] != b"QBF1":
            issues.append(
                {
                    "type": "qbf_bad_magic",
                    "path": rel,
                    "magic4": magic4,
                }
            )

        record = {
            "path": rel,
            "extension": extension,
            "category": category,
            "size": size,
            "top_level": rel_path.parts[0] if rel_path.parts else "<root>",
            "parent_dir": rel_path.parent.as_posix() if rel_path.parent.parts else "<root>",
            "magic4": magic4,
            "signature16": signature16,
            "classification": classification,
            "metadata": metadata,
        }
        if extension == ".qbf":
            qbf_font_records.append(
                {
                    "path": rel,
                    "size": size,
                    **metadata,
                }
            )
        if include_records:
            records.append(record)
        if len(sample_records) < sample_limit:
            sample_records.append(record)

    group_records = build_group_records(grouped_paths, group_metadata, issues)
    group_completion_counts = Counter(
        "complete" if group["complete"] else "incomplete" for group in group_records
    )
    screen_set_counts = Counter(
        "complete" if group["complete"] else "incomplete"
        for group in group_records
        if group["group_kind"] == "screen_layout_set"
    )
    group_kind_counts.update(str(group["group_kind"]) for group in group_records)

    audit: dict[str, object] = {
        "format": "oot3d_q_format_asset_audit_v1",
        "romfs_root": str(romfs_root),
        "file_count": sum(extension_counts.values()),
        "total_size": sum(sum(values) for values in size_by_extension.values()),
        "extension_counts": sorted_counter(extension_counts),
        "category_counts": sorted_counter(category_counts),
        "top_level_counts": sorted_counter(top_level_counts),
        "parent_dir_counts": sorted_counter(parent_dir_counts),
        "role_counts": sorted_counter(role_counts),
        "language_counts": sorted_counter(language_counts),
        "screen_counts": sorted_counter(screen_counts),
        "group_kind_counts": sorted_counter(group_kind_counts),
        "group_completion_counts": sorted_counter(group_completion_counts),
        "screen_layout_set_completion_counts": sorted_counter(screen_set_counts),
        "size_summary_by_extension": {
            ext: size_summary(size_by_extension[ext])
            for ext in Q_FORMAT_EXTENSIONS
            if extension_counts[ext]
        },
        "magic4_counts": sorted_nested_counter(magic4_counts),
        "signature16_counts": sorted_nested_counter(signature16_counts),
        "qbf_font_records": sorted(qbf_font_records, key=lambda item: str(item["path"])),
        "groups": group_records,
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


def classify_q_asset(rel_path: Path) -> dict[str, object]:
    rel = rel_path.as_posix()
    parts = rel_path.parts
    extension = rel_path.suffix.lower()
    name = rel_path.name
    stem = rel_path.stem

    if rel == "message/anim.qan":
        return q_classification("message/system", "message_system", "animation", "message")
    if rel == "message/color.qcl":
        return q_classification("message/system", "message_system", "color_table", "message")
    if rel == "message/layout.qly":
        return q_classification("message/system", "message_system", "layout", "message")
    if rel == "message/sprite.qsp":
        return q_classification("message/system", "message_system", "sprite", "message")
    if rel == "message/sys8.qbf":
        return q_classification("message/system", "message_system", "sys8_font", "message")
    if rel == "message/eu/ltn16.qbf":
        return q_classification(
            "message/system",
            "message_system",
            "eu_ltn16_font",
            "message",
            region="eu",
        )
    if rel == "misc/bossRush.qbr":
        return q_classification("misc/bossRush", "metadata_singleton", "metadata", "bossRush")
    if rel == "misc/hint/list.qhm":
        return q_classification("misc/hint/list", "metadata_singleton", "metadata", "hint")

    if parts and parts[0] == "misc":
        if name in {
            "ending_top.qly",
            "ending_bottom.qly",
            "ending.qan",
            "ending.qsp",
        }:
            return q_classification(
                "misc/ending",
                "screen_layout_set",
                screen_layout_role("ending", stem, extension),
                "ending",
            )
        if len(parts) == 4 and parts[1] == "eu":
            language = parts[2]
            screen = localized_screen_name(stem)
            role = screen_layout_role(screen, stem, extension) if screen else "unknown"
            return q_classification(
                f"misc/eu/{language}/{screen}" if screen else f"misc/eu/{language}/unknown",
                "screen_layout_set" if role != "unknown" else "unknown",
                role,
                screen,
                region="eu",
                language=language,
            )

    return q_classification(rel, "unknown", "unknown", None)


def q_classification(
    group_id: str,
    group_kind: str,
    role: str,
    screen: str | None,
    *,
    region: str | None = None,
    language: str | None = None,
) -> dict[str, object]:
    return {
        "group_id": group_id,
        "group_kind": group_kind,
        "role": role,
        "region": region,
        "language": language,
        "screen": screen,
    }


def localized_screen_name(stem: str) -> str | None:
    for suffix in ("_top", "_bottom"):
        if stem.endswith(suffix):
            return stem[: -len(suffix)]
    if stem:
        return stem
    return None


def screen_layout_role(screen: str | None, stem: str, extension: str) -> str:
    if not screen:
        return "unknown"
    if extension == ".qan" and stem == screen:
        return "animation"
    if extension == ".qsp" and stem == screen:
        return "sprite"
    if extension == ".qly" and stem == f"{screen}_top":
        return "top_layout"
    if extension == ".qly" and stem == f"{screen}_bottom":
        return "bottom_layout"
    return "unknown"


def q_format_metadata(extension: str, head: bytes) -> dict[str, object]:
    metadata: dict[str, object] = {
        "header_words_le": [u32le(head, offset) for offset in range(0, 32, 4)],
        "nonzero_prefix_bytes": sum(1 for byte in head if byte != 0),
    }
    if extension == ".qbf":
        metadata.update(
            {
                "magic": head[:4].decode("ascii", errors="replace"),
                "texture_width_candidate_le": u16le(head, 4),
                "texture_height_candidate_le": u16le(head, 6),
                "glyph_count_candidate_le": u16le(head, 8),
                "cell_width_candidate": head[12] if len(head) > 12 else None,
                "cell_height_candidate": head[13] if len(head) > 13 else None,
                "tile_width_candidate": head[14] if len(head) > 14 else None,
                "tile_height_candidate": head[15] if len(head) > 15 else None,
            }
        )
    return metadata


def build_group_records(
    grouped_paths: dict[str, dict[str, list[str]]],
    group_metadata: dict[str, dict[str, object]],
    issues: list[dict[str, object]],
) -> list[dict[str, object]]:
    groups: list[dict[str, object]] = []
    for group_id in sorted(grouped_paths):
        metadata = group_metadata[group_id]
        group_kind = str(metadata["group_kind"])
        expected_roles = EXPECTED_ROLES_BY_GROUP_KIND.get(group_kind, ())
        roles = {
            role: sorted(paths)
            for role, paths in sorted(grouped_paths[group_id].items())
        }
        missing_roles = [role for role in expected_roles if role not in roles]
        duplicate_roles = [
            role for role, paths in roles.items() if len(paths) > 1 and role in expected_roles
        ]
        extra_roles = [role for role in roles if expected_roles and role not in expected_roles]
        complete = not missing_roles and not duplicate_roles and not extra_roles and group_kind != "unknown"
        group = {
            **metadata,
            "expected_roles": list(expected_roles),
            "roles": roles,
            "file_count": sum(len(paths) for paths in roles.values()),
            "missing_roles": missing_roles,
            "duplicate_roles": duplicate_roles,
            "extra_roles": extra_roles,
            "complete": complete,
        }
        if missing_roles:
            issues.append(
                {
                    "type": "missing_q_format_group_roles",
                    "group_id": group_id,
                    "missing_roles": missing_roles,
                }
            )
        if duplicate_roles:
            issues.append(
                {
                    "type": "duplicate_q_format_group_roles",
                    "group_id": group_id,
                    "duplicate_roles": duplicate_roles,
                }
            )
        if extra_roles:
            issues.append(
                {
                    "type": "extra_q_format_group_roles",
                    "group_id": group_id,
                    "extra_roles": extra_roles,
                }
            )
        groups.append(group)
    return groups
