#!/usr/bin/env python3
"""Decode OOT3D global entrance entries referenced by native scene data.

This is a source-oriented extraction of the global entrance rows that are
reachable from currently decoded OOT3D scene exit data. The rows themselves are
read from OOT3D code.bin. Optional N64 decomp scene-table names are used only as secondary
labels for scene ids; native coverage is reported separately by checking those
labels against extracted OOT3D ZSI scene/setup entrance lists.
"""

from __future__ import annotations

import csv
import json
import re
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CODE_BIN = Path(r"E:\ppssppvr\oot3d_decomp\work\extract\exefs\code.bin")
DEFAULT_SCENE_INDEX = ROOT / "analysis" / "oot3d_native_scene_index_all_full.json"
DEFAULT_TRANSITION_TABLES = ROOT / "analysis" / "scene_exit_transition_tables.json"
DEFAULT_DIRECT_PLAYER_EVENT_TABLE = ROOT / "analysis" / "scene_cutscene_direct_player_event_table.json"
DEFAULT_SCENE_RESOURCE_TABLE = ROOT / "analysis" / "scene_resource_table.json"
DEFAULT_N64_SCENE_TABLE = ROOT / "reference_inputs" / "n64_scene_table.h"
DEFAULT_OUT_JSON = ROOT / "analysis" / "scene_global_entrance_table.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "scene_global_entrance_table.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "scene_global_entrance_table.md"
DEFAULT_OUT_HEADER = ROOT / "include" / "oot3d" / "scene_global_entrance.h"
DEFAULT_OUT_SOURCE = ROOT / "src" / "code" / "z_scene_global_entrance_table.c"

CODE_LOAD_BASE = 0x00100000
GLOBAL_ENTRANCE_TABLE = 0x00543BB8
GLOBAL_ENTRANCE_ENTRY_SIZE = 4
GLOBAL_ENTRANCE_EXTENT_ZERO_RUN = 16
KOKIRI_SLOT5_GLOBAL_ENTRANCE = 0x0211


def as_dict(value: Any) -> dict[str, Any]:
    return value if isinstance(value, dict) else {}


def as_list(value: Any) -> list[Any]:
    return value if isinstance(value, list) else []


def int_value(value: Any, default: int = 0) -> int:
    try:
        return int(value)
    except (TypeError, ValueError):
        return default


def s16(value: int) -> int:
    value &= 0xFFFF
    return value - 0x10000 if value & 0x8000 else value


def c_string(value: str) -> str:
    escaped = value.replace("\\", "\\\\").replace('"', '\\"')
    return f'"{escaped}"'


def c_identifier(value: str) -> str:
    identifier = re.sub(r"[^0-9A-Za-z_]+", "_", value).strip("_")
    if not identifier:
        return "unnamed"
    if identifier[0].isdigit():
        return f"_{identifier}"
    return identifier


def parse_secondary_scene_labels(path: Path) -> dict[int, dict[str, str]]:
    labels: dict[int, dict[str, str]] = {}
    if not path.is_file():
        return labels
    pattern = re.compile(
        r"/\*\s*0x(?P<scene_id>[0-9A-Fa-f]+)\s*\*/\s*"
        r"DEFINE_SCENE\((?P<scene_symbol>[^,]+),\s*[^,]+,\s*(?P<scene_enum>[^,]+),"
    )
    for line in path.read_text(encoding="utf-8", errors="ignore").splitlines():
        match = pattern.search(line)
        if match is None:
            continue
        scene_id = int(match.group("scene_id"), 16)
        scene_symbol = match.group("scene_symbol").strip()
        stem = scene_symbol[:-6] if scene_symbol.endswith("_scene") else scene_symbol
        labels[scene_id] = {
            "secondary_scene_symbol": scene_symbol,
            "secondary_scene_stem": stem,
            "secondary_scene_enum": match.group("scene_enum").strip(),
            "secondary_label_source": str(path),
        }
    return labels


def setup_entries(setup: dict[str, Any], command_id: int, key: str = "entries") -> list[dict[str, Any]]:
    for command in as_list(setup.get("commands")):
        if not isinstance(command, dict) or int_value(command.get("command_id"), -1) != command_id:
            continue
        decoded = as_dict(command.get("decoded"))
        if command_id in {0x00, 0x06}:
            selected = as_dict(decoded.get("selected_candidate"))
            return [entry for entry in as_list(selected.get("entries")) if isinstance(entry, dict)]
        return [entry for entry in as_list(decoded.get(key)) if isinstance(entry, dict)]
    return []


