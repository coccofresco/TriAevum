#!/usr/bin/env python3
"""Build a source-oriented OOT3D scene table from native decoded evidence.

This table is an integration layer over already-confirmed OOT3D artifacts:

- code.bin scene resource rows
- code.bin global entrance rows referenced by native scene exits
- generated native ZSI scene-index source files

It does not use N64 scene data as a source. N64-derived labels are carried only
when they already exist in the upstream scene-resource/global-entrance rows.
"""

from __future__ import annotations

import csv
import json
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SCENE_RESOURCE_TABLE = ROOT / "analysis" / "scene_resource_table.json"
DEFAULT_GLOBAL_ENTRANCE_TABLE = ROOT / "analysis" / "scene_global_entrance_table.json"
DEFAULT_SCENE_SOURCE_INDEX = ROOT / "analysis" / "oot3d_native_scene_source_index.json"
DEFAULT_OUT_JSON = ROOT / "analysis" / "scene_source_table.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "scene_source_table.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "scene_source_table.md"
DEFAULT_OUT_HEADER = ROOT / "include" / "oot3d" / "scene_source_table.h"
DEFAULT_OUT_SOURCE = ROOT / "src" / "code" / "z_scene_source_table.c"


def as_dict(value: Any) -> dict[str, Any]:
    return value if isinstance(value, dict) else {}


def as_list(value: Any) -> list[Any]:
    return value if isinstance(value, list) else []


def int_value(value: Any, default: int = 0) -> int:
    try:
        return int(value)
    except (TypeError, ValueError):
        return default


def c_string(value: str) -> str:
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


def source_index_by_path(payload: dict[str, Any]) -> dict[str, dict[str, Any]]:
    return {
        str(row.get("scene_path", "")).lower(): row
        for row in as_list(payload.get("scenes"))
        if isinstance(row, dict) and row.get("scene_path")
    }


def entrance_refs_by_scene(payload: dict[str, Any]) -> dict[int, list[dict[str, Any]]]:
    by_scene: dict[int, list[dict[str, Any]]] = defaultdict(list)
    for row in as_list(payload.get("rows")):
        if not isinstance(row, dict):
            continue
        scene_id = int_value(row.get("scene_id"), -1)
        if scene_id >= 0:
            by_scene[scene_id].append(row)
    for rows in by_scene.values():
        rows.sort(key=lambda item: int_value(item.get("entrance_index"), 0))
    return by_scene


def scene_ref_summary(entrance_refs: list[dict[str, Any]]) -> dict[str, Any]:
    status_counts = Counter(str(row.get("native_validation_status", "")) for row in entrance_refs)
    local_indices = sorted({int_value(row.get("local_entrance_index"), -1) for row in entrance_refs})
    return {
        "global_entrance_count": len(entrance_refs),
        "local_entrance_indices": [index for index in local_indices if index >= 0],
        "global_entrance_indices": [int_value(row.get("entrance_index"), 0) for row in entrance_refs],
        "global_entrance_indices_hex": [
            str(row.get("entrance_index_hex", f"0x{int_value(row.get('entrance_index'), 0):04x}"))
            for row in entrance_refs
        ],
        "entrance_validation_status_counts": dict(sorted(status_counts.items())),
    }


