#!/usr/bin/env python3
"""Build OOT3D-native cutscene payload decode tables.

The source cutscene table records scene command 0x17 references. This generator
lowers the native payload format consumed by OOT3D `Cutscene_ProcessCommands`:
asset envelope at command argument +0x10, `QDB ` magic, command count/end frame,
then native command records with the strides observed in code.bin.
"""

from __future__ import annotations

import csv
import json
import sys
from collections import Counter
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
ASSET_TOOL_SRC = ROOT.parent / "oot3d_asset_tool" / "src"
if ASSET_TOOL_SRC.is_dir():
    sys.path.insert(0, str(ASSET_TOOL_SRC))

from oot3d_asset_tool.zsi_cutscene_audit import audit_zsi_cutscene_metadata


DEFAULT_SOURCE_TABLE = ROOT / "analysis" / "scene_cutscene_source_table.json"
DEFAULT_OUT_JSON = ROOT / "analysis" / "scene_cutscene_native_decode_table.json"
DEFAULT_OUT_MD = ROOT / "analysis" / "scene_cutscene_native_decode_table.md"
DEFAULT_OUT_CUTSCENE_CSV = ROOT / "analysis" / "scene_cutscene_native_decode_table.csv"
DEFAULT_OUT_COMMAND_CSV = ROOT / "analysis" / "scene_cutscene_native_command_table.csv"
NO_OFFSET = 0xFFFFFFFF


def as_dict(value: Any) -> dict[str, Any]:
    return value if isinstance(value, dict) else {}


def as_list(value: Any) -> list[Any]:
    return value if isinstance(value, list) else []


def int_value(value: Any, default: int = 0) -> int:
    try:
        if value is None:
            return default
        return int(value)
    except (TypeError, ValueError):
        return default


def bool_value(value: Any) -> bool:
    return bool(value)


def hex_u32(value: Any) -> str:
    return f"0x{int_value(value) & 0xFFFFFFFF:08x}"


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as handle:
        json.dump(payload, handle, indent=2, sort_keys=True, allow_nan=False)
        handle.write("\n")


def write_csv(path: Path, rows: list[dict[str, Any]], fieldnames: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)


def audit_record_key(row: dict[str, Any]) -> tuple[str, int, int, int]:
    return (
        str(row.get("path", "")),
        int_value(row.get("setup_index")),
        int_value(row.get("command_offset")),
        int_value(row.get("argument")),
    )


def source_row_key(row: dict[str, Any]) -> tuple[str, int, int, int]:
    return (
        str(row.get("scene_path", "")),
        int_value(row.get("setup_index")),
        int_value(row.get("command_offset")),
        int_value(row.get("command_argument")),
    )


def sample_error_text(native_decode: dict[str, Any]) -> str:
    errors = as_list(native_decode.get("sample_errors"))
    if not errors:
        return ""
    return " | ".join(
        f"+0x{int_value(error.get('delta')):02x}@{hex_u32(error.get('offset'))}: {error.get('error', '')}"
        for error in errors[:5]
        if isinstance(error, dict)
    )