def scene_lookup(scene_index: dict[str, Any]) -> dict[str, list[dict[str, Any]]]:
    by_stem: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for record in as_list(scene_index.get("records")):
        if not isinstance(record, dict):
            continue
        stem = str(record.get("scene_stem", ""))
        if stem:
            by_stem[stem.lower()].append(record)
    return by_stem


def scene_lookup_by_path(scene_index: dict[str, Any]) -> dict[str, dict[str, Any]]:
    by_path: dict[str, dict[str, Any]] = {}
    for record in as_list(scene_index.get("records")):
        if not isinstance(record, dict):
            continue
        scene_path = str(record.get("scene_path", ""))
        if scene_path:
            by_path[scene_path.lower()] = record
    return by_path


def load_native_scene_resources(path: Path) -> dict[int, dict[str, Any]]:
    if not path.is_file():
        return {}
    payload = json.loads(path.read_text(encoding="utf-8"))
    rows: dict[int, dict[str, Any]] = {}
    for row in as_list(payload.get("rows")):
        if not isinstance(row, dict):
            continue
        scene_id = int_value(row.get("scene_id"), -1)
        if scene_id >= 0:
            rows[scene_id] = row
    return rows


def collect_referenced_entrances(
    scene_index: dict[str, Any],
    transition_tables: dict[str, Any],
    direct_player_event_table: dict[str, Any],
) -> tuple[dict[int, Counter[str]], dict[str, Any]]:
    references: dict[int, Counter[str]] = defaultdict(Counter)
    direct_negative: Counter[int] = Counter()
    direct_positive: Counter[int] = Counter()
    for record in as_list(scene_index.get("records")):
        if not isinstance(record, dict):
            continue
        for setup in as_list(record.get("setups")):
            if not isinstance(setup, dict):
                continue
            for command in as_list(setup.get("commands")):
                if not isinstance(command, dict) or int_value(command.get("command_id"), -1) != 0x13:
                    continue
                decoded = as_dict(command.get("decoded"))
                for exit_entry in as_list(decoded.get("values")):
                    if not isinstance(exit_entry, dict):
                        continue
                    if str(exit_entry.get("oot3d_exit_category")) != "direct_transition_value":
                        continue
                    raw = int_value(exit_entry.get("raw_u16", exit_entry.get("value", 0))) & 0xFFFF
                    signed = s16(raw)
                    if signed < 0:
                        direct_negative[raw] += 1
                        continue
                    direct_positive[signed] += 1
                    references[signed]["direct_exit"] += 1

    for row in as_list(transition_tables.get("verified_entrance_table_rows")):
        if not isinstance(row, dict):
            continue
        entrance = int_value(row.get("entrance"), -1)
        if entrance >= 0:
            references[entrance]["high_remap_exit"] += 1

    direct_player_event_transition_requests: Counter[int] = Counter()
    for row in as_list(direct_player_event_table.get("direct_player_event_rows")):
        if not isinstance(row, dict) or int_value(row.get("dispatch_resolved")) == 0:
            continue
        entrance = int_value(row.get("transition_request_index"), -1)
        if entrance >= 0:
            references[entrance]["cutscene_direct_player_event_transition_request"] += 1
            direct_player_event_transition_requests[entrance] += 1

    for row in as_list(direct_player_event_table.get("direct_player_event_dynamic_variant_rows")):
        if not isinstance(row, dict):
            continue
        entrance = int_value(row.get("transition_request_index"), -1)
        if entrance >= 0:
            references[entrance]["cutscene_direct_player_event_transition_request"] += 1
            direct_player_event_transition_requests[entrance] += 1

    references[KOKIRI_SLOT5_GLOBAL_ENTRANCE]["kokiri_slot5_fixture"] += 1
    stats = {
        "direct_positive_unique_count": len(direct_positive),
        "direct_negative_unique_count": len(direct_negative),
        "direct_negative_use_count": sum(direct_negative.values()),
        "cutscene_direct_player_event_transition_request_unique_count": len(direct_player_event_transition_requests),
        "cutscene_direct_player_event_transition_request_use_count": sum(direct_player_event_transition_requests.values()),
        "cutscene_direct_player_event_transition_request_indices": [
            {
                "entrance_index": entrance,
                "entrance_index_hex": f"0x{entrance:04x}",
                "use_count": count,
            }
            for entrance, count in sorted(direct_player_event_transition_requests.items())
        ],
        "direct_negative_values": [
            {"raw_u16": raw, "raw_u16_hex": f"0x{raw:04x}", "signed": s16(raw), "use_count": count}
            for raw, count in sorted(direct_negative.items())
        ],
    }
    return references, stats


