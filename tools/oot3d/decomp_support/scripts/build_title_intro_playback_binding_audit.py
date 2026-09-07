#!/usr/bin/env python3
"""Bind title-intro QDB cues to the decoded OOT3D runtime record path."""

from __future__ import annotations

import json
from collections import defaultdict
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
ANALYSIS = ROOT / "analysis"

SOURCE_TABLE = ANALYSIS / "title_intro_source_table.json"
CONSUMER_AUDIT = ANALYSIS / "title_intro_player_action_consumer_audit.json"
RECORD_POINTER_AUDIT = ANALYSIS / "title_intro_record_pointer_audit.json"
CONTEXT_FEED_AUDIT = ANALYSIS / "title_intro_context_feed_audit.json"
PRIMARY_PRODUCER_AUDIT = ANALYSIS / "title_intro_primary_producer_audit.json"
RUNTIME_TABLES = ANALYSIS / "title_intro_runtime_tables.json"
PLAYBACK_RUNTIME_AUDIT = ANALYSIS / "title_intro_playback_runtime_audit.json"

OUT_JSON = ANALYSIS / "title_intro_playback_binding_audit.json"
OUT_MD = ANALYSIS / "title_intro_playback_binding_audit.md"

TARGET_CUES = [36, 37, 38]
REQUIRED_CONTEXT_FIELDS = {
    "record_source_byte",
    "mode_byte",
    "record_cursor_count",
    "record_c_byte0",
}


def read_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def rel(path: Path) -> str:
    try:
        return path.relative_to(ROOT).as_posix()
    except ValueError:
        return path.as_posix()


def group_qdb_rows(rows: list[dict[str, Any]]) -> dict[int, list[dict[str, Any]]]:
    grouped: dict[int, list[dict[str, Any]]] = defaultdict(list)
    for row in rows:
        grouped[int(row["cue_id"])].append(row)
    return dict(grouped)


def summarize_qdb_rows(rows: list[dict[str, Any]]) -> dict[str, Any]:
    qdb_names = sorted({str(row["qdb_embedded_name"]) for row in rows})
    qdb_indices = sorted({int(row["qdb_index"]) for row in rows})
    return {
        "row_count": len(rows),
        "qdb_indices": qdb_indices,
        "qdb_names": qdb_names,
        "first_frame": min(int(row["start_frame"]) for row in rows),
        "last_frame": max(int(row["end_frame"]) for row in rows),
        "total_duration_frames": sum(int(row["duration_frames"]) for row in rows),
        "positions": [
            {
                "player_action_index": int(row["player_action_index"]),
                "qdb_index": int(row["qdb_index"]),
                "frames": [int(row["start_frame"]), int(row["end_frame"])],
                "start": [int(row["start_x"]), int(row["start_y"]), int(row["start_z"])],
                "end": [int(row["end_x"]), int(row["end_y"]), int(row["end_z"])],
                "rot": [int(row["rot_x"]), int(row["rot_y"]), int(row["rot_z"])],
            }
            for row in rows
        ],
    }


def state_by_id(rows: list[dict[str, Any]]) -> dict[int, dict[str, Any]]:
    return {int(row["state"]): row for row in rows}


def field_by_name(rows: list[dict[str, Any]]) -> dict[str, dict[str, Any]]:
    return {str(row["name"]): row for row in rows}


def table_map_rows(runtime_tables: dict[str, Any]) -> list[dict[str, Any]]:
    native = runtime_tables["native_addresses"]
    table_sizes = {
        "history_other_modes": len(runtime_tables["tables"]["history_other_modes"]) * 8,
        "history_mode1": len(runtime_tables["tables"]["history_mode1"]) * 8,
        "compact_lookup": len(runtime_tables["tables"]["compact_lookup"]),
        "pattern_records": len(runtime_tables["tables"]["pattern_records"]) * 9,
        "fallback_zero_run": len(runtime_tables["tables"]["fallback_zero_run"]),
        "vector_lookup": len(runtime_tables["tables"]["vector_lookup_bits"]) * 4,
        "input_vector_prefix": len(runtime_tables["tables"]["input_vector_prefix"]),
        "global_flag_context_probe": len(runtime_tables["tables"]["global_flag_context_probe_prefix"]),
    }
    symbols = {
        "history_other_modes": "gOot3dTitleIntroHistoryOtherModeRecords",
        "history_mode1": "gOot3dTitleIntroHistoryMode1Records",
        "compact_lookup": "gOot3dTitleIntroCompactLookup",
        "pattern_records": "gOot3dTitleIntroPatternRecords",
        "fallback_zero_run": "gOot3dTitleIntroFallbackZeroRun",
        "vector_lookup": "gOot3dTitleIntroVectorLookupBits",
        "input_vector_prefix": "gOot3dTitleIntroInputVectorPrefix",
        "global_flag_context_probe": "gOot3dTitleIntroGlobalFlagContextProbePrefix",
    }
    return [
        {
            "key": key,
            "native_address": native[key],
            "byte_size": table_sizes[key],
            "source_symbol": symbols[key],
        }
        for key in symbols
    ]