def build_tables(source_table_path: Path = DEFAULT_SOURCE_TABLE) -> dict[str, Any]:
    source_table = load_json(source_table_path)
    scene_root = Path(str(as_dict(source_table.get("summary")).get("scene_root", "")))
    if not scene_root.is_dir():
        raise FileNotFoundError(f"{scene_root}: scene root from source table not found")

    audit = audit_zsi_cutscene_metadata(scene_root, None, sample_limit=0, include_records=True)
    audit_by_key = {audit_record_key(as_dict(row)): as_dict(row) for row in as_list(audit.get("records"))}

    cutscene_rows: list[dict[str, Any]] = []
    native_command_rows: list[dict[str, Any]] = []
    missing_audit_bindings: list[dict[str, Any]] = []

    for source_row in as_list(source_table.get("cutscene_rows")):
        source_row = as_dict(source_row)
        source_index = int_value(source_row.get("cutscene_source_index"))
        audit_record = audit_by_key.get(source_row_key(source_row))
        if audit_record is None:
            missing_audit_bindings.append(
                {
                    "cutscene_source_index": source_index,
                    "scene_path": source_row.get("scene_path", ""),
                    "setup_index": int_value(source_row.get("setup_index")),
                    "command_argument_hex": source_row.get("command_argument_hex", ""),
                }
            )
            native_decode: dict[str, Any] = {
                "decoded": False,
                "sample_errors": [{"delta": 0, "offset": 0, "error": "audit binding missing"}],
            }
        else:
            native_decode = as_dict(audit_record.get("oot3d_native_decode"))

        decoded = bool_value(native_decode.get("decoded"))
        command_start = len(native_command_rows)
        if decoded:
            for command in as_list(native_decode.get("commands")):
                if not isinstance(command, dict):
                    continue
                native_command_rows.append(
                    {
                        "native_command_source_index": len(native_command_rows),
                        "cutscene_source_index": source_index,
                        "scene_path": source_row.get("scene_path", ""),
                        "setup_index": int_value(source_row.get("setup_index")),
                        "local_command_index": int_value(command.get("index")),
                        "command_offset": int_value(command.get("offset"), NO_OFFSET),
                        "command_offset_hex": hex_u32(command.get("offset", NO_OFFSET)),
                        "command_id": int_value(command.get("command_id")),
                        "command_id_hex": command.get("command_id_hex", ""),
                        "command_name": command.get("name", ""),
                        "category": command.get("category", ""),
                        "entry_count": int_value(command.get("entry_count"), 0),
                        "camera_point_count": int_value(command.get("camera_point_count"), 0),
                        "blob_size": int_value(command.get("blob_size"), 0),
                        "packed_stride": int_value(command.get("packed_stride"), 0),
                        "payload_size": int_value(command.get("payload_size"), 0),
                        "total_size": int_value(command.get("total_size"), 0),
                        "raw_prefix_hex": command.get("raw_prefix_hex", ""),
                    }
                )

        command_count = len(native_command_rows) - command_start
        cutscene_rows.append(
            {
                "cutscene_source_index": source_index,
                "scene_path": source_row.get("scene_path", ""),
                "scene_stem": source_row.get("scene_stem", ""),
                "scene_id": int_value(source_row.get("scene_id")),
                "scene_id_hex": source_row.get("scene_id_hex", ""),
                "setup_index": int_value(source_row.get("setup_index")),
                "setup_symbol": source_row.get("setup_symbol", ""),
                "payload_symbol": source_row.get("payload_symbol", ""),
                "command_argument": int_value(source_row.get("command_argument"), NO_OFFSET),
                "command_argument_hex": source_row.get("command_argument_hex", ""),
                "strict_decoded": bool_value(source_row.get("strict_decoded")),
                "native_decoded": decoded,
                "native_decode_status": "decoded" if decoded else "undecoded",
                "native_header_delta": int_value(native_decode.get("header_delta"), NO_OFFSET),
                "native_header_delta_hex": ""
                if not decoded
                else f"0x{int_value(native_decode.get('header_delta')):02x}",
                "native_header_offset": int_value(native_decode.get("header_offset"), NO_OFFSET),
                "native_header_offset_hex": ""
                if not decoded
                else hex_u32(native_decode.get("header_offset", NO_OFFSET)),
                "native_magic_hex": native_decode.get("magic_hex", ""),
                "native_version_or_flags_hex": native_decode.get("version_or_flags_hex", ""),
                "native_command_count": int_value(native_decode.get("command_count"), 0),
                "native_end_frame": int_value(native_decode.get("end_frame"), 0),
                "native_decoded_size": int_value(native_decode.get("decoded_size"), 0),
                "native_command_ref_start": command_start,
                "native_command_ref_count": command_count,
                "native_decode_error_sample": sample_error_text(native_decode),
            }
        )

    category_counts = Counter(str(row["category"]) for row in native_command_rows)
    command_id_counts = Counter(str(row["command_id_hex"]) for row in native_command_rows)
    spot04_rows = [row for row in cutscene_rows if row["scene_path"] == "spot04_info.zsi"]
    summary = {
        "format": "oot3d_scene_cutscene_native_decode_table_v1",
        "scene_root": str(scene_root),
        "source_table": str(source_table_path),
        "cutscene_source_row_count": len(cutscene_rows),
        "native_decoded_cutscene_count": sum(1 for row in cutscene_rows if row["native_decoded"]),
        "native_decode_error_count": sum(1 for row in cutscene_rows if not row["native_decoded"]),
        "native_command_row_count": len(native_command_rows),
        "strict_decoded_cutscene_count": sum(1 for row in cutscene_rows if row["strict_decoded"]),
        "spot04_cutscene_count": len(spot04_rows),
        "spot04_native_decoded_count": sum(1 for row in spot04_rows if row["native_decoded"]),
        "spot04_native_command_count": sum(
            int_value(row.get("native_command_ref_count")) for row in spot04_rows
        ),
        "native_command_category_counts": dict(sorted(category_counts.items())),
        "native_command_id_counts": dict(sorted(command_id_counts.items())),
        "missing_audit_binding_count": len(missing_audit_bindings),
        "code_bin_evidence": {
            "interpreter": "Cutscene_ProcessCommands at 0x002C5BA0",
            "header_layout": "QDB magic at effective header +0x00, command_count +0x08, end_frame +0x0C, command stream +0x10",
            "asset_envelope_delta": "0x10 for every decoded payload in this asset set",
        },
    }

    return {
        "summary": summary,
        "cutscene_rows": cutscene_rows,
        "native_command_rows": native_command_rows,
        "missing_audit_bindings": missing_audit_bindings,
    }