def infer_global_entrance_table_extent(code: bytes) -> tuple[int, dict[str, Any]]:
    table_offset = GLOBAL_ENTRANCE_TABLE - CODE_LOAD_BASE
    max_count = (len(code) - table_offset) // GLOBAL_ENTRANCE_ENTRY_SIZE
    zero_run = 0
    for index in range(max_count):
        offset = table_offset + index * GLOBAL_ENTRANCE_ENTRY_SIZE
        raw = code[offset : offset + GLOBAL_ENTRANCE_ENTRY_SIZE]
        if raw == b"\x00\x00\x00\x00":
            zero_run += 1
            if zero_run >= GLOBAL_ENTRANCE_EXTENT_ZERO_RUN:
                run_start = index - zero_run + 1
                return run_start, {
                    "method": f"first_{GLOBAL_ENTRANCE_EXTENT_ZERO_RUN}_zero_entries",
                    "first_zero_run_start": run_start,
                    "first_zero_run_start_hex": f"0x{run_start:04x}",
                    "first_zero_run_end_exclusive": index + 1,
                    "first_zero_run_end_exclusive_hex": f"0x{index + 1:04x}",
                    "zero_run_length": GLOBAL_ENTRANCE_EXTENT_ZERO_RUN,
                }
        else:
            zero_run = 0
    return max_count, {
        "method": "code_image_end",
        "zero_run_length": GLOBAL_ENTRANCE_EXTENT_ZERO_RUN,
    }


def decode_global_entry(code: bytes, entrance_index: int, table_entry_count: int) -> dict[str, Any]:
    table_offset = GLOBAL_ENTRANCE_TABLE - CODE_LOAD_BASE
    offset = table_offset + entrance_index * GLOBAL_ENTRANCE_ENTRY_SIZE
    if entrance_index >= table_entry_count:
        return {
            "entrance_index": entrance_index,
            "status": "outside_inferred_global_entrance_table_extent",
            "table_file_offset": offset,
            "table_file_offset_hex": f"0x{offset:x}",
            "inferred_table_entry_count": table_entry_count,
        }
    if offset < 0 or offset + GLOBAL_ENTRANCE_ENTRY_SIZE > len(code):
        return {
            "entrance_index": entrance_index,
            "status": "outside_code_bin_image",
            "table_file_offset": offset,
            "table_file_offset_hex": f"0x{offset:x}",
        }
    raw = code[offset : offset + GLOBAL_ENTRANCE_ENTRY_SIZE]
    return {
        "entrance_index": entrance_index,
        "status": "decoded_from_code_bin",
        "table_file_offset": offset,
        "table_file_offset_hex": f"0x{offset:x}",
        "raw_hex": raw.hex(),
        "scene_id": raw[0],
        "scene_id_hex": f"0x{raw[0]:02x}",
        "local_entrance_index": raw[1],
        "field": int.from_bytes(raw[2:4], "little"),
        "field_hex": f"0x{int.from_bytes(raw[2:4], 'little'):04x}",
    }


def native_entrance_matches(
    decoded: dict[str, Any],
    scene_records: list[dict[str, Any]],
) -> list[dict[str, Any]]:
    if decoded.get("status") != "decoded_from_code_bin":
        return []
    local_index = int_value(decoded.get("local_entrance_index"), -1)
    matches: list[dict[str, Any]] = []
    for record in scene_records:
        scene_path = str(record.get("scene_path", ""))
        for setup in as_list(record.get("setups")):
            if not isinstance(setup, dict):
                continue
            setup_index = int_value(setup.get("index"), -1)
            entrances = setup_entries(setup, 0x06)
            if local_index < 0 or local_index >= len(entrances):
                continue
            entrance = entrances[local_index]
            spawn_index = int_value(entrance.get("spawn"), -1)
            spawns = setup_entries(setup, 0x00)
            spawn = spawns[spawn_index] if 0 <= spawn_index < len(spawns) else {}
            matches.append(
                {
                    "scene_path": scene_path,
                    "setup_index": setup_index,
                    "local_entrance_index": local_index,
                    "entrance_spawn": spawn_index,
                    "entrance_room": entrance.get("room"),
                    "spawn_resolved": bool(spawn),
                    "spawn_actor_name": spawn.get("actor_name", ""),
                    "spawn_pos": spawn.get("pos", []),
                    "spawn_rot": spawn.get("rot", []),
                    "spawn_params": spawn.get("params", ""),
                }
            )
    return matches