def source_binding(resource_row: dict[str, Any], source_index: dict[str, dict[str, Any]]) -> dict[str, Any]:
    zsi_path = str(resource_row.get("zsi_path", ""))
    source_row = source_index.get(zsi_path.lower(), {})
    return {
        "native_scene_source_status": "native_scene_source_resolved" if source_row else "native_scene_source_missing",
        "scene_stem": source_row.get("scene_stem", resource_row.get("secondary_scene_stem", "")),
        "source_basename": source_row.get("source_basename", resource_row.get("native_scene_source_basename", "")),
        "source_c_file": source_row.get("source_c_file", ""),
        "scene_index_symbol": source_row.get("scene_index_symbol", resource_row.get("native_scene_index_symbol", "")),
        "setup_symbol": source_row.get("setup_symbol", ""),
        "room_refs_symbol": source_row.get("room_refs_symbol", ""),
        "rooms_symbol": source_row.get("rooms_symbol", ""),
        "setup_count": int_value(source_row.get("setup_count"), 0),
        "room_count": int_value(source_row.get("room_count"), 0),
        "command_count": int_value(source_row.get("command_count"), 0),
        "unresolved_command_count": int_value(source_row.get("unresolved_command_count"), 0),
        "collision_candidate_count": int_value(source_row.get("collision_candidate_count"), 0),
        "room_actor_count": int_value(source_row.get("room_actor_count"), 0),
        "room_object_count": int_value(source_row.get("room_object_count"), 0),
        "payload_counts": source_row.get("payload_counts", ""),
        "support_levels": source_row.get("support_levels", ""),
    }


def build_scene_row(
    resource_row: dict[str, Any],
    source_index: dict[str, dict[str, Any]],
    scene_entrance_refs: list[dict[str, Any]],
    entrance_ref_start: int,
) -> dict[str, Any]:
    binding = source_binding(resource_row, source_index)
    ref_summary = scene_ref_summary(scene_entrance_refs)
    return {
        "scene_id": int_value(resource_row.get("scene_id"), 0),
        "scene_id_hex": resource_row.get("scene_id_hex", ""),
        "source_block": resource_row.get("source_block", ""),
        "resource_record_index": int_value(resource_row.get("record_index"), 0),
        "resource_record_address_hex": resource_row.get("record_address_hex", ""),
        "resource_runtime_address_hex": resource_row.get("runtime_address_hex", ""),
        "resource_metadata_bytes_hex": resource_row.get("metadata_bytes_hex", ""),
        "resource_metadata_raw_hex": resource_row.get("metadata_raw_hex", ""),
        "zsi_path": resource_row.get("zsi_path", ""),
        "zar_path": resource_row.get("zar_path", ""),
        "secondary_scene_stem": resource_row.get("secondary_scene_stem", ""),
        "secondary_scene_enum": resource_row.get("secondary_scene_enum", ""),
        "resource_coverage_status": resource_row.get("native_coverage_status", ""),
        "scene_index_registry_status": resource_row.get("native_scene_index_registry_status", ""),
        "entrance_ref_start": entrance_ref_start,
        **binding,
        **ref_summary,
    }


def build_variant_row(resource_row: dict[str, Any], source_index: dict[str, dict[str, Any]]) -> dict[str, Any]:
    binding = source_binding(resource_row, source_index)
    return {
        "variant_index": int_value(resource_row.get("variant_index"), 0),
        "variant_index_hex": resource_row.get("variant_index_hex", ""),
        "scene_id": int_value(resource_row.get("scene_id"), 0),
        "scene_id_hex": resource_row.get("scene_id_hex", ""),
        "source_block": resource_row.get("source_block", ""),
        "resource_record_index": int_value(resource_row.get("record_index"), 0),
        "resource_record_address_hex": resource_row.get("record_address_hex", ""),
        "resource_runtime_address_hex": resource_row.get("runtime_address_hex", ""),
        "resource_metadata_bytes_hex": resource_row.get("metadata_bytes_hex", ""),
        "resource_metadata_raw_hex": resource_row.get("metadata_raw_hex", ""),
        "zsi_path": resource_row.get("zsi_path", ""),
        "zar_path": resource_row.get("zar_path", ""),
        "resource_coverage_status": resource_row.get("native_coverage_status", ""),
        "scene_index_registry_status": resource_row.get("native_scene_index_registry_status", ""),
        **binding,
    }


