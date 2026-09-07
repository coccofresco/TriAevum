#!/usr/bin/env python3
"""Export OOT3D native scene resource rows from code.bin.

The native code image contains fixed 0x8C-byte records with a ZSI path, a ZAR
path, and a 4-byte metadata trailer. The runtime scene-id range is reconstructed
from the OOT3D-native loader layout:

- function 0x002EAFB4 copies 14 selected records to 0x00545484
- normal selected records come from 0x004DC400
- alternate selected records come from 0x004DCBA8
- scene ids 0x0E..0x6E use the static runtime tail at 0x00545C2C

The alternate selected records are exported separately and are not promoted as
default scene ids.
"""

from __future__ import annotations

import csv
import json
import re
from collections import Counter
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CODE_BIN = Path(r"E:\ppssppvr\oot3d_decomp\work\extract\exefs\code.bin")
DEFAULT_SCENE_INDEX = ROOT / "analysis" / "oot3d_native_scene_index_all_full.json"
DEFAULT_SCENE_INDEX_REGISTRY = ROOT / "src" / "assets" / "scenes" / "scene_index_registry.c"
DEFAULT_N64_SCENE_TABLE = ROOT / "reference_inputs" / "n64_scene_table.h"
DEFAULT_N64_SCENE_HEADER = ROOT / "reference_inputs" / "n64_z64scene.h"
DEFAULT_OUT_JSON = ROOT / "analysis" / "scene_resource_table.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "scene_resource_table.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "scene_resource_table.md"
DEFAULT_OUT_HEADER = ROOT / "include" / "oot3d" / "scene_resource.h"
DEFAULT_OUT_SOURCE = ROOT / "src" / "code" / "z_scene_resource_table.c"

CODE_LOAD_BASE = 0x00100000
RESOURCE_COPY_FUNCTION = 0x002EAFB4
RUNTIME_RESOURCE_TABLE = 0x00545484
SELECTED_NORMAL_RESOURCE_SOURCE = 0x004DC400
SELECTED_ALTERNATE_RESOURCE_SOURCE = 0x004DCBA8
STATIC_TAIL_RESOURCE_TABLE = 0x00545C2C
RESOURCE_ENTRY_SIZE = 0x8C
RESOURCE_ZSI_OFFSET = 0x00
RESOURCE_ZAR_OFFSET = 0x44
RESOURCE_METADATA_OFFSET = 0x88
SELECTED_SCENE_COUNT = 0x0E
SELECTED_COPY_SIZE = SELECTED_SCENE_COUNT * RESOURCE_ENTRY_SIZE
TAIL_SCENE_START = SELECTED_SCENE_COUNT
TOTAL_SCENE_COUNT = 0x6F
TAIL_SCENE_COUNT = TOTAL_SCENE_COUNT - TAIL_SCENE_START


def as_list(value: Any) -> list[Any]:
    return value if isinstance(value, list) else []


def c_string(value: str) -> str:
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


def rom_scene_basename(value: str) -> str:
    prefix = "rom:/scene/"
    return value[len(prefix) :] if value.startswith(prefix) else value


def cstr(code: bytes, offset: int) -> str:
    if offset < 0 or offset >= len(code):
        return ""
    end = code.find(b"\x00", offset)
    if end < 0:
        end = len(code)
    return code[offset:end].decode("ascii", errors="ignore")


def int_token(value: str) -> int | None:
    value = value.strip()
    try:
        return int(value, 0)
    except ValueError:
        return None


def parse_scene_draw_config_enum(path: Path) -> dict[str, int]:
    values: dict[str, int] = {}
    if not path.is_file():
        return values
    pattern = re.compile(r"/\*\s*(?P<index>[0-9]+)\s*\*/\s*(?P<name>SDC_[A-Za-z0-9_]+)")
    for line in path.read_text(encoding="utf-8", errors="ignore").splitlines():
        match = pattern.search(line)
        if match is None:
            continue
        values[match.group("name").strip()] = int(match.group("index"), 10)
    return values


def parse_scene_index_registry(path: Path) -> dict[str, dict[str, str]]:
    registry: dict[str, dict[str, str]] = {}
    if not path.is_file():
        return registry
    pattern = re.compile(
        r'\{\s*"(?P<stem>[^"]*)",\s*"(?P<path>[^"]*)",\s*"(?P<source>[^"]*)",\s*&(?P<symbol>[A-Za-z0-9_]+)\s*\}'
    )
    for line in path.read_text(encoding="utf-8", errors="ignore").splitlines():
        match = pattern.search(line)
        if match is None:
            continue
        registry[match.group("path").lower()] = {
            "native_scene_index_stem": match.group("stem"),
            "native_scene_source_basename": match.group("source"),
            "native_scene_index_symbol": match.group("symbol"),
        }
    return registry


