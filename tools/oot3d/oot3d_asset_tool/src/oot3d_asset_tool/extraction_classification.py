from __future__ import annotations

import json
from collections import Counter
from pathlib import Path

from .binary import ParseError


INTERPRETABLE_ROMFS_SUFFIXES = {
    ".bcstm": "streamed_audio_data",
    ".bcsar": "sound_archive_data",
    ".cmb": "model_data",
    ".ctxb": "texture_data",
    ".moflex": "movie_data",
    ".shbin": "shader_binary_data",
    ".zar": "asset_archive_data",
    ".zsi": "scene_room_cutscene_data",
}


def audit_extraction_classification(
    extraction_root: Path,
    output_path: Path | None = None,
    *,
    include_records: bool = True,
) -> dict[str, object]:
    if not extraction_root.is_dir():
        raise ParseError(f"{extraction_root}: expected an extracted OOT3D root directory")

    romfs_root = extraction_root / "romfs"
    exefs_root = extraction_root / "exefs"
    if not romfs_root.is_dir():
        raise ParseError(f"{romfs_root}: missing RomFS directory")
    if not exefs_root.is_dir():
        raise ParseError(f"{exefs_root}: missing ExeFS directory")

    romfs_files = sorted(path for path in romfs_root.rglob("*") if path.is_file())
    exefs_files = sorted(path for path in exefs_root.rglob("*") if path.is_file())

    romfs_extension_counts: Counter[str] = Counter()
    romfs_top_level_counts: Counter[str] = Counter()
    romfs_kind_counts: Counter[str] = Counter()
    romfs_total_bytes = 0
    romfs_records: list[dict[str, object]] = []

    for path in romfs_files:
        rel = path.relative_to(romfs_root)
        suffix = normalized_suffix(path)
        kind = romfs_kind_for_suffix(suffix)
        size = path.stat().st_size
        romfs_total_bytes += size
        romfs_extension_counts[suffix] += 1
        romfs_top_level_counts[top_level_key(rel)] += 1
        romfs_kind_counts[kind] += 1
        if include_records:
            romfs_records.append(
                {
                    "path": rel.as_posix(),
                    "top_level": top_level_key(rel),
                    "extension": suffix,
                    "size": size,
                    "classification": "interpret_or_convert_data",
                    "kind": kind,
                }
            )

    exefs_records: list[dict[str, object]] = []
    exefs_total_bytes = 0
    exefs_code_bytes = 0
    exefs_metadata_bytes = 0
    decompile_file_count = 0
    metadata_file_count = 0
    for path in exefs_files:
        rel_text = path.relative_to(exefs_root).as_posix()
        size = path.stat().st_size
        exefs_total_bytes += size
        if path.name.lower() == "code.bin":
            classification = "decompile_code"
            kind = "arm11_game_code_binary"
            decompile_file_count += 1
            exefs_code_bytes += size
        else:
            classification = "interpret_metadata_or_title_binary"
            kind = "exefs_metadata_or_title_asset"
            metadata_file_count += 1
            exefs_metadata_bytes += size
        exefs_records.append(
            {
                "path": rel_text,
                "size": size,
                "classification": classification,
                "kind": kind,
            }
        )

    report = {
        "format": "oot3d_extraction_classification_v1",
        "extraction_root": str(extraction_root),
        "summary": {
            "total_extracted_files": len(romfs_files) + len(exefs_files),
            "romfs_interpret_or_convert_file_count": len(romfs_files),
            "romfs_interpret_or_convert_bytes": romfs_total_bytes,
            "exefs_file_count": len(exefs_files),
            "exefs_total_bytes": exefs_total_bytes,
            "exefs_decompile_code_file_count": decompile_file_count,
            "exefs_decompile_code_bytes": exefs_code_bytes,
            "exefs_metadata_or_title_file_count": metadata_file_count,
            "exefs_metadata_or_title_bytes": exefs_metadata_bytes,
            "effective_gameplay_script_file_count": decompile_file_count,
            "classification_policy": (
                "RomFS files are data assets to interpret/convert; ExeFS code.bin is the compiled "
                "gameplay/actor runtime that must be decompiled for behavior parity."
            ),
        },
        "romfs": {
            "root": str(romfs_root),
            "file_count": len(romfs_files),
            "total_bytes": romfs_total_bytes,
            "top_level_file_counts": sorted_counter(romfs_top_level_counts),
            "extension_counts": sorted_counter(romfs_extension_counts),
            "kind_counts": sorted_counter(romfs_kind_counts),
            "records": romfs_records if include_records else [],
        },
        "exefs": {
            "root": str(exefs_root),
            "file_count": len(exefs_files),
            "total_bytes": exefs_total_bytes,
            "records": exefs_records,
        },
    }

    if output_path is not None:
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8", newline="\n")
    return report


def romfs_kind_for_suffix(suffix: str) -> str:
    if suffix.startswith(".q"):
        return "q_format_ui_or_message_data"
    return INTERPRETABLE_ROMFS_SUFFIXES.get(suffix, "other_data")


def normalized_suffix(path: Path) -> str:
    return path.suffix.lower() or "<none>"


def top_level_key(relative_path: Path) -> str:
    return relative_path.parts[0] if len(relative_path.parts) > 1 else "<root>"


def sorted_counter(counter: Counter[str]) -> dict[str, int]:
    return {key: counter[key] for key in sorted(counter)}