def build_report() -> dict[str, Any]:
    resource_payload = json.loads(DEFAULT_SCENE_RESOURCE_TABLE.read_text(encoding="utf-8"))
    entrance_payload = json.loads(DEFAULT_GLOBAL_ENTRANCE_TABLE.read_text(encoding="utf-8"))
    source_payload = json.loads(DEFAULT_SCENE_SOURCE_INDEX.read_text(encoding="utf-8"))
    source_index = source_index_by_path(source_payload)
    entrance_by_scene = entrance_refs_by_scene(entrance_payload)

    entrance_refs: list[dict[str, Any]] = []
    rows: list[dict[str, Any]] = []
    for resource_row in as_list(resource_payload.get("rows")):
        if not isinstance(resource_row, dict):
            continue
        scene_id = int_value(resource_row.get("scene_id"), -1)
        scene_entrance_refs = entrance_by_scene.get(scene_id, [])
        ref_start = len(entrance_refs)
        for entrance in scene_entrance_refs:
            entrance_refs.append(
                {
                    "scene_id": scene_id,
                    "scene_id_hex": resource_row.get("scene_id_hex", ""),
                    "entrance_index": int_value(entrance.get("entrance_index"), 0),
                    "entrance_index_hex": entrance.get("entrance_index_hex", ""),
                    "local_entrance_index": int_value(entrance.get("local_entrance_index"), 0),
                    "field_hex": entrance.get("field_hex", ""),
                    "reference_kinds": entrance.get("reference_kinds", ""),
                    "native_validation_status": entrance.get("native_validation_status", ""),
                }
            )
        rows.append(build_scene_row(resource_row, source_index, scene_entrance_refs, ref_start))

    variant_rows = [
        build_variant_row(row, source_index)
        for row in as_list(resource_payload.get("variant_rows"))
        if isinstance(row, dict)
    ]

    source_counts = Counter(str(row.get("native_scene_source_status", "")) for row in rows)
    variant_source_counts = Counter(str(row.get("native_scene_source_status", "")) for row in variant_rows)
    entrance_count_distribution = Counter(int_value(row.get("global_entrance_count"), 0) for row in rows)
    rows_with_entrances = [row for row in rows if int_value(row.get("global_entrance_count"), 0) > 0]
    unresolved_command_rows = [row for row in rows if int_value(row.get("unresolved_command_count"), 0) > 0]

    summary = {
        "format": "oot3d_scene_source_table_v1",
        "scene_row_count": len(rows),
        "variant_row_count": len(variant_rows),
        "entrance_ref_count": len(entrance_refs),
        "scene_source_status_counts": dict(sorted(source_counts.items())),
        "variant_scene_source_status_counts": dict(sorted(variant_source_counts.items())),
        "scenes_with_global_entrances_count": len(rows_with_entrances),
        "scenes_without_global_entrances_count": len(rows) - len(rows_with_entrances),
        "entrance_count_distribution": {
            str(count): total for count, total in sorted(entrance_count_distribution.items())
        },
        "unresolved_command_scene_count": len(unresolved_command_rows),
        "resource_scene_row_count": int_value(resource_payload.get("summary", {}).get("scene_row_count"), 0),
        "global_entrance_decoded_referenced_count": int_value(
            entrance_payload.get("summary", {}).get("decoded_referenced_entrance_count"), 0
        ),
        "native_scene_source_index_scene_count": len(source_index),
    }
    return {
        "summary": summary,
        "source_policy": {
            "scene_resource_source": str(DEFAULT_SCENE_RESOURCE_TABLE),
            "global_entrance_source": str(DEFAULT_GLOBAL_ENTRANCE_TABLE),
            "native_scene_source_index": str(DEFAULT_SCENE_SOURCE_INDEX),
            "n64_policy": "No N64 source is read by this integration table. N64-derived labels are carried only when already present in the upstream native OOT3D artifacts.",
        },
        "rows": rows,
        "variant_rows": variant_rows,
        "entrance_refs": entrance_refs,
    }


CSV_COLUMNS = [
    "scene_id_hex",
    "scene_id",
    "zsi_path",
    "source_block",
    "source_basename",
    "scene_index_symbol",
    "source_c_file",
    "setup_count",
    "room_count",
    "command_count",
    "global_entrance_count",
    "global_entrance_indices_hex",
    "native_scene_source_status",
    "resource_coverage_status",
    "scene_index_registry_status",
]