def read_resource_record(code: bytes, address: int, record_index: int, runtime_address: int) -> dict[str, Any]:
    file_offset = address - CODE_LOAD_BASE + record_index * RESOURCE_ENTRY_SIZE
    record_address = address + record_index * RESOURCE_ENTRY_SIZE
    runtime_record_address = runtime_address
    metadata_offset = file_offset + RESOURCE_METADATA_OFFSET
    raw = code[metadata_offset : metadata_offset + 4]
    if len(raw) != 4:
        raise ValueError(f"record outside code.bin: address=0x{address:08x} index={record_index}")
    zsi_path = cstr(code, file_offset + RESOURCE_ZSI_OFFSET)
    zar_path = cstr(code, file_offset + RESOURCE_ZAR_OFFSET)
    return {
        "record_index": record_index,
        "record_address": record_address,
        "record_address_hex": f"0x{record_address:08x}",
        "record_file_offset": file_offset,
        "record_file_offset_hex": f"0x{file_offset:08x}",
        "runtime_address": runtime_record_address,
        "runtime_address_hex": f"0x{runtime_record_address:08x}",
        "runtime_metadata_address": runtime_record_address + RESOURCE_METADATA_OFFSET,
        "runtime_metadata_address_hex": f"0x{runtime_record_address + RESOURCE_METADATA_OFFSET:08x}",
        "metadata_address": record_address + RESOURCE_METADATA_OFFSET,
        "metadata_address_hex": f"0x{record_address + RESOURCE_METADATA_OFFSET:08x}",
        "metadata_file_offset": metadata_offset,
        "metadata_file_offset_hex": f"0x{metadata_offset:08x}",
        "metadata_raw": int.from_bytes(raw, "little"),
        "metadata_raw_hex": f"0x{int.from_bytes(raw, 'little'):08x}",
        "metadata_bytes_hex": raw.hex(),
        "metadata_b0": raw[0],
        "metadata_b1": raw[1],
        "metadata_b2": raw[2],
        "metadata_b3": raw[3],
        "zsi_rom_path": zsi_path,
        "zar_rom_path": zar_path,
        "zsi_path": rom_scene_basename(zsi_path),
        "zar_path": rom_scene_basename(zar_path),
    }


def parse_secondary_scene_labels(path: Path, draw_config_indices: dict[str, int]) -> dict[int, dict[str, Any]]:
    labels: dict[int, dict[str, Any]] = {}
    if not path.is_file():
        return labels
    pattern = re.compile(
        r"/\*\s*0x(?P<scene_id>[0-9A-Fa-f]+)\s*\*/\s*"
        r"DEFINE_SCENE\((?P<scene_symbol>[^,]+),\s*[^,]+,\s*(?P<scene_enum>[^,]+),"
        r"\s*(?P<draw_config>[^,]+),\s*(?P<unk10>[^,]+),\s*(?P<unk12>[^)]+)\)"
    )
    for line in path.read_text(encoding="utf-8", errors="ignore").splitlines():
        match = pattern.search(line)
        if match is None:
            continue
        scene_id = int(match.group("scene_id"), 16)
        scene_symbol = match.group("scene_symbol").strip()
        stem = scene_symbol[:-6] if scene_symbol.endswith("_scene") else scene_symbol
        draw_config = match.group("draw_config").strip()
        unk10 = int_token(match.group("unk10"))
        unk12 = int_token(match.group("unk12"))
        labels[scene_id] = {
            "secondary_scene_symbol": scene_symbol,
            "secondary_scene_stem": stem,
            "secondary_scene_enum": match.group("scene_enum").strip(),
            "secondary_scene_draw_config": draw_config,
            "secondary_scene_draw_config_index": draw_config_indices.get(draw_config),
            "secondary_scene_unk10": unk10,
            "secondary_scene_unk12": unk12,
        }
    return labels


def scene_index_paths(scene_index: dict[str, Any]) -> set[str]:
    return {
        str(record.get("scene_path", "")).lower()
        for record in as_list(scene_index.get("records"))
        if isinstance(record, dict) and record.get("scene_path")
    }


