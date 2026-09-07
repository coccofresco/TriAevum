#!/usr/bin/env python3
"""Build C source tables for OOT3D-native cutscene payloads.

`build_scene_cutscene_native_decode_table.py` validates and decodes scene command
0x17 payloads from the original OOT3D ZSI files. This generator promotes those
decoded native headers and command records into compact C tables that can be
consumed by decomp-support code without falling back to strict-N64 probing.
"""

from __future__ import annotations

import csv
import json
import sys
from collections import Counter
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_NATIVE_DECODE_TABLE = ROOT / "analysis" / "scene_cutscene_native_decode_table.json"
DEFAULT_OUT_JSON = ROOT / "analysis" / "scene_cutscene_native_source_table.json"
DEFAULT_OUT_MD = ROOT / "analysis" / "scene_cutscene_native_source_table.md"
DEFAULT_OUT_CUTSCENE_CSV = ROOT / "analysis" / "scene_cutscene_native_source_table.csv"
DEFAULT_OUT_COMMAND_CSV = ROOT / "analysis" / "scene_cutscene_native_command_source_table.csv"
DEFAULT_OUT_HEADER = ROOT / "include" / "oot3d" / "scene_cutscene_native_source_table.h"
DEFAULT_OUT_SOURCE = ROOT / "src" / "code" / "z_scene_cutscene_native_source_table.c"
NO_OFFSET = 0xFFFFFFFF
MAGIC_QDB = 0x51444220


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


def parse_hex_u32(value: Any, default: int = 0) -> int:
    if isinstance(value, int):
        return value & 0xFFFFFFFF
    text = str(value or "").strip()
    if not text:
        return default & 0xFFFFFFFF
    return int(text, 16) & 0xFFFFFFFF


def hex_u32(value: Any) -> str:
    return f"0x{int_value(value) & 0xFFFFFFFF:08x}"


def c_string(value: Any) -> str:
    text = "" if value is None else str(value)
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def c_bool(value: Any) -> str:
    return "1u" if bool_value(value) else "0u"


def c_u8(value: Any) -> str:
    return f"{int_value(value) & 0xFF}u"


def c_u16(value: Any) -> str:
    return f"{int_value(value) & 0xFFFF}u"


def c_s32(value: Any) -> str:
    raw = int_value(value)
    return str(max(-2147483648, min(2147483647, raw)))


def c_u32(value: Any) -> str:
    return f"{int_value(value) & 0xFFFFFFFF}u"


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as handle:
        json.dump(payload, handle, indent=2, sort_keys=True, allow_nan=False)
        handle.write("\n")


def csv_value(value: Any) -> str:
    if isinstance(value, list):
        return "; ".join(str(item) for item in value)
    if isinstance(value, dict):
        return json.dumps(value, sort_keys=True)
    return str(value)


def write_csv(path: Path, rows: list[dict[str, Any]], fieldnames: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames, extrasaction="ignore")
        writer.writeheader()
        for row in rows:
            writer.writerow({key: csv_value(row.get(key, "")) for key in fieldnames})


def decode_status(value: str) -> str:
    return {
        "decoded": "decoded_qdb_native",
        "undecoded": "undecoded_stub_or_no_qdb_magic",
    }.get(value, "undecoded_stub_or_no_qdb_magic")


def decode_status_enum(value: str) -> str:
    return {
        "decoded_qdb_native": "OOT3D_CUTSCENE_NATIVE_DECODED_QDB",
        "undecoded_stub_or_no_qdb_magic": "OOT3D_CUTSCENE_NATIVE_UNDECODED_STUB_OR_NO_QDB_MAGIC",
    }.get(value, "OOT3D_CUTSCENE_NATIVE_UNDECODED_STUB_OR_NO_QDB_MAGIC")


def command_category_enum(value: str) -> str:
    return {
        "blob_u32_size": "OOT3D_CUTSCENE_NATIVE_COMMAND_BLOB_U32_SIZE",
        "blob_16bit_count": "OOT3D_CUTSCENE_NATIVE_COMMAND_BLOB_16BIT_COUNT",
        "camera_list": "OOT3D_CUTSCENE_NATIVE_COMMAND_CAMERA_LIST",
        "counted_12word_entries": "OOT3D_CUTSCENE_NATIVE_COMMAND_COUNTED_12WORD_ENTRIES",
        "counted_3word_entries": "OOT3D_CUTSCENE_NATIVE_COMMAND_COUNTED_3WORD_ENTRIES",
        "fixed16": "OOT3D_CUTSCENE_NATIVE_COMMAND_FIXED16",
        "noop": "OOT3D_CUTSCENE_NATIVE_COMMAND_NOOP",
        "packed_3word_pairs": "OOT3D_CUTSCENE_NATIVE_COMMAND_PACKED_3WORD_PAIRS",
        "single_camera_fixed": "OOT3D_CUTSCENE_NATIVE_COMMAND_SINGLE_CAMERA_FIXED",
    }.get(value, "OOT3D_CUTSCENE_NATIVE_COMMAND_UNKNOWN")