def build_report() -> dict[str, Any]:
    source_table = read_json(SOURCE_TABLE)
    consumer = read_json(CONSUMER_AUDIT)
    record_pointer = read_json(RECORD_POINTER_AUDIT)
    context_feed = read_json(CONTEXT_FEED_AUDIT)
    primary = read_json(PRIMARY_PRODUCER_AUDIT)
    runtime_tables = read_json(RUNTIME_TABLES)
    playback_runtime = read_json(PLAYBACK_RUNTIME_AUDIT)

    qdb_rows = source_table["title_link_boy_player_action_rows"]
    grouped_rows = group_qdb_rows(qdb_rows)
    state_rows = state_by_id(consumer["state_rows"])
    context_fields = field_by_name(context_feed["fields"])
    record_c = next(row for row in record_pointer["record_table"] if row["record"] == "C")
    runtime_addresses = runtime_tables["native_addresses"]
    resolved_literals = primary["resolved_literals"]

    cue_bindings = []
    for cue_id in TARGET_CUES:
        state = state_rows.get(cue_id, {})
        cue_bindings.append(
            {
                "cue_id": cue_id,
                "cue_role": f"player_action_cue_{cue_id}",
                "qdb_summary": summarize_qdb_rows(grouped_rows.get(cue_id, [])),
                "direct_state": {
                    "state": state.get("state"),
                    "state_hex": state.get("state_hex"),
                    "handler": state.get("handler"),
                    "features": state.get("features"),
                    "calls": state.get("calls"),
                    "outgoing_targets": state.get("outgoing_targets"),
                },
                "runtime_record": {
                    "record": "C",
                    "address": record_c["address"],
                    "context_offset": record_c["context_offset"],
                    "known_bytes": record_c["known_bytes"],
                },
            }
        )

    record_feed_fields = [
        {
            "name": name,
            "offset": context_fields[name]["offset"],
            "target": context_fields[name]["target"],
            "feed": context_fields[name]["feed"],
            "writers": context_fields[name]["write_entries"],
            "readers": context_fields[name]["read_entries"],
        }
        for name in sorted(REQUIRED_CONTEXT_FIELDS)
    ]

    checks = {
        "source_table_ok": bool(source_table["summary"].get("title_link_boy_player_action_decoded")),
        "consumer_audit_ok": bool(consumer["summary"]["ok"]),
        "record_pointer_audit_ok": bool(record_pointer["summary"]["ok"]),
        "context_feed_audit_ok": bool(context_feed["summary"]["ok"]),
        "primary_producer_audit_ok": bool(primary["summary"]["checks"]),
        "runtime_tables_ok": bool(runtime_tables["summary"]["ok"]),
        "playback_runtime_audit_ok": bool(playback_runtime["summary"]["ok"]),
        "cue_ids_match_target": sorted(grouped_rows) == TARGET_CUES,
        "state_rows_match_target": sorted(state_rows) == TARGET_CUES,
        "record_c_matches_runtime_table": record_c["address"].lower() == runtime_addresses["record_c"].lower(),
        "primary_vector_lookup_matches_runtime_table": resolved_literals["vector_lookup_table"].lower()
        == runtime_addresses["vector_lookup"].lower(),
        "primary_input_vector_matches_runtime_table": resolved_literals["input_vector_bytes"].lower()
        == runtime_addresses["input_vector_prefix"].lower(),
        "primary_history_mode1_matches_runtime_table": resolved_literals["history_buffer_mode1"].lower()
        == runtime_addresses["history_mode1"].lower(),
        "primary_history_other_matches_runtime_table": resolved_literals["history_buffer_other_modes"].lower()
        == runtime_addresses["history_other_modes"].lower(),
        "required_context_fields_present": REQUIRED_CONTEXT_FIELDS.issubset(context_fields),
    }
    return {
        "format": "oot3d_title_intro_playback_binding_audit_v1",
        "inputs": {
            "source_table": rel(SOURCE_TABLE),
            "consumer_audit": rel(CONSUMER_AUDIT),
            "record_pointer_audit": rel(RECORD_POINTER_AUDIT),
            "context_feed_audit": rel(CONTEXT_FEED_AUDIT),
            "primary_producer_audit": rel(PRIMARY_PRODUCER_AUDIT),
            "runtime_tables": rel(RUNTIME_TABLES),
            "playback_runtime_audit": rel(PLAYBACK_RUNTIME_AUDIT),
        },
        "summary": {
            "ok": all(checks.values()),
            "checks": checks,
            "cue_ids": TARGET_CUES,
            "qdb_row_count": len(qdb_rows),
            "next_gate": "Bind the promoted title-intro playback runtime to the open-title scene setup, actor instances, camera/logo timeline, and slot-6 validation route.",
        },
        "identity": consumer["identity"],
        "cue_bindings": cue_bindings,
        "record_feed_fields": record_feed_fields,
        "runtime_tables": table_map_rows(runtime_tables),
        "source_selector": primary["source_selector"],
        "history_commit": primary["history_commit"],
        "unresolved": [
            "The QDB cue rows, direct states, record C, backing tables, record applicator, and producer-side dispatch helpers are now represented in maintained runtime code.",
            "The remaining milestone gap is orchestration of the first open-title scene: scene setup, actor instantiation, camera/logo timeline, and validation against the slot-6 emulator route.",
            "The payload allocator/resource internals behind descriptor 0x010004E0 remain broader engine integration work, but the title-intro helper call shape is no longer opaque.",
        ],
    }


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")