def coverage_status(zsi_path: str, indexed_paths: set[str]) -> str:
    if not zsi_path:
        return "no_zsi_path"
    return "native_zsi_indexed" if zsi_path.lower() in indexed_paths else "native_zsi_not_indexed"


def metadata_correlations(record: dict[str, Any], label: dict[str, Any]) -> dict[str, Any]:
    draw_config_index = label.get("secondary_scene_draw_config_index")
    unk10 = label.get("secondary_scene_unk10")
    unk12 = label.get("secondary_scene_unk12")
    return {
        "metadata_b0_matches_secondary_unk10": isinstance(unk10, int) and record["metadata_b0"] == unk10,
        "metadata_b1_matches_secondary_draw_config": isinstance(draw_config_index, int)
        and record["metadata_b1"] == draw_config_index,
        "metadata_b2_matches_secondary_unk12": isinstance(unk12, int) and record["metadata_b2"] == unk12,
    }


def scene_index_binding(record: dict[str, Any], scene_registry: dict[str, dict[str, str]]) -> dict[str, Any]:
    binding = scene_registry.get(str(record.get("zsi_path", "")).lower(), {})
    return {
        **binding,
        "native_scene_index_registry_status": "native_scene_index_resolved"
        if binding
        else "native_scene_index_missing",
    }


def scene_row(
    code: bytes,
    indexed_paths: set[str],
    secondary_labels: dict[int, dict[str, Any]],
    scene_registry: dict[str, dict[str, str]],
    scene_id: int,
    table_address: int,
    record_index: int,
    source_block: str,
) -> dict[str, Any]:
    runtime_address = RUNTIME_RESOURCE_TABLE + scene_id * RESOURCE_ENTRY_SIZE
    record = read_resource_record(code, table_address, record_index, runtime_address)
    label = secondary_labels.get(scene_id, {})
    return {
        "scene_id": scene_id,
        "scene_id_hex": f"0x{scene_id:02x}",
        "source_block": source_block,
        **record,
        **label,
        **scene_index_binding(record, scene_registry),
        **metadata_correlations(record, label),
        "native_coverage_status": coverage_status(str(record.get("zsi_path", "")), indexed_paths),
    }