def command_kind(value: str) -> str:
    return {
        "blob_u32_size": "camera_or_runtime_blob",
        "blob_16bit_count": "runtime_blob_16bit_count",
        "camera_list": "camera_point_list",
        "counted_12word_entries": "timeline_record_list_12word",
        "counted_3word_entries": "timeline_record_list_3word",
        "fixed16": "fixed_16_byte_record",
        "noop": "noop",
        "packed_3word_pairs": "packed_3word_pair_records",
        "single_camera_fixed": "single_camera_point",
    }.get(value, "unknown")


def build_source_table(native_decode_path: Path = DEFAULT_NATIVE_DECODE_TABLE) -> dict[str, Any]:
    decode_payload = load_json(native_decode_path)
    source_cutscene_rows: list[dict[str, Any]] = []
    source_command_rows: list[dict[str, Any]] = []

    for row in as_list(decode_payload.get("cutscene_rows")):
        row = as_dict(row)
        status = decode_status(str(row.get("native_decode_status", "")))
        source_cutscene_rows.append(
            {
                "cutscene_source_index": int_value(row.get("cutscene_source_index")),
                "scene_id": int_value(row.get("scene_id"), 0xFF),
                "scene_id_hex": row.get("scene_id_hex", ""),
                "scene_path": row.get("scene_path", ""),
                "scene_stem": row.get("scene_stem", ""),
                "setup_index": int_value(row.get("setup_index")),
                "setup_symbol": row.get("setup_symbol", ""),
                "payload_symbol": row.get("payload_symbol", ""),
                "command_argument": int_value(row.get("command_argument"), NO_OFFSET),
                "command_argument_hex": row.get("command_argument_hex", ""),
                "decode_status": status,
                "native_decoded": bool_value(row.get("native_decoded")),
                "native_header_delta": int_value(row.get("native_header_delta"), NO_OFFSET),
                "native_header_offset": int_value(row.get("native_header_offset"), NO_OFFSET),
                "native_magic": parse_hex_u32(row.get("native_magic_hex"), 0),
                "native_magic_hex": row.get("native_magic_hex", ""),
                "native_version_or_flags": parse_hex_u32(
                    row.get("native_version_or_flags_hex"), 0
                ),
                "native_version_or_flags_hex": row.get("native_version_or_flags_hex", ""),
                "native_command_count": int_value(row.get("native_command_count")),
                "native_end_frame": int_value(row.get("native_end_frame")),
                "native_decoded_size": int_value(row.get("native_decoded_size")),
                "native_command_ref_start": int_value(row.get("native_command_ref_start")),
                "native_command_ref_count": int_value(row.get("native_command_ref_count")),
                "strict_decoded": bool_value(row.get("strict_decoded")),
                "native_decode_error_sample": row.get("native_decode_error_sample", ""),
            }
        )

    for row in as_list(decode_payload.get("native_command_rows")):
        row = as_dict(row)
        category = str(row.get("category", ""))
        source_command_rows.append(
            {
                "native_command_source_index": int_value(row.get("native_command_source_index")),
                "cutscene_source_index": int_value(row.get("cutscene_source_index")),
                "scene_path": row.get("scene_path", ""),
                "setup_index": int_value(row.get("setup_index")),
                "local_command_index": int_value(row.get("local_command_index")),
                "command_offset": int_value(row.get("command_offset"), NO_OFFSET),
                "command_offset_hex": row.get("command_offset_hex", ""),
                "command_id": int_value(row.get("command_id")),
                "command_id_hex": row.get("command_id_hex", ""),
                "command_name": row.get("command_name", ""),
                "category": category,
                "semantic_kind": command_kind(category),
                "entry_count": int_value(row.get("entry_count")),
                "camera_point_count": int_value(row.get("camera_point_count")),
                "blob_size": int_value(row.get("blob_size")),
                "packed_stride": int_value(row.get("packed_stride")),
                "payload_size": int_value(row.get("payload_size")),
                "total_size": int_value(row.get("total_size")),
                "raw_prefix_hex": row.get("raw_prefix_hex", ""),
            }
        )

    category_counts = Counter(str(row.get("category")) for row in source_command_rows)
    kind_counts = Counter(str(row.get("semantic_kind")) for row in source_command_rows)
    status_counts = Counter(str(row.get("decode_status")) for row in source_cutscene_rows)
    decoded_rows = [
        row for row in source_cutscene_rows if row.get("decode_status") == "decoded_qdb_native"
    ]
    bad_magic_rows = [
        row
        for row in decoded_rows
        if int_value(row.get("native_magic")) != MAGIC_QDB
    ]
    non_envelope_rows = [
        row
        for row in decoded_rows
        if int_value(row.get("native_header_delta")) != 0x10
    ]
    command_slice_mismatches: list[dict[str, Any]] = []
    for row in source_cutscene_rows:
        start = int_value(row.get("native_command_ref_start"))
        count = int_value(row.get("native_command_ref_count"))
        if start + count > len(source_command_rows):
            command_slice_mismatches.append(
                {
                    "cutscene_source_index": row.get("cutscene_source_index"),
                    "reason": "command slice outside table",
                }
            )
            continue
        for index in range(start, start + count):
            if int_value(source_command_rows[index].get("cutscene_source_index")) != int_value(
                row.get("cutscene_source_index")
            ):
                command_slice_mismatches.append(
                    {
                        "cutscene_source_index": row.get("cutscene_source_index"),
                        "native_command_source_index": index,
                        "reason": "command slice owner mismatch",
                    }
                )
                break

    summary = {
        "format": "oot3d_scene_cutscene_native_source_table_v1",
        "source_decode_table": str(native_decode_path),
        "cutscene_source_row_count": len(source_cutscene_rows),
        "native_command_source_row_count": len(source_command_rows),
        "decode_status_counts": dict(sorted(status_counts.items())),
        "command_category_counts": dict(sorted(category_counts.items())),
        "semantic_kind_counts": dict(sorted(kind_counts.items())),
        "bad_magic_count": len(bad_magic_rows),
        "non_envelope_header_delta_count": len(non_envelope_rows),
        "command_slice_mismatch_count": len(command_slice_mismatches),
        "native_layout": {
            "asset_envelope_delta": 0x10,
            "magic": hex_u32(MAGIC_QDB),
            "command_count_offset": 0x08,
            "end_frame_offset": 0x0C,
            "command_stream_offset": 0x10,
        },
    }
    return {
        "summary": summary,
        "source_policy": {
            "primary_source": "OOT3D ZSI scene command 0x17 native payloads",
            "runtime_reference": "code.bin Cutscene_ProcessCommands at 0x002C5BA0",
            "strict_n64_policy": "strict-N64-compatible rows are preserved as subset evidence only, not used as the primary layout",
        },
        "cutscene_rows": source_cutscene_rows,
        "native_command_rows": source_command_rows,
        "bad_magic_rows": bad_magic_rows,
        "non_envelope_rows": non_envelope_rows,
        "command_slice_mismatches": command_slice_mismatches,
    }