def validation_status(decoded: dict[str, Any], labels: dict[str, str], matches: list[dict[str, Any]]) -> str:
    if decoded.get("status") != "decoded_from_code_bin":
        return "outside_code_bin_image"
    if not labels:
        return "decoded_scene_id_unlabeled"
    if not labels.get("native_scene_path_candidates"):
        return "secondary_label_no_native_stem_match"
    if not matches:
        return "native_scene_candidate_local_entrance_uncovered"
    if any(match.get("spawn_resolved") for match in matches):
        if int_value(decoded.get("entrance_index"), -1) == KOKIRI_SLOT5_GLOBAL_ENTRANCE:
            return "native_kokiri_slot5_entrypoint_confirmed"
        return "native_scene_candidate_spawn_resolved"
    return "native_scene_candidate_local_entrance_covered"


def build_report() -> dict[str, Any]:
    code = DEFAULT_CODE_BIN.read_bytes()
    scene_index = json.loads(DEFAULT_SCENE_INDEX.read_text(encoding="utf-8"))
    transition_tables = json.loads(DEFAULT_TRANSITION_TABLES.read_text(encoding="utf-8"))
    direct_player_event_table = json.loads(DEFAULT_DIRECT_PLAYER_EVENT_TABLE.read_text(encoding="utf-8"))
    secondary_labels = parse_secondary_scene_labels(DEFAULT_N64_SCENE_TABLE)
    by_stem = scene_lookup(scene_index)
    by_path = scene_lookup_by_path(scene_index)
    native_scene_resources = load_native_scene_resources(DEFAULT_SCENE_RESOURCE_TABLE)
    references, reference_stats = collect_referenced_entrances(
        scene_index,
        transition_tables,
        direct_player_event_table,
    )
    max_code_image_entry_count = (len(code) - (GLOBAL_ENTRANCE_TABLE - CODE_LOAD_BASE)) // GLOBAL_ENTRANCE_ENTRY_SIZE
    table_entry_count, table_extent_evidence = infer_global_entrance_table_extent(code)

    rows: list[dict[str, Any]] = []
    unpromoted_references: list[dict[str, Any]] = []
    for entrance_index in sorted(references):
        decoded = decode_global_entry(code, entrance_index, table_entry_count)
        if decoded.get("status") != "decoded_from_code_bin":
            unpromoted_references.append(
                {
                    "entrance_index": entrance_index,
                    "entrance_index_hex": f"0x{entrance_index:04x}",
                    "references": dict(sorted(references[entrance_index].items())),
                    "reference_kinds": "; ".join(sorted(references[entrance_index])),
                    "reference_use_count": sum(references[entrance_index].values()),
                    "reason": str(decoded.get("status")),
                    "table_file_offset": decoded.get("table_file_offset"),
                    "table_file_offset_hex": decoded.get("table_file_offset_hex"),
                }
            )
            continue
        scene_id = int_value(decoded.get("scene_id"), -1)
        label = secondary_labels.get(scene_id, {})
        stem = str(label.get("secondary_scene_stem", ""))
        native_resource = native_scene_resources.get(scene_id, {})
        native_resource_zsi_path = str(native_resource.get("zsi_path", ""))
        native_candidates: list[dict[str, Any]] = []
        native_path_source = ""
        if native_resource_zsi_path:
            candidate = by_path.get(native_resource_zsi_path.lower())
            if candidate is not None:
                native_candidates = [candidate]
                native_path_source = "oot3d_code_bin_scene_resource_table"
            else:
                native_path_source = "oot3d_code_bin_scene_resource_table_unindexed"
        if not native_candidates and stem:
            native_candidates = by_stem.get(stem.lower(), [])
            native_path_source = "secondary_scene_stem_match" if native_candidates else native_path_source
        native_paths = [str(record.get("scene_path", "")) for record in native_candidates]
        label_payload = {
            **label,
            "native_scene_resource_zsi_path": native_resource_zsi_path,
            "native_scene_resource_source_block": native_resource.get("source_block", ""),
            "native_scene_source_basename": native_resource.get("native_scene_source_basename", ""),
            "native_scene_index_symbol": native_resource.get("native_scene_index_symbol", ""),
            "native_scene_index_registry_status": native_resource.get("native_scene_index_registry_status", ""),
            "native_scene_path_source": native_path_source,
            "native_scene_path_candidates": native_paths,
        }
        matches = native_entrance_matches(decoded, native_candidates)
        status = validation_status({"entrance_index": entrance_index, **decoded}, label_payload, matches)
        rows.append(
            {
                **decoded,
                "entrance_index_hex": f"0x{entrance_index:04x}",
                "references": dict(sorted(references[entrance_index].items())),
                "reference_kinds": "; ".join(sorted(references[entrance_index])),
                "reference_use_count": sum(references[entrance_index].values()),
                **label_payload,
                "native_validation_status": status,
                "native_match_count": len(matches),
                "native_spawn_resolved_count": sum(1 for match in matches if match.get("spawn_resolved")),
                "native_matches": matches,
            }
        )

    status_counts = Counter(str(row.get("native_validation_status", "")) for row in rows)
    scene_index_registry_counts = Counter(
        str(row.get("native_scene_index_registry_status", "") or "native_scene_index_missing") for row in rows
    )
    reference_kind_counts: Counter[str] = Counter()
    for ref_counter in references.values():
        for key, count in ref_counter.items():
            reference_kind_counts[key] += count
    unpromoted_reason_counts = Counter(str(row.get("reason", "")) for row in unpromoted_references)
    outside_code_image = [
        row for row in unpromoted_references if row.get("reason") == "outside_code_bin_image"
    ]
    outside_table_extent = [
        row
        for row in unpromoted_references
        if row.get("reason") == "outside_inferred_global_entrance_table_extent"
    ]
    summary = {
        "format": "oot3d_scene_global_entrance_table_v2",
        "global_entrance_table": f"0x{GLOBAL_ENTRANCE_TABLE:08x}",
        "global_entrance_table_file_offset": f"0x{GLOBAL_ENTRANCE_TABLE - CODE_LOAD_BASE:08x}",
        "entry_size": GLOBAL_ENTRANCE_ENTRY_SIZE,
        "code_image_max_entry_count": max_code_image_entry_count,
        "inferred_table_entry_count": table_entry_count,
        "inferred_table_entry_count_hex": f"0x{table_entry_count:04x}",
        "inferred_table_end_file_offset": f"0x{GLOBAL_ENTRANCE_TABLE - CODE_LOAD_BASE + table_entry_count * GLOBAL_ENTRANCE_ENTRY_SIZE:08x}",
        "inferred_table_extent_evidence": table_extent_evidence,
        "referenced_entrance_count": len(references),
        "decoded_referenced_entrance_count": len(rows),
        "unpromoted_referenced_entrance_count": len(unpromoted_references),
        "outside_inferred_table_extent_referenced_entrance_count": len(outside_table_extent),
        "outside_code_image_referenced_entrance_count": len(outside_code_image),
        "unpromoted_reason_counts": dict(sorted(unpromoted_reason_counts.items())),
        "reference_kind_counts": dict(sorted(reference_kind_counts.items())),
        "validation_status_counts": dict(sorted(status_counts.items())),
        "scene_index_registry_status_counts": dict(sorted(scene_index_registry_counts.items())),
        **reference_stats,
    }
    return {
        "summary": summary,
        "source_policy": {
            "native_code_source": str(DEFAULT_CODE_BIN),
            "native_asset_source": str(DEFAULT_SCENE_INDEX),
            "high_remap_source": str(DEFAULT_TRANSITION_TABLES),
            "cutscene_direct_player_event_source": str(DEFAULT_DIRECT_PLAYER_EVENT_TABLE),
            "native_scene_resource_source": str(DEFAULT_SCENE_RESOURCE_TABLE),
            "secondary_scene_label_source": str(DEFAULT_N64_SCENE_TABLE),
            "n64_policy": "N64/SoH scene_table is used only to label scene ids and as a fallback stem hint. Primary scene-id path resolution comes from OOT3D code.bin scene_resource_table rows.",
            "table_extent_policy": f"Promoted rows must fall before the native code.bin table extent inferred from the first run of {GLOBAL_ENTRANCE_EXTENT_ZERO_RUN} zero entries after the table start. Referenced values outside that extent are retained as unpromoted worklist entries, not emitted as EntranceInfo rows.",
        },
        "rows": rows,
        "unpromoted_references": unpromoted_references,
        "outside_code_image_references": outside_code_image,
        "outside_inferred_table_extent_references": outside_table_extent,
    }