def build_report() -> dict[str, Any]:
    code = DEFAULT_CODE_BIN.read_bytes()
    scene_index = json.loads(DEFAULT_SCENE_INDEX.read_text(encoding="utf-8"))
    indexed_paths = scene_index_paths(scene_index)
    scene_registry = parse_scene_index_registry(DEFAULT_SCENE_INDEX_REGISTRY)
    draw_config_indices = parse_scene_draw_config_enum(DEFAULT_N64_SCENE_HEADER)
    secondary_labels = parse_secondary_scene_labels(DEFAULT_N64_SCENE_TABLE, draw_config_indices)

    rows: list[dict[str, Any]] = []
    for scene_id in range(SELECTED_SCENE_COUNT):
        rows.append(
            scene_row(
                code,
                indexed_paths,
                secondary_labels,
                scene_registry,
                scene_id,
                SELECTED_NORMAL_RESOURCE_SOURCE,
                scene_id,
                "selected_normal",
            )
        )
    for record_index in range(TAIL_SCENE_COUNT):
        scene_id = TAIL_SCENE_START + record_index
        rows.append(
            scene_row(
                code,
                indexed_paths,
                secondary_labels,
                scene_registry,
                scene_id,
                STATIC_TAIL_RESOURCE_TABLE,
                record_index,
                "static_tail",
            )
        )

    variant_rows: list[dict[str, Any]] = []
    for variant_index in range(SELECTED_SCENE_COUNT):
        scene_id = variant_index
        record = read_resource_record(
            code,
            SELECTED_ALTERNATE_RESOURCE_SOURCE,
            variant_index,
            RUNTIME_RESOURCE_TABLE + scene_id * RESOURCE_ENTRY_SIZE,
        )
        label = secondary_labels.get(scene_id, {})
        variant_rows.append(
            {
                "variant_index": variant_index,
                "variant_index_hex": f"0x{variant_index:02x}",
                "scene_id": scene_id,
                "scene_id_hex": f"0x{scene_id:02x}",
                "source_block": "selected_alternate",
                **record,
                **label,
                **scene_index_binding(record, scene_registry),
                **metadata_correlations(record, label),
                "native_coverage_status": coverage_status(str(record.get("zsi_path", "")), indexed_paths),
            }
        )

    coverage_counts = Counter(str(row.get("native_coverage_status", "")) for row in rows)
    variant_coverage_counts = Counter(str(row.get("native_coverage_status", "")) for row in variant_rows)
    registry_counts = Counter(str(row.get("native_scene_index_registry_status", "")) for row in rows)
    variant_registry_counts = Counter(
        str(row.get("native_scene_index_registry_status", "")) for row in variant_rows
    )
    b0_compared = [row for row in rows if isinstance(row.get("secondary_scene_unk10"), int)]
    b1_compared = [row for row in rows if isinstance(row.get("secondary_scene_draw_config_index"), int)]
    b2_compared = [row for row in rows if isinstance(row.get("secondary_scene_unk12"), int)]
    b1_mismatches = [
        {
            "scene_id_hex": row["scene_id_hex"],
            "zsi_path": row["zsi_path"],
            "metadata_b1": row["metadata_b1"],
            "secondary_scene_draw_config": row.get("secondary_scene_draw_config"),
            "secondary_scene_draw_config_index": row.get("secondary_scene_draw_config_index"),
        }
        for row in b1_compared
        if not row["metadata_b1_matches_secondary_draw_config"]
    ]
    summary = {
        "format": "oot3d_scene_resource_table_v1",
        "entry_size": RESOURCE_ENTRY_SIZE,
        "zsi_offset": RESOURCE_ZSI_OFFSET,
        "zar_offset": RESOURCE_ZAR_OFFSET,
        "metadata_offset": RESOURCE_METADATA_OFFSET,
        "resource_copy_function": f"0x{RESOURCE_COPY_FUNCTION:08x}",
        "runtime_resource_table": f"0x{RUNTIME_RESOURCE_TABLE:08x}",
        "runtime_resource_table_file_offset": f"0x{RUNTIME_RESOURCE_TABLE - CODE_LOAD_BASE:08x}",
        "selected_normal_resource_source": f"0x{SELECTED_NORMAL_RESOURCE_SOURCE:08x}",
        "selected_normal_resource_source_file_offset": f"0x{SELECTED_NORMAL_RESOURCE_SOURCE - CODE_LOAD_BASE:08x}",
        "selected_alternate_resource_source": f"0x{SELECTED_ALTERNATE_RESOURCE_SOURCE:08x}",
        "selected_alternate_resource_source_file_offset": f"0x{SELECTED_ALTERNATE_RESOURCE_SOURCE - CODE_LOAD_BASE:08x}",
        "selected_copy_size": SELECTED_COPY_SIZE,
        "selected_copy_size_hex": f"0x{SELECTED_COPY_SIZE:08x}",
        "static_tail_resource_table": f"0x{STATIC_TAIL_RESOURCE_TABLE:08x}",
        "static_tail_resource_table_file_offset": f"0x{STATIC_TAIL_RESOURCE_TABLE - CODE_LOAD_BASE:08x}",
        "scene_row_count": len(rows),
        "scene_id_min": rows[0]["scene_id_hex"],
        "scene_id_max": rows[-1]["scene_id_hex"],
        "selected_scene_count": SELECTED_SCENE_COUNT,
        "tail_scene_start": TAIL_SCENE_START,
        "tail_scene_count": TAIL_SCENE_COUNT,
        "selected_alternate_variant_count": len(variant_rows),
        "coverage_status_counts": dict(sorted(coverage_counts.items())),
        "variant_coverage_status_counts": dict(sorted(variant_coverage_counts.items())),
        "scene_index_registry_status_counts": dict(sorted(registry_counts.items())),
        "variant_scene_index_registry_status_counts": dict(sorted(variant_registry_counts.items())),
        "metadata_b0_secondary_unk10_match_count": sum(
            1 for row in b0_compared if row["metadata_b0_matches_secondary_unk10"]
        ),
        "metadata_b0_secondary_unk10_compared_count": len(b0_compared),
        "metadata_b1_secondary_draw_config_match_count": sum(
            1 for row in b1_compared if row["metadata_b1_matches_secondary_draw_config"]
        ),
        "metadata_b1_secondary_draw_config_compared_count": len(b1_compared),
        "metadata_b2_secondary_unk12_match_count": sum(
            1 for row in b2_compared if row["metadata_b2_matches_secondary_unk12"]
        ),
        "metadata_b2_secondary_unk12_compared_count": len(b2_compared),
        "metadata_b1_secondary_draw_config_mismatches": b1_mismatches,
    }
    return {
        "summary": summary,
        "source_policy": {
            "native_code_source": str(DEFAULT_CODE_BIN),
        "native_asset_index": str(DEFAULT_SCENE_INDEX),
        "native_scene_index_registry_source": str(DEFAULT_SCENE_INDEX_REGISTRY),
        "secondary_scene_label_source": str(DEFAULT_N64_SCENE_TABLE),
        "secondary_scene_draw_config_source": str(DEFAULT_N64_SCENE_HEADER),
        "n64_policy": "N64/SoH scene_table is used only for secondary labels and metadata hypotheses. Scene ids, metadata, and paths are decoded from OOT3D code.bin.",
        },
        "rows": rows,
        "variant_rows": variant_rows,
    }