def md_cell(value: Any) -> str:
    return "" if value is None else str(value).replace("|", "\\|")


def write_markdown(path: Path, tables: dict[str, Any]) -> None:
    summary = as_dict(tables.get("summary"))
    cutscene_rows = [as_dict(row) for row in as_list(tables.get("cutscene_rows"))]
    spot04_rows = [row for row in cutscene_rows if row.get("scene_path") == "spot04_info.zsi"]
    lines = [
        "# OOT3D Scene Cutscene Native Decode Table",
        "",
        "This generated table decodes native scene command `0x17` payloads using the OOT3D `code.bin` interpreter layout from `Cutscene_ProcessCommands` at `0x002C5BA0`. It treats strict-N64-compatible rows as subset evidence only.",
        "",
        "## Summary",
        "",
        f"- Cutscene references: {summary.get('cutscene_source_row_count')}",
        f"- OOT3D-native decoded cutscenes: {summary.get('native_decoded_cutscene_count')}",
        f"- OOT3D-native decode errors: {summary.get('native_decode_error_count')}",
        f"- Native command rows: {summary.get('native_command_row_count')}",
        f"- Strict-N64-compatible decoded cutscenes: {summary.get('strict_decoded_cutscene_count')}",
        f"- `spot04_info.zsi`: {summary.get('spot04_native_decoded_count')}/{summary.get('spot04_cutscene_count')} native decoded, {summary.get('spot04_native_command_count')} commands",
        "",
        "## Native Layout",
        "",
        "- Scene command `0x17` points at an asset envelope.",
        "- The effective OOT3D header starts at envelope `+0x10` for every decoded payload in this asset set.",
        "- Header word `+0x00` is `0x51444220` (`QDB `), `+0x08` is command count, `+0x0C` is end frame, and commands start at `+0x10`.",
        "",
        "## Command Categories",
        "",
    ]
    for category, count in sorted(as_dict(summary.get("native_command_category_counts")).items()):
        lines.append(f"- `{category}`: {count}")

    lines.extend(
        [
            "",
            "## Undecoded Rows",
            "",
            "| index | scene | setup | offset | reason |",
            "| ---: | --- | ---: | ---: | --- |",
        ]
    )
    for row in cutscene_rows:
        if row.get("native_decoded"):
            continue
        lines.append(
            "| {cutscene_source_index} | {scene_path} | {setup_index} | {command_argument_hex} | {native_decode_error_sample} |".format(
                **{key: md_cell(value) for key, value in row.items()}
            )
        )

    lines.extend(
        [
            "",
            "## Kokiri / `spot04_info.zsi`",
            "",
            "| setup | offset | commands | end frame | decoded size | strict subset |",
            "| ---: | ---: | ---: | ---: | ---: | --- |",
        ]
    )
    for row in sorted(spot04_rows, key=lambda item: int_value(item.get("setup_index"))):
        lines.append(
            "| {setup_index} | {command_argument_hex} | {native_command_ref_count} | {native_end_frame} | {native_decoded_size} | {strict_decoded} |".format(
                **row
            )
        )

    lines.extend(
        [
            "",
            "## Outputs",
            "",
            "- `analysis/scene_cutscene_native_decode_table.json`",
            "- `analysis/scene_cutscene_native_decode_table.csv`",
            "- `analysis/scene_cutscene_native_command_table.csv`",
            "",
        ]
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def write_outputs(tables: dict[str, Any]) -> None:
    write_json(DEFAULT_OUT_JSON, tables)
    write_markdown(DEFAULT_OUT_MD, tables)
    write_csv(
        DEFAULT_OUT_CUTSCENE_CSV,
        as_list(tables.get("cutscene_rows")),
        [
            "cutscene_source_index",
            "scene_path",
            "scene_stem",
            "scene_id_hex",
            "setup_index",
            "command_argument_hex",
            "payload_symbol",
            "strict_decoded",
            "native_decoded",
            "native_decode_status",
            "native_header_delta_hex",
            "native_header_offset_hex",
            "native_magic_hex",
            "native_version_or_flags_hex",
            "native_command_count",
            "native_end_frame",
            "native_decoded_size",
            "native_command_ref_start",
            "native_command_ref_count",
            "native_decode_error_sample",
        ],
    )
    write_csv(
        DEFAULT_OUT_COMMAND_CSV,
        as_list(tables.get("native_command_rows")),
        [
            "native_command_source_index",
            "cutscene_source_index",
            "scene_path",
            "setup_index",
            "local_command_index",
            "command_offset_hex",
            "command_id_hex",
            "command_name",
            "category",
            "entry_count",
            "camera_point_count",
            "blob_size",
            "packed_stride",
            "payload_size",
            "total_size",
            "raw_prefix_hex",
        ],
    )


def validate(tables: dict[str, Any]) -> list[str]:
    summary = as_dict(tables.get("summary"))
    cutscene_rows = [as_dict(row) for row in as_list(tables.get("cutscene_rows"))]
    command_rows = [as_dict(row) for row in as_list(tables.get("native_command_rows"))]
    errors: list[str] = []
    if int_value(summary.get("cutscene_source_row_count")) != 112:
        errors.append("cutscene reference count changed from audited baseline of 112")
    if int_value(summary.get("native_decoded_cutscene_count")) != 110:
        errors.append("native decoded count changed from audited baseline of 110")
    if int_value(summary.get("native_decode_error_count")) != 2:
        errors.append("native decode error count changed from audited baseline of 2")
    if int_value(summary.get("spot04_native_decoded_count")) != 10:
        errors.append("spot04 native decode coverage changed from 10")
    if int_value(summary.get("native_command_row_count")) != len(command_rows):
        errors.append("native command row count mismatch")
    if int_value(summary.get("missing_audit_binding_count")) != 0:
        errors.append("one or more cutscene rows did not bind to audit records")
    for row in cutscene_rows:
        start = int_value(row.get("native_command_ref_start"))
        count = int_value(row.get("native_command_ref_count"))
        if start + count > len(command_rows):
            errors.append(f"cutscene {row.get('cutscene_source_index')}: command slice outside table")
        elif any(
            int_value(command_rows[index].get("cutscene_source_index"))
            != int_value(row.get("cutscene_source_index"))
            for index in range(start, start + count)
        ):
            errors.append(f"cutscene {row.get('cutscene_source_index')}: command slice owner mismatch")
    return errors


def main() -> int:
    tables = build_tables()
    errors = validate(tables)
    if errors:
        for error in errors:
            print(f"error: {error}", file=sys.stderr)
        return 1
    write_outputs(tables)
    summary = as_dict(tables.get("summary"))
    print(
        "wrote scene cutscene native decode table: "
        f"{summary.get('native_decoded_cutscene_count')} decoded, "
        f"{summary.get('native_decode_error_count')} errors, "
        f"{summary.get('native_command_row_count')} commands"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