def csv_value(value: Any) -> str:
    if isinstance(value, list):
        return "; ".join(str(item) for item in value)
    if isinstance(value, dict):
        return json.dumps(value, sort_keys=True)
    return str(value)


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
        "# Scene Source Table",
        "",
        "Generated by joining OOT3D-native scene resources, referenced global entrances, and generated ZSI scene-index sources.",
        "",
        "## Summary",
        "",
        f"- Scene rows: {summary['scene_row_count']}",
        f"- Variant rows: {summary['variant_row_count']}",
        f"- Entrance refs: {summary['entrance_ref_count']}",
        f"- Scene-source status: `{json.dumps(summary['scene_source_status_counts'], sort_keys=True)}`",
        f"- Variant scene-source status: `{json.dumps(summary['variant_scene_source_status_counts'], sort_keys=True)}`",
        f"- Scenes with global entrances: {summary['scenes_with_global_entrances_count']}",
        f"- Scenes without global entrances: {summary['scenes_without_global_entrances_count']}",
        f"- Entrance count distribution: `{json.dumps(summary['entrance_count_distribution'], sort_keys=True)}`",
        f"- Unresolved-command scenes: {summary['unresolved_command_scene_count']}",
        "",
        "## Scene Rows",
        "",
        "| SceneId | ZSI | Source symbol | Setups | Rooms | Commands | Global entrances | Status |",
        "| --- | --- | --- | ---: | ---: | ---: | --- | --- |",
    ]
    for row in payload["rows"]:
        lines.append(
            f"| `{row['scene_id_hex']}` | `{row['zsi_path']}` | `{row.get('scene_index_symbol', '')}` | "
            f"{row['setup_count']} | {row['room_count']} | {row['command_count']} | "
            f"`{'; '.join(row['global_entrance_indices_hex'])}` | `{row['native_scene_source_status']}` |"
        )

    lines.extend(
        [
            "",
            "## Variant Rows",
            "",
            "| Variant | SceneId | ZSI | Source symbol | Status |",
            "| --- | --- | --- | --- | --- |",
        ]
    )
    for row in payload["variant_rows"]:
        lines.append(
            f"| `{row['variant_index_hex']}` | `{row['scene_id_hex']}` | `{row['zsi_path']}` | "
            f"`{row.get('scene_index_symbol', '')}` | `{row['native_scene_source_status']}` |"
        )

    lines.extend(
        [
            "",
            "## Notes",
            "",
            "- This is a join table over native OOT3D evidence; it does not infer new gameplay behavior.",
            "- Rows missing a scene source correspond to resource rows whose ZSI is not present in the generated native scene-index registry.",
            "- `entrance_ref_start` and `global_entrance_count` in JSON/C point into the contiguous entrance-ref array.",
        ]
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines).rstrip() + "\n", encoding="utf-8")


def block_c_name(block: str) -> str:
    mapping = {
        "selected_normal": "OOT3D_SCENE_RESOURCE_BLOCK_SELECTED_NORMAL",
        "static_tail": "OOT3D_SCENE_RESOURCE_BLOCK_STATIC_TAIL",
        "selected_alternate": "OOT3D_SCENE_RESOURCE_BLOCK_SELECTED_ALTERNATE",
    }
    return mapping.get(block, "OOT3D_SCENE_RESOURCE_BLOCK_SELECTED_NORMAL")


def reference_flags_c(value: str) -> str:
    flags = []
    if "direct_exit" in value:
        flags.append("OOT3D_GLOBAL_ENTRANCE_REF_DIRECT_EXIT")
    if "high_remap_exit" in value:
        flags.append("OOT3D_GLOBAL_ENTRANCE_REF_HIGH_REMAP_EXIT")
    if "kokiri_slot5_fixture" in value:
        flags.append("OOT3D_GLOBAL_ENTRANCE_REF_KOKIRI_SLOT5_FIXTURE")
    return " | ".join(flags) if flags else "0"