CSV_COLUMNS = [
    "scene_id_hex",
    "scene_id",
    "source_block",
    "record_index",
    "record_address_hex",
    "runtime_address_hex",
    "metadata_address_hex",
    "runtime_metadata_address_hex",
    "metadata_bytes_hex",
    "metadata_raw_hex",
    "zsi_path",
    "zar_path",
    "native_scene_source_basename",
    "native_scene_index_symbol",
    "native_scene_index_registry_status",
    "secondary_scene_enum",
    "secondary_scene_stem",
    "secondary_scene_draw_config",
    "secondary_scene_draw_config_index",
    "secondary_scene_unk10",
    "secondary_scene_unk12",
    "metadata_b0_matches_secondary_unk10",
    "metadata_b1_matches_secondary_draw_config",
    "metadata_b2_matches_secondary_unk12",
    "native_coverage_status",
]


def csv_value(value: Any) -> str:
    return json.dumps(value, sort_keys=True) if isinstance(value, (dict, list)) else str(value)


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def write_csv(path: Path, rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=CSV_COLUMNS)
        writer.writeheader()
        for row in rows:
            writer.writerow({key: csv_value(row.get(key, "")) for key in CSV_COLUMNS})


def write_markdown(path: Path, payload: dict[str, Any]) -> None:
    summary = payload["summary"]
    lines = [
        "# Scene Resource Table",
        "",
        "Generated from OOT3D `code.bin` fixed scene resource records.",
        "N64/SoH scene-table names are secondary labels only.",
        "",
        "## Summary",
        "",
        f"- Entry size: `0x{summary['entry_size']:02x}`",
        f"- Layout: ZSI at `+0x{summary['zsi_offset']:02x}`, ZAR at `+0x{summary['zar_offset']:02x}`, metadata trailer at `+0x{summary['metadata_offset']:02x}`",
        f"- Runtime resource table base: `{summary['runtime_resource_table']}` / file offset `{summary['runtime_resource_table_file_offset']}`",
        f"- Selected-record copy function: `{summary['resource_copy_function']}`, copy size `{summary['selected_copy_size_hex']}`",
        f"- Normal selected-record source: `{summary['selected_normal_resource_source']}` / file offset `{summary['selected_normal_resource_source_file_offset']}`",
        f"- Alternate selected-record source: `{summary['selected_alternate_resource_source']}` / file offset `{summary['selected_alternate_resource_source_file_offset']}`",
        f"- Static tail resource table: `{summary['static_tail_resource_table']}` / file offset `{summary['static_tail_resource_table_file_offset']}`",
        f"- Scene rows: {summary['scene_row_count']} (`{summary['scene_id_min']}`..`{summary['scene_id_max']}`)",
        f"- Selected scene rows: {summary['selected_scene_count']}",
        f"- Tail scene rows: {summary['tail_scene_count']} starting at `0x{summary['tail_scene_start']:02x}`",
        f"- Alternate selected-record variant rows: {summary['selected_alternate_variant_count']}",
        f"- Coverage: `{json.dumps(summary['coverage_status_counts'], sort_keys=True)}`",
        f"- Variant coverage: `{json.dumps(summary['variant_coverage_status_counts'], sort_keys=True)}`",
        f"- Scene-index registry: `{json.dumps(summary['scene_index_registry_status_counts'], sort_keys=True)}`",
        f"- Variant scene-index registry: `{json.dumps(summary['variant_scene_index_registry_status_counts'], sort_keys=True)}`",
        f"- Secondary metadata correlation: byte0 vs N64 `unk_10` {summary['metadata_b0_secondary_unk10_match_count']}/{summary['metadata_b0_secondary_unk10_compared_count']}; byte1 vs N64 draw config {summary['metadata_b1_secondary_draw_config_match_count']}/{summary['metadata_b1_secondary_draw_config_compared_count']}; byte2 vs N64 `unk_12` {summary['metadata_b2_secondary_unk12_match_count']}/{summary['metadata_b2_secondary_unk12_compared_count']}",
        "",
        "## Scene Rows",
        "",
        "| SceneId | Block | Record | Source addr | Runtime addr | Metadata | ZSI | Scene index | Secondary scene | Secondary draw config | Coverage |",
        "| --- | --- | ---: | --- | --- | --- | --- | --- | --- | --- | --- |",
    ]
    for row in payload["rows"]:
        lines.append(
            f"| `{row['scene_id_hex']}` | `{row['source_block']}` | {row['record_index']} | "
            f"`{row['record_address_hex']}` | `{row['runtime_address_hex']}` | `{row['metadata_bytes_hex']}` | "
            f"`{row['zsi_path']}` | `{row.get('native_scene_index_symbol', '')}` | `{row.get('secondary_scene_enum', '')}` | "
            f"`{row.get('secondary_scene_draw_config', '')}` | `{row['native_coverage_status']}` |"
        )

    lines.extend(
        [
            "",
            "## Alternate Selected-Record Variant Rows",
            "",
            "| Variant | SceneId | Record | Source addr | Runtime addr | Metadata | ZSI | Scene index | Coverage |",
            "| --- | --- | ---: | --- | --- | --- | --- | --- | --- |",
        ]
    )
    for row in payload["variant_rows"]:
        lines.append(
            f"| `{row['variant_index_hex']}` | `{row['scene_id_hex']}` | {row['record_index']} | "
            f"`{row['record_address_hex']}` | `{row['runtime_address_hex']}` | `{row['metadata_bytes_hex']}` | "
            f"`{row['zsi_path']}` | `{row.get('native_scene_index_symbol', '')}` | `{row['native_coverage_status']}` |"
        )

    if summary["metadata_b1_secondary_draw_config_mismatches"]:
        lines.extend(
            [
                "",
                "## Secondary Draw-Config Mismatches",
                "",
                "| SceneId | Metadata byte1 | N64 draw config | ZSI |",
                "| --- | ---: | --- | --- |",
            ]
        )
        for mismatch in summary["metadata_b1_secondary_draw_config_mismatches"]:
            lines.append(
                f"| `{mismatch['scene_id_hex']}` | {mismatch['metadata_b1']} | "
                f"`{mismatch.get('secondary_scene_draw_config', '')}` ({mismatch.get('secondary_scene_draw_config_index', '')}) | "
                f"`{mismatch['zsi_path']}` |"
            )

    lines.extend(
        [
            "",
            "## Notes",
            "",
            "- Scene ids `0x00..0x0D` are copied by OOT3D function `0x002EAFB4` from the normal or alternate selected-record source to the runtime table base.",
            "- Scene ids `0x0E..0x6E` are read from the static runtime tail.",
            "- Alternate selected records are exported as variants, not as default scene ids.",
            "- The 4-byte metadata trailer is preserved raw. Byte correlations against N64/SoH are reported only as secondary hypotheses until OOT3D consuming code paths are fully named.",
        ]
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines).rstrip() + "\n", encoding="utf-8")