CUTSCENE_CSV_COLUMNS = [
    "cutscene_source_index",
    "scene_id_hex",
    "scene_id",
    "scene_path",
    "scene_stem",
    "setup_index",
    "command_argument_hex",
    "decode_status",
    "native_header_offset",
    "native_header_offset_hex",
    "native_command_count",
    "native_end_frame",
    "native_decoded_size",
    "native_command_ref_start",
    "native_command_ref_count",
    "strict_decoded",
    "payload_symbol",
    "native_decode_error_sample",
]

COMMAND_CSV_COLUMNS = [
    "native_command_source_index",
    "cutscene_source_index",
    "scene_path",
    "setup_index",
    "local_command_index",
    "command_offset_hex",
    "command_id_hex",
    "command_name",
    "category",
    "semantic_kind",
    "entry_count",
    "camera_point_count",
    "blob_size",
    "payload_size",
    "total_size",
    "raw_prefix_hex",
]


def write_markdown(path: Path, payload: dict[str, Any]) -> None:
    summary = as_dict(payload.get("summary"))
    cutscene_rows = [as_dict(row) for row in as_list(payload.get("cutscene_rows"))]
    kokiri_rows = [row for row in cutscene_rows if row.get("scene_path") == "spot04_info.zsi"]
    lines = [
        "# Scene Cutscene Native Source Table",
        "",
        "Generated C-facing source table for OOT3D-native scene command `0x17` cutscene payloads.",
        "",
        "## Summary",
        "",
        f"- Cutscene source rows: {summary.get('cutscene_source_row_count')}",
        f"- Native command source rows: {summary.get('native_command_source_row_count')}",
        f"- Decode status: `{json.dumps(summary.get('decode_status_counts'), sort_keys=True)}`",
        f"- Command categories: `{json.dumps(summary.get('command_category_counts'), sort_keys=True)}`",
        f"- Semantic kinds: `{json.dumps(summary.get('semantic_kind_counts'), sort_keys=True)}`",
        f"- Bad magic rows: {summary.get('bad_magic_count')}",
        f"- Non-`+0x10` envelope rows: {summary.get('non_envelope_header_delta_count')}",
        f"- Command slice mismatches: {summary.get('command_slice_mismatch_count')}",
        "",
        "## Native Layout",
        "",
        "- Scene command `0x17` points to an OOT3D asset envelope.",
        "- The effective native header starts at envelope `+0x10`.",
        "- Header `+0x00` is `QDB `, `+0x08` is command count, `+0x0C` is end frame, and command records start at header `+0x10`.",
        "",
        "## Kokiri / `spot04_info.zsi`",
        "",
        "| cutscene | setup | header | commands | end frame | command slice |",
        "| ---: | ---: | ---: | ---: | ---: | --- |",
    ]
    for row in sorted(kokiri_rows, key=lambda item: int_value(item.get("setup_index"))):
        command_slice = f"{row.get('native_command_ref_start')}+{row.get('native_command_ref_count')}"
        lines.append(
            f"| {row.get('cutscene_source_index')} | {row.get('setup_index')} | "
            f"`{hex_u32(row.get('native_header_offset'))}` | {row.get('native_command_count')} | "
            f"{row.get('native_end_frame')} | `{command_slice}` |"
        )

    lines.extend(
        [
            "",
            "## Outputs",
            "",
            "- `include/oot3d/scene_cutscene_native_source_table.h`",
            "- `src/code/z_scene_cutscene_native_source_table.c`",
            "- `analysis/scene_cutscene_native_source_table.json`",
            "- `analysis/scene_cutscene_native_source_table.csv`",
            "- `analysis/scene_cutscene_native_command_source_table.csv`",
            "",
        ]
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def write_header(path: Path, payload: dict[str, Any]) -> None:
    cutscene_count = len(as_list(payload.get("cutscene_rows")))
    command_count = len(as_list(payload.get("native_command_rows")))
    lines = [
        "#ifndef OOT3D_SCENE_CUTSCENE_NATIVE_SOURCE_TABLE_H",
        "#define OOT3D_SCENE_CUTSCENE_NATIVE_SOURCE_TABLE_H",
        "",
        '#include "oot3d/scene.h"',
        '#include "oot3d/scene_cutscene_source_table.h"',
        "",
        "enum {",
        f"    OOT3D_SCENE_CUTSCENE_NATIVE_SOURCE_ROW_COUNT = {cutscene_count},",
        f"    OOT3D_SCENE_CUTSCENE_NATIVE_COMMAND_SOURCE_ROW_COUNT = {command_count},",
        "    OOT3D_SCENE_CUTSCENE_NATIVE_NO_OFFSET = 0xFFFFFFFF,",
        "    OOT3D_SCENE_CUTSCENE_NATIVE_MAGIC_QDB = 0x51444220,",
        "};",
        "",
        "typedef enum {",
        "    OOT3D_CUTSCENE_NATIVE_DECODED_QDB,",
        "    OOT3D_CUTSCENE_NATIVE_UNDECODED_STUB_OR_NO_QDB_MAGIC,",
        "} Oot3dCutsceneNativeDecodeStatus;",
        "",
        "typedef enum {",
        "    OOT3D_CUTSCENE_NATIVE_COMMAND_UNKNOWN,",
        "    OOT3D_CUTSCENE_NATIVE_COMMAND_BLOB_U32_SIZE,",
        "    OOT3D_CUTSCENE_NATIVE_COMMAND_BLOB_16BIT_COUNT,",
        "    OOT3D_CUTSCENE_NATIVE_COMMAND_CAMERA_LIST,",
        "    OOT3D_CUTSCENE_NATIVE_COMMAND_COUNTED_12WORD_ENTRIES,",
        "    OOT3D_CUTSCENE_NATIVE_COMMAND_COUNTED_3WORD_ENTRIES,",
        "    OOT3D_CUTSCENE_NATIVE_COMMAND_FIXED16,",
        "    OOT3D_CUTSCENE_NATIVE_COMMAND_NOOP,",
        "    OOT3D_CUTSCENE_NATIVE_COMMAND_PACKED_3WORD_PAIRS,",
        "    OOT3D_CUTSCENE_NATIVE_COMMAND_SINGLE_CAMERA_FIXED,",
        "} Oot3dCutsceneNativeCommandCategory;",
        "",
        "typedef struct {",
        "    u16 cutsceneSourceIndex;",
        "    u8 sceneId;",
        "    u16 setupIndex;",
        "    u32 commandArgument;",
        "    Oot3dCutsceneNativeDecodeStatus decodeStatus;",
        "    u8 nativeDecoded;",
        "    u32 nativeHeaderDelta;",
        "    u32 nativeHeaderOffset;",
        "    u32 nativeMagic;",
        "    u32 nativeVersionOrFlags;",
        "    u16 nativeCommandCount;",
        "    s32 nativeEndFrame;",
        "    u32 nativeDecodedSize;",
        "    u16 nativeCommandRefStart;",
        "    u16 nativeCommandRefCount;",
        "    u8 strictDecoded;",
        "    const char* scenePath;",
        "    const char* sceneStem;",
        "    const char* setupSymbol;",
        "    const char* payloadSymbol;",
        "    const char* nativeDecodeErrorSample;",
        "} Oot3dSceneCutsceneNativeSourceRow;",
        "",
        "typedef struct {",
        "    u16 nativeCommandSourceIndex;",
        "    u16 cutsceneSourceIndex;",
        "    u16 setupIndex;",
        "    u16 localCommandIndex;",
        "    u32 commandOffset;",
        "    s32 commandId;",
        "    Oot3dCutsceneNativeCommandCategory category;",
        "    u16 entryCount;",
        "    u16 cameraPointCount;",
        "    u32 blobSize;",
        "    u32 packedStride;",
        "    u32 payloadSize;",
        "    u32 totalSize;",
        "    const char* commandName;",
        "    const char* categoryName;",
        "    const char* semanticKind;",
        "    const char* rawPrefixHex;",
        "} Oot3dSceneCutsceneNativeCommandSourceRow;",
        "",
        "extern const Oot3dSceneCutsceneNativeSourceRow oot3d_scene_cutscene_native_source_rows[];",
        "extern const Oot3dSceneCutsceneNativeCommandSourceRow oot3d_scene_cutscene_native_command_source_rows[];",
        "extern const u32 oot3d_scene_cutscene_native_source_row_count;",
        "extern const u32 oot3d_scene_cutscene_native_command_source_row_count;",
        "",
        "#endif",
        "",
    ]
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def write_source(path: Path, payload: dict[str, Any]) -> None:
    cutscene_rows = [as_dict(row) for row in as_list(payload.get("cutscene_rows"))]
    command_rows = [as_dict(row) for row in as_list(payload.get("native_command_rows"))]
    lines = [
        "/* Generated by build_scene_cutscene_native_source_table.py. */",
        "",
        '#include "oot3d/scene_cutscene_native_source_table.h"',
        "",
        "const Oot3dSceneCutsceneNativeSourceRow oot3d_scene_cutscene_native_source_rows[] = {",
    ]
    for row in cutscene_rows:
        lines.append(
            "    { "
            f"{c_u16(row.get('cutscene_source_index'))}, "
            f"{c_u8(row.get('scene_id'))}, "
            f"{c_u16(row.get('setup_index'))}, "
            f"{c_u32(row.get('command_argument'))}, "
            f"{decode_status_enum(str(row.get('decode_status')))}, "
            f"{c_bool(row.get('native_decoded'))}, "
            f"{c_u32(row.get('native_header_delta'))}, "
            f"{c_u32(row.get('native_header_offset'))}, "
            f"{c_u32(row.get('native_magic'))}, "
            f"{c_u32(row.get('native_version_or_flags'))}, "
            f"{c_u16(row.get('native_command_count'))}, "
            f"{c_s32(row.get('native_end_frame'))}, "
            f"{c_u32(row.get('native_decoded_size'))}, "
            f"{c_u16(row.get('native_command_ref_start'))}, "
            f"{c_u16(row.get('native_command_ref_count'))}, "
            f"{c_bool(row.get('strict_decoded'))}, "
            f"{c_string(row.get('scene_path'))}, "
            f"{c_string(row.get('scene_stem'))}, "
            f"{c_string(row.get('setup_symbol'))}, "
            f"{c_string(row.get('payload_symbol'))}, "
            f"{c_string(row.get('native_decode_error_sample'))}"
            " },"
        )
    lines.extend(["};", "", "const Oot3dSceneCutsceneNativeCommandSourceRow oot3d_scene_cutscene_native_command_source_rows[] = {"])
    for row in command_rows:
        lines.append(
            "    { "
            f"{c_u16(row.get('native_command_source_index'))}, "
            f"{c_u16(row.get('cutscene_source_index'))}, "
            f"{c_u16(row.get('setup_index'))}, "
            f"{c_u16(row.get('local_command_index'))}, "
            f"{c_u32(row.get('command_offset'))}, "
            f"{c_s32(row.get('command_id'))}, "
            f"{command_category_enum(str(row.get('category')))}, "
            f"{c_u16(row.get('entry_count'))}, "
            f"{c_u16(row.get('camera_point_count'))}, "
            f"{c_u32(row.get('blob_size'))}, "
            f"{c_u32(row.get('packed_stride'))}, "
            f"{c_u32(row.get('payload_size'))}, "
            f"{c_u32(row.get('total_size'))}, "
            f"{c_string(row.get('command_name'))}, "
            f"{c_string(row.get('category'))}, "
            f"{c_string(row.get('semantic_kind'))}, "
            f"{c_string(row.get('raw_prefix_hex'))}"
            " },"
        )
    lines.extend(
        [
            "};",
            "",
            "const u32 oot3d_scene_cutscene_native_source_row_count = OOT3D_SCENE_CUTSCENE_NATIVE_SOURCE_ROW_COUNT;",
            "const u32 oot3d_scene_cutscene_native_command_source_row_count = OOT3D_SCENE_CUTSCENE_NATIVE_COMMAND_SOURCE_ROW_COUNT;",
            "",
        ]
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def write_outputs(payload: dict[str, Any]) -> None:
    write_json(DEFAULT_OUT_JSON, payload)
    write_markdown(DEFAULT_OUT_MD, payload)
    write_csv(DEFAULT_OUT_CUTSCENE_CSV, as_list(payload.get("cutscene_rows")), CUTSCENE_CSV_COLUMNS)
    write_csv(
        DEFAULT_OUT_COMMAND_CSV,
        as_list(payload.get("native_command_rows")),
        COMMAND_CSV_COLUMNS,
    )
    write_header(DEFAULT_OUT_HEADER, payload)
    write_source(DEFAULT_OUT_SOURCE, payload)


def validate(payload: dict[str, Any]) -> list[str]:
    summary = as_dict(payload.get("summary"))
    cutscene_rows = [as_dict(row) for row in as_list(payload.get("cutscene_rows"))]
    command_rows = [as_dict(row) for row in as_list(payload.get("native_command_rows"))]
    errors: list[str] = []
    if int_value(summary.get("cutscene_source_row_count")) != 112:
        errors.append("cutscene source row count changed from baseline of 112")
    if int_value(summary.get("native_command_source_row_count")) != 1154:
        errors.append("native command source row count changed from baseline of 1154")
    if int_value(summary.get("bad_magic_count")) != 0:
        errors.append("decoded native rows include non-QDB magic")
    if int_value(summary.get("non_envelope_header_delta_count")) != 0:
        errors.append("decoded native rows include an envelope delta other than +0x10")
    if int_value(summary.get("command_slice_mismatch_count")) != 0:
        errors.append("command slice mismatch detected")
    if sum(1 for row in cutscene_rows if row.get("decode_status") == "decoded_qdb_native") != 110:
        errors.append("decoded native cutscene count changed from baseline of 110")
    for row in command_rows:
        total_size = int_value(row.get("total_size"))
        payload_size = int_value(row.get("payload_size"))
        if total_size < payload_size:
            errors.append(
                f"command {row.get('native_command_source_index')}: total size smaller than payload size"
            )
            break
    return errors


def main() -> int:
    payload = build_source_table()
    errors = validate(payload)
    if errors:
        for error in errors:
            print(f"error: {error}", file=sys.stderr)
        return 1
    write_outputs(payload)
    summary = as_dict(payload.get("summary"))
    print(
        "wrote scene cutscene native source table: "
        f"{summary.get('cutscene_source_row_count')} cutscenes, "
        f"{summary.get('native_command_source_row_count')} commands"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