def validation_c_name(status: str) -> str:
    mapping = {
        "outside_code_bin_image": "OOT3D_GLOBAL_ENTRANCE_OUTSIDE_CODE_BIN_IMAGE",
        "outside_inferred_global_entrance_table_extent": "OOT3D_GLOBAL_ENTRANCE_OUTSIDE_INFERRED_TABLE_EXTENT",
        "decoded_scene_id_unlabeled": "OOT3D_GLOBAL_ENTRANCE_DECODED_SCENE_ID_UNLABELED",
        "secondary_label_no_native_stem_match": "OOT3D_GLOBAL_ENTRANCE_SECONDARY_LABEL_NO_NATIVE_STEM_MATCH",
        "native_scene_candidate_local_entrance_uncovered": "OOT3D_GLOBAL_ENTRANCE_NATIVE_SCENE_CANDIDATE_LOCAL_ENTRANCE_UNCOVERED",
        "native_scene_candidate_local_entrance_covered": "OOT3D_GLOBAL_ENTRANCE_NATIVE_SCENE_CANDIDATE_LOCAL_ENTRANCE_COVERED",
        "native_scene_candidate_spawn_resolved": "OOT3D_GLOBAL_ENTRANCE_NATIVE_SCENE_CANDIDATE_SPAWN_RESOLVED",
        "native_kokiri_slot5_entrypoint_confirmed": "OOT3D_GLOBAL_ENTRANCE_NATIVE_KOKIRI_SLOT5_ENTRYPOINT_CONFIRMED",
    }
    return mapping.get(status, "OOT3D_GLOBAL_ENTRANCE_DECODED_SCENE_ID_UNLABELED")