def coverage_c_name(status: str) -> str:
    mapping = {
        "no_zsi_path": "OOT3D_SCENE_RESOURCE_NO_ZSI_PATH",
        "native_zsi_indexed": "OOT3D_SCENE_RESOURCE_NATIVE_ZSI_INDEXED",
        "native_zsi_not_indexed": "OOT3D_SCENE_RESOURCE_NATIVE_ZSI_NOT_INDEXED",
    }
    return mapping.get(status, "OOT3D_SCENE_RESOURCE_NO_ZSI_PATH")


def block_c_name(block: str) -> str:
    mapping = {
        "selected_normal": "OOT3D_SCENE_RESOURCE_BLOCK_SELECTED_NORMAL",
        "static_tail": "OOT3D_SCENE_RESOURCE_BLOCK_STATIC_TAIL",
        "selected_alternate": "OOT3D_SCENE_RESOURCE_BLOCK_SELECTED_ALTERNATE",
    }
    return mapping.get(block, "OOT3D_SCENE_RESOURCE_BLOCK_SELECTED_NORMAL")


def write_header(path: Path, payload: dict[str, Any]) -> None:
    lines = [
        "#ifndef OOT3D_SCENE_RESOURCE_H",
        "#define OOT3D_SCENE_RESOURCE_H",
        "",
        '#include "oot3d/types.h"',
        "",
        "enum {",
        f"    OOT3D_SCENE_RESOURCE_COPY_FUNCTION = 0x{RESOURCE_COPY_FUNCTION:08X},",
        f"    OOT3D_SCENE_RESOURCE_RUNTIME_TABLE_ADDRESS = 0x{RUNTIME_RESOURCE_TABLE:08X},",
        f"    OOT3D_SCENE_RESOURCE_SELECTED_NORMAL_SOURCE_ADDRESS = 0x{SELECTED_NORMAL_RESOURCE_SOURCE:08X},",
        f"    OOT3D_SCENE_RESOURCE_SELECTED_ALTERNATE_SOURCE_ADDRESS = 0x{SELECTED_ALTERNATE_RESOURCE_SOURCE:08X},",
        f"    OOT3D_SCENE_RESOURCE_STATIC_TAIL_TABLE_ADDRESS = 0x{STATIC_TAIL_RESOURCE_TABLE:08X},",
        f"    OOT3D_SCENE_RESOURCE_ENTRY_SIZE = 0x{RESOURCE_ENTRY_SIZE:02X},",
        f"    OOT3D_SCENE_RESOURCE_METADATA_OFFSET = 0x{RESOURCE_METADATA_OFFSET:02X},",
        f"    OOT3D_SCENE_RESOURCE_SELECTED_COPY_SIZE = 0x{SELECTED_COPY_SIZE:08X},",
        f"    OOT3D_SCENE_RESOURCE_SCENE_ROW_COUNT = {len(payload['rows'])},",
        f"    OOT3D_SCENE_RESOURCE_VARIANT_ROW_COUNT = {len(payload['variant_rows'])},",
        "};",
        "",
        "typedef enum {",
        "    OOT3D_SCENE_RESOURCE_BLOCK_SELECTED_NORMAL,",
        "    OOT3D_SCENE_RESOURCE_BLOCK_STATIC_TAIL,",
        "    OOT3D_SCENE_RESOURCE_BLOCK_SELECTED_ALTERNATE,",
        "} Oot3dSceneResourceBlock;",
        "",
        "typedef enum {",
        "    OOT3D_SCENE_RESOURCE_NO_ZSI_PATH,",
        "    OOT3D_SCENE_RESOURCE_NATIVE_ZSI_INDEXED,",
        "    OOT3D_SCENE_RESOURCE_NATIVE_ZSI_NOT_INDEXED,",
        "} Oot3dSceneResourceCoverageStatus;",
        "",
        "typedef struct {",
        "    u8 sceneId;",
        "    Oot3dSceneResourceBlock sourceBlock;",
        "    u16 recordIndex;",
        "    u32 recordAddress;",
        "    u32 runtimeAddress;",
        "    u32 metadataAddress;",
        "    u32 runtimeMetadataAddress;",
        "    u32 metadataRaw;",
        "    u8 metadataBytes[4];",
        "    const char* zsiPath;",
        "    const char* zarPath;",
        "    const char* nativeSceneSourceBasename;",
        "    const char* nativeSceneIndexSymbol;",
        "    const char* secondarySceneStem;",
        "    const char* secondarySceneEnum;",
        "    Oot3dSceneResourceCoverageStatus coverageStatus;",
        "} Oot3dSceneResourceRow;",
        "",
        "typedef struct {",
        "    u8 variantIndex;",
        "    u8 sceneId;",
        "    Oot3dSceneResourceBlock sourceBlock;",
        "    u16 recordIndex;",
        "    u32 recordAddress;",
        "    u32 runtimeAddress;",
        "    u32 metadataAddress;",
        "    u32 runtimeMetadataAddress;",
        "    u32 metadataRaw;",
        "    u8 metadataBytes[4];",
        "    const char* zsiPath;",
        "    const char* zarPath;",
        "    const char* nativeSceneSourceBasename;",
        "    const char* nativeSceneIndexSymbol;",
        "    Oot3dSceneResourceCoverageStatus coverageStatus;",
        "} Oot3dSceneResourceVariantRow;",
        "",
        "extern const Oot3dSceneResourceRow oot3d_scene_resource_rows[];",
        "extern const u32 oot3d_scene_resource_row_count;",
        "extern const Oot3dSceneResourceVariantRow oot3d_scene_resource_variant_rows[];",
        "extern const u32 oot3d_scene_resource_variant_row_count;",
        "",
        "#endif",
        "",
    ]
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def metadata_bytes_initializer(row: dict[str, Any]) -> str:
    return "{ " + ", ".join(f"0x{int(row[f'metadata_b{i}']):02X}u" for i in range(4)) + " }"