CSV_COLUMNS = [
    "entrance_index",
    "entrance_index_hex",
    "reference_kinds",
    "reference_use_count",
    "raw_hex",
    "scene_id_hex",
    "local_entrance_index",
    "field_hex",
    "secondary_scene_stem",
    "secondary_scene_enum",
    "native_scene_resource_zsi_path",
    "native_scene_source_basename",
    "native_scene_index_symbol",
    "native_scene_index_registry_status",
    "native_scene_path_source",
    "native_validation_status",
    "native_match_count",
    "native_spawn_resolved_count",
    "native_scene_path_candidates",
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
    rows = payload["rows"]
    lines = [
        "# Scene Global Entrance Table",
        "",
        "Generated from OOT3D `code.bin` global entrance rows referenced by native OOT3D scene exits.",
        "N64/SoH scene-table names are secondary labels only; native validation is reported separately.",
        "",
        "## Summary",
        "",
        f"- Global entrance table: `{summary['global_entrance_table']}`",
        f"- File offset: `{summary['global_entrance_table_file_offset']}`",
        f"- Entry size: {summary['entry_size']}",
        f"- Code-image max entry count: {summary['code_image_max_entry_count']}",
        f"- Inferred table entry count: {summary['inferred_table_entry_count']} (`{summary['inferred_table_entry_count_hex']}`)",
        f"- Inferred table end file offset: `{summary['inferred_table_end_file_offset']}`",
        f"- Inferred extent evidence: `{json.dumps(summary['inferred_table_extent_evidence'], sort_keys=True)}`",
        f"- Referenced entrances: {summary['referenced_entrance_count']}",
        f"- Decoded referenced entrances: {summary['decoded_referenced_entrance_count']}",
        f"- Unpromoted referenced entrances: {summary['unpromoted_referenced_entrance_count']}",
        f"- Outside inferred table extent references: {summary['outside_inferred_table_extent_referenced_entrance_count']}",
        f"- Outside code image references: {summary['outside_code_image_referenced_entrance_count']}",
        f"- Unpromoted reasons: `{json.dumps(summary['unpromoted_reason_counts'], sort_keys=True)}`",
        f"- Reference kinds: `{json.dumps(summary['reference_kind_counts'], sort_keys=True)}`",
        f"- Validation statuses: `{json.dumps(summary['validation_status_counts'], sort_keys=True)}`",
        f"- Scene-index registry: `{json.dumps(summary['scene_index_registry_status_counts'], sort_keys=True)}`",
        f"- Cutscene direct-player transition requests: {summary['cutscene_direct_player_event_transition_request_unique_count']} unique / {summary['cutscene_direct_player_event_transition_request_use_count']} uses",
        f"- Direct signed-negative exit values not decoded as table indices: {summary['direct_negative_unique_count']} unique / {summary['direct_negative_use_count']} uses",
        "",
        "## Decoded Referenced Rows",
        "",
        "| Entrance | Refs | Raw | SceneId | Local | Field | Native scene | Scene index | Native validation | Native matches |",
        "| ---: | --- | --- | --- | ---: | --- | --- | --- | --- | ---: |",
    ]
    for row in rows:
        lines.append(
            f"| {row['entrance_index']} | `{row['reference_kinds']}` | `{row['raw_hex']}` | "
            f"`{row['scene_id_hex']}` | {row['local_entrance_index']} | `{row['field_hex']}` | "
            f"`{row.get('native_scene_resource_zsi_path', '')}` | `{row.get('native_scene_index_symbol', '')}` | "
            f"`{row['native_validation_status']}` | "
            f"{row['native_match_count']} |"
        )

    if payload["unpromoted_references"]:
        lines.extend(
            [
                "",
                "## Unpromoted Referenced Values",
                "",
                "| Entrance | Refs | Uses | References | Reason |",
                "| ---: | --- | ---: | --- | --- |",
            ]
        )
        for row in payload["unpromoted_references"]:
            lines.append(
                f"| {row['entrance_index']} | `{row['reference_kinds']}` | {row['reference_use_count']} | "
                f"`{json.dumps(row['references'], sort_keys=True)}` | `{row['reason']}` |"
            )

    lines.extend(
        [
            "",
            "## Remaining Gaps",
            "",
            "- The total semantic extent of the global entrance table is inferred from native `code.bin` layout, not from `code.bin` length. Referenced values outside that inferred extent are preserved as unpromoted worklist items.",
            "- Only rows referenced by decoded native scene exits/high-remap data and inside the inferred native table extent are emitted.",
            "- Resolved cutscene direct-player event transition requests are emitted through the same global-entrance table because `FUN_003716F0` writes the same pending entrance/index field used by exit transitions.",
            "- Scene id to ZSI path resolution now comes first from the OOT3D `code.bin` scene resource table. Secondary N64/SoH scene_table names remain labels/fallback hints only.",
            "- Decoded rows also carry the generated native scene-index source basename/symbol when the ZSI is present in `scene_index_registry.c`.",
            "- Signed-negative direct exit values are preserved in exit-list data but are not decoded as global entrance indices here.",
        ]
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines).rstrip() + "\n", encoding="utf-8")


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