def write_json(path: Path, data: Any) -> None:
    write_text(path, json.dumps(data, indent=2) + "\n")


def write_markdown(path: Path, data: dict[str, Any]) -> None:
    lines = [
        "# OOT3D Title Intro Playback Binding Audit",
        "",
        "This audit binds the native title-intro QDB player-action rows to the decoded OOT3D player direct states, runtime record C, context feed fields, and promoted backing tables.",
        "",
        "## Summary",
        "",
        f"- OK: {data['summary']['ok']}",
        f"- Cue IDs: `{', '.join(str(value) for value in data['summary']['cue_ids'])}`",
        f"- QDB rows: `{data['summary']['qdb_row_count']}`",
        f"- Next gate: {data['summary']['next_gate']}",
        "",
        "## Checks",
        "",
        "| Check | Status |",
        "| --- | --- |",
    ]
    for key, value in data["summary"]["checks"].items():
        lines.append(f"| `{key}` | `{value}` |")

    lines.extend(
        [
            "",
            "## Cue Bindings",
            "",
            "| Cue | QDB rows | Frames | State | Handler | Record |",
            "| ---: | ---: | --- | ---: | --- | --- |",
        ]
    )
    for row in data["cue_bindings"]:
        qdb = row["qdb_summary"]
        state = row["direct_state"]
        record = row["runtime_record"]
        lines.append(
            f"| {row['cue_id']} | {qdb['row_count']} | {qdb['first_frame']}..{qdb['last_frame']} | "
            f"{state['state']} | `{state['handler']}` | `{record['address']}` {record['context_offset']} |"
        )

    lines.extend(
        [
            "",
            "## Record Feed Fields",
            "",
            "| Field | Offset | Target | Writers |",
            "| --- | --- | --- | --- |",
        ]
    )
    for field in data["record_feed_fields"]:
        lines.append(
            f"| `{field['name']}` | `{field['offset']}` | `{field['target']}` | `{';'.join(field['writers'])}` |"
        )

    lines.extend(
        [
            "",
            "## Runtime Tables",
            "",
            "| Key | Native address | Bytes | C symbol |",
            "| --- | --- | ---: | --- |",
        ]
    )
    for table in data["runtime_tables"]:
        lines.append(
            f"| `{table['key']}` | `{table['native_address']}` | {table['byte_size']} | `{table['source_symbol']}` |"
        )

    lines.extend(["", "## Unresolved", ""])
    for item in data["unresolved"]:
        lines.append(f"- {item}")
    write_text(OUT_MD, "\n".join(lines) + "\n")


def main() -> None:
    data = build_report()
    write_json(OUT_JSON, data)
    write_markdown(OUT_MD, data)
    if not data["summary"]["ok"]:
        raise SystemExit("title-intro playback binding audit failed")


if __name__ == "__main__":
    main()