def write_source(path: Path, payload: dict[str, Any]) -> None:
    lines = [
        "/*",
        " * Generated from OOT3D code.bin scene resource records.",
        " * Secondary scene names are labels only; paths and metadata trailers are native OOT3D data.",
        " */",
        '#include "oot3d/scene_resource.h"',
        "",
        "const Oot3dSceneResourceRow oot3d_scene_resource_rows[] = {",
    ]
    for row in payload["rows"]:
        lines.append(
            "    { "
            f"0x{int(row['scene_id']):02X}u, "
            f"{block_c_name(str(row['source_block']))}, "
            f"{int(row['record_index'])}u, "
            f"0x{int(row['record_address']):08X}u, "
            f"0x{int(row['runtime_address']):08X}u, "
            f"0x{int(row['metadata_address']):08X}u, "
            f"0x{int(row['runtime_metadata_address']):08X}u, "
            f"0x{int(row['metadata_raw']):08X}u, "
            f"{metadata_bytes_initializer(row)}, "
            f"{c_string(str(row.get('zsi_path', '')))}, "
            f"{c_string(str(row.get('zar_path', '')))}, "
            f"{c_string(str(row.get('native_scene_source_basename', '')))}, "
            f"{c_string(str(row.get('native_scene_index_symbol', '')))}, "
            f"{c_string(str(row.get('secondary_scene_stem', '')))}, "
            f"{c_string(str(row.get('secondary_scene_enum', '')))}, "
            f"{coverage_c_name(str(row.get('native_coverage_status', '')))} "
            "},"
        )
    lines.extend(
        [
            "};",
            "",
            "const u32 oot3d_scene_resource_row_count = OOT3D_SCENE_RESOURCE_SCENE_ROW_COUNT;",
            "",
            "const Oot3dSceneResourceVariantRow oot3d_scene_resource_variant_rows[] = {",
        ]
    )
    for row in payload["variant_rows"]:
        lines.append(
            "    { "
            f"0x{int(row['variant_index']):02X}u, "
            f"0x{int(row['scene_id']):02X}u, "
            f"{block_c_name(str(row['source_block']))}, "
            f"{int(row['record_index'])}u, "
            f"0x{int(row['record_address']):08X}u, "
            f"0x{int(row['runtime_address']):08X}u, "
            f"0x{int(row['metadata_address']):08X}u, "
            f"0x{int(row['runtime_metadata_address']):08X}u, "
            f"0x{int(row['metadata_raw']):08X}u, "
            f"{metadata_bytes_initializer(row)}, "
            f"{c_string(str(row.get('zsi_path', '')))}, "
            f"{c_string(str(row.get('zar_path', '')))}, "
            f"{c_string(str(row.get('native_scene_source_basename', '')))}, "
            f"{c_string(str(row.get('native_scene_index_symbol', '')))}, "
            f"{coverage_c_name(str(row.get('native_coverage_status', '')))} "
            "},"
        )
    lines.extend(
        [
            "};",
            "",
            "const u32 oot3d_scene_resource_variant_row_count = OOT3D_SCENE_RESOURCE_VARIANT_ROW_COUNT;",
            "",
        ]
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    payload = build_report()
    write_json(DEFAULT_OUT_JSON, payload)
    write_csv(DEFAULT_OUT_CSV, payload["rows"])
    write_markdown(DEFAULT_OUT_MD, payload)
    write_header(DEFAULT_OUT_HEADER, payload)
    write_source(DEFAULT_OUT_SOURCE, payload)
    print(DEFAULT_OUT_JSON)
    print(DEFAULT_OUT_CSV)
    print(DEFAULT_OUT_MD)
    print(DEFAULT_OUT_HEADER)
    print(DEFAULT_OUT_SOURCE)
    print(json.dumps(payload["summary"], indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