def write_header(path: Path, payload: dict[str, Any]) -> None:
    lines = [
        "#ifndef OOT3D_SCENE_SOURCE_TABLE_H",
        "#define OOT3D_SCENE_SOURCE_TABLE_H",
        "",
        '#include "oot3d/scene_global_entrance.h"',
        '#include "oot3d/scene_resource.h"',
        "",
        "enum {",
        f"    OOT3D_SCENE_SOURCE_ROW_COUNT = {len(payload['rows'])},",
        f"    OOT3D_SCENE_SOURCE_VARIANT_ROW_COUNT = {len(payload['variant_rows'])},",
        f"    OOT3D_SCENE_SOURCE_ENTRANCE_REF_COUNT = {len(payload['entrance_refs'])},",
        "};",
        "",
        "typedef struct {",
        "    u16 entranceIndex;",
        "    u8 localEntranceIndex;",
        "    u8 referenceFlags;",
        "    Oot3dGlobalEntranceValidationStatus validationStatus;",
        "} Oot3dSceneSourceEntranceRef;",
        "",
        "typedef struct {",
        "    u8 sceneId;",
        "    Oot3dSceneResourceBlock resourceBlock;",
        "    u16 resourceRecordIndex;",
        "    const char* zsiPath;",
        "    const char* zarPath;",
        "    const char* sourceBasename;",
        "    const char* sourceCFile;",
        "    const char* sceneIndexSymbol;",
        "    const char* setupSymbol;",
        "    const char* roomRefsSymbol;",
        "    const char* roomsSymbol;",
        "    u16 setupCount;",
        "    u16 roomCount;",
        "    u16 commandCount;",
        "    u16 entranceRefStart;",
        "    u16 entranceRefCount;",
        "} Oot3dSceneSourceRow;",
        "",
        "typedef struct {",
        "    u8 variantIndex;",
        "    u8 sceneId;",
        "    Oot3dSceneResourceBlock resourceBlock;",
        "    u16 resourceRecordIndex;",
        "    const char* zsiPath;",
        "    const char* zarPath;",
        "    const char* sourceBasename;",
        "    const char* sourceCFile;",
        "    const char* sceneIndexSymbol;",
        "} Oot3dSceneSourceVariantRow;",
        "",
        "extern const Oot3dSceneSourceEntranceRef oot3d_scene_source_entrance_refs[];",
        "extern const Oot3dSceneSourceRow oot3d_scene_source_rows[];",
        "extern const Oot3dSceneSourceVariantRow oot3d_scene_source_variant_rows[];",
        "extern const u32 oot3d_scene_source_row_count;",
        "extern const u32 oot3d_scene_source_variant_row_count;",
        "extern const u32 oot3d_scene_source_entrance_ref_count;",
        "",
        "#endif",
        "",
    ]
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def write_source(path: Path, payload: dict[str, Any]) -> None:
    lines = [
        "/*",
        " * Generated source-oriented OOT3D scene table.",
        " * Joins native scene resources, global entrance rows, and generated ZSI scene-index sources.",
        " */",
        '#include "oot3d/scene_source_table.h"',
        "",
        "const Oot3dSceneSourceEntranceRef oot3d_scene_source_entrance_refs[] = {",
    ]
    for row in payload["entrance_refs"]:
        lines.append(
            "    { "
            f"{int(row['entrance_index'])}u, "
            f"{int(row['local_entrance_index'])}u, "
            f"{reference_flags_c(str(row.get('reference_kinds', '')))}, "
            f"{validation_c_name(str(row.get('native_validation_status', '')))} "
            "},"
        )
    lines.extend(
        [
            "};",
            "",
            "const Oot3dSceneSourceRow oot3d_scene_source_rows[] = {",
        ]
    )
    for row in payload["rows"]:
        lines.append(
            "    { "
            f"0x{int(row['scene_id']):02X}u, "
            f"{block_c_name(str(row.get('source_block', '')))}, "
            f"{int(row['resource_record_index'])}u, "
            f"{c_string(str(row.get('zsi_path', '')))}, "
            f"{c_string(str(row.get('zar_path', '')))}, "
            f"{c_string(str(row.get('source_basename', '')))}, "
            f"{c_string(str(row.get('source_c_file', '')))}, "
            f"{c_string(str(row.get('scene_index_symbol', '')))}, "
            f"{c_string(str(row.get('setup_symbol', '')))}, "
            f"{c_string(str(row.get('room_refs_symbol', '')))}, "
            f"{c_string(str(row.get('rooms_symbol', '')))}, "
            f"{int(row['setup_count'])}u, "
            f"{int(row['room_count'])}u, "
            f"{int(row['command_count'])}u, "
            f"{int(row['entrance_ref_start'])}u, "
            f"{int(row['global_entrance_count'])}u "
            "},"
        )
    lines.extend(
        [
            "};",
            "",
            "const Oot3dSceneSourceVariantRow oot3d_scene_source_variant_rows[] = {",
        ]
    )
    for row in payload["variant_rows"]:
        lines.append(
            "    { "
            f"0x{int(row['variant_index']):02X}u, "
            f"0x{int(row['scene_id']):02X}u, "
            f"{block_c_name(str(row.get('source_block', '')))}, "
            f"{int(row['resource_record_index'])}u, "
            f"{c_string(str(row.get('zsi_path', '')))}, "
            f"{c_string(str(row.get('zar_path', '')))}, "
            f"{c_string(str(row.get('source_basename', '')))}, "
            f"{c_string(str(row.get('source_c_file', '')))}, "
            f"{c_string(str(row.get('scene_index_symbol', '')))} "
            "},"
        )
    lines.extend(
        [
            "};",
            "",
            "const u32 oot3d_scene_source_row_count = OOT3D_SCENE_SOURCE_ROW_COUNT;",
            "const u32 oot3d_scene_source_variant_row_count = OOT3D_SCENE_SOURCE_VARIANT_ROW_COUNT;",
            "const u32 oot3d_scene_source_entrance_ref_count = OOT3D_SCENE_SOURCE_ENTRANCE_REF_COUNT;",
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