def reference_flags(row: dict[str, Any]) -> str:
    refs = as_dict(row.get("references"))
    flags = []
    if refs.get("direct_exit"):
        flags.append("OOT3D_GLOBAL_ENTRANCE_REF_DIRECT_EXIT")
    if refs.get("high_remap_exit"):
        flags.append("OOT3D_GLOBAL_ENTRANCE_REF_HIGH_REMAP_EXIT")
    if refs.get("kokiri_slot5_fixture"):
        flags.append("OOT3D_GLOBAL_ENTRANCE_REF_KOKIRI_SLOT5_FIXTURE")
    if refs.get("cutscene_direct_player_event_transition_request"):
        flags.append("OOT3D_GLOBAL_ENTRANCE_REF_CUTSCENE_DIRECT_PLAYER_EVENT")
    return " | ".join(flags) if flags else "0"


def write_header(path: Path, payload: dict[str, Any]) -> None:
    lines = [
        "#ifndef OOT3D_SCENE_GLOBAL_ENTRANCE_H",
        "#define OOT3D_SCENE_GLOBAL_ENTRANCE_H",
        "",
        '#include "oot3d/types.h"',
        "",
        "enum {",
        f"    OOT3D_GLOBAL_ENTRANCE_TABLE_ADDRESS = 0x{GLOBAL_ENTRANCE_TABLE:08X},",
        f"    OOT3D_GLOBAL_ENTRANCE_ENTRY_SIZE = {GLOBAL_ENTRANCE_ENTRY_SIZE},",
        f"    OOT3D_GLOBAL_ENTRANCE_INFERRED_TABLE_ENTRY_COUNT = {int(payload['summary']['inferred_table_entry_count'])},",
        f"    OOT3D_GLOBAL_ENTRANCE_REFERENCED_ROW_COUNT = {len(payload['rows'])},",
        "};",
        "",
        "typedef enum {",
        "    OOT3D_GLOBAL_ENTRANCE_REF_DIRECT_EXIT = 1 << 0,",
        "    OOT3D_GLOBAL_ENTRANCE_REF_HIGH_REMAP_EXIT = 1 << 1,",
        "    OOT3D_GLOBAL_ENTRANCE_REF_KOKIRI_SLOT5_FIXTURE = 1 << 2,",
        "    OOT3D_GLOBAL_ENTRANCE_REF_CUTSCENE_DIRECT_PLAYER_EVENT = 1 << 3,",
        "} Oot3dGlobalEntranceReferenceFlags;",
        "",
        "typedef enum {",
        "    OOT3D_GLOBAL_ENTRANCE_OUTSIDE_CODE_BIN_IMAGE,",
        "    OOT3D_GLOBAL_ENTRANCE_OUTSIDE_INFERRED_TABLE_EXTENT,",
        "    OOT3D_GLOBAL_ENTRANCE_DECODED_SCENE_ID_UNLABELED,",
        "    OOT3D_GLOBAL_ENTRANCE_SECONDARY_LABEL_NO_NATIVE_STEM_MATCH,",
        "    OOT3D_GLOBAL_ENTRANCE_NATIVE_SCENE_CANDIDATE_LOCAL_ENTRANCE_UNCOVERED,",
        "    OOT3D_GLOBAL_ENTRANCE_NATIVE_SCENE_CANDIDATE_LOCAL_ENTRANCE_COVERED,",
        "    OOT3D_GLOBAL_ENTRANCE_NATIVE_SCENE_CANDIDATE_SPAWN_RESOLVED,",
        "    OOT3D_GLOBAL_ENTRANCE_NATIVE_KOKIRI_SLOT5_ENTRYPOINT_CONFIRMED,",
        "} Oot3dGlobalEntranceValidationStatus;",
        "",
        "typedef struct {",
        "    u16 entranceIndex;",
        "    u8 referenceFlags;",
        "    u8 sceneId;",
        "    u8 localEntranceIndex;",
        "    u16 field;",
        "    const char* secondarySceneStem;",
        "    const char* secondarySceneEnum;",
        "    const char* nativeScenePathCandidate;",
        "    const char* nativeSceneResourceZsiPath;",
        "    const char* nativeSceneSourceBasename;",
        "    const char* nativeSceneIndexSymbol;",
        "    Oot3dGlobalEntranceValidationStatus validationStatus;",
        "} Oot3dGlobalEntranceRow;",
        "",
        "extern const Oot3dGlobalEntranceRow oot3d_global_entrance_referenced_rows[];",
        "extern const u32 oot3d_global_entrance_referenced_row_count;",
        "",
        "const Oot3dGlobalEntranceRow* Oot3d_GlobalEntranceFindRow(",
        "    u16 entranceIndex",
        ");",
        "",
        "#endif",
        "",
    ]
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def write_source(path: Path, payload: dict[str, Any]) -> None:
    lines = [
        "/*",
        " * Generated from OOT3D code.bin global entrance table rows referenced by native scene exits.",
        " * Rows outside the inferred native table extent are retained in analysis output only.",
        " * Secondary scene names are labels only; validation status records native ZSI coverage.",
        " */",
        '#include "oot3d/scene_global_entrance.h"',
        "",
        "#include <stddef.h>",
        "",
        "const Oot3dGlobalEntranceRow oot3d_global_entrance_referenced_rows[] = {",
    ]
    for row in payload["rows"]:
        candidates = as_list(row.get("native_scene_path_candidates"))
        candidate = str(candidates[0]) if candidates else ""
        lines.append(
            "    { "
            f"{int(row['entrance_index'])}u, "
            f"{reference_flags(row)}, "
            f"0x{int(row['scene_id']):02X}u, "
            f"{int(row['local_entrance_index'])}u, "
            f"0x{int(row['field']):04X}u, "
            f"{c_string(str(row.get('secondary_scene_stem', '')))}, "
            f"{c_string(str(row.get('secondary_scene_enum', '')))}, "
            f"{c_string(candidate)}, "
            f"{c_string(str(row.get('native_scene_resource_zsi_path', '')))}, "
            f"{c_string(str(row.get('native_scene_source_basename', '')))}, "
            f"{c_string(str(row.get('native_scene_index_symbol', '')))}, "
            f"{validation_c_name(str(row.get('native_validation_status', '')))} "
            "},"
        )
    lines.extend(
        [
            "};",
            "",
        "const u32 oot3d_global_entrance_referenced_row_count = OOT3D_GLOBAL_ENTRANCE_REFERENCED_ROW_COUNT;",
        "",
        "const Oot3dGlobalEntranceRow* Oot3d_GlobalEntranceFindRow(",
        "    u16 entranceIndex",
        ") {",
        "    u32 rowIndex;",
        "",
        "    for (rowIndex = 0; rowIndex < oot3d_global_entrance_referenced_row_count; rowIndex++) {",
        "        const Oot3dGlobalEntranceRow* row = &oot3d_global_entrance_referenced_rows[rowIndex];",
        "",
        "        if (row->entranceIndex == entranceIndex) {",
        "            return row;",
        "        }",
        "    }",
        "",
        "    return NULL;",
        "}",
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
