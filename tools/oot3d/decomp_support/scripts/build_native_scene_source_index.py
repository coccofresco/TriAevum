#!/usr/bin/env python3
"""Build a source-oriented index from decoded native OOT3D scene data.

The full ZSI scene export already contains the original OOT3D asset payloads
and code.bin evidence. This script reshapes that data into reviewable tables
that answer the N64-source-style question: which scene/setup/room file defines
which actors, entrances, transitions, paths, lights, exits, and handler-backed
commands.
"""

from __future__ import annotations

import csv
import json
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_INDEX = ROOT / "analysis" / "oot3d_native_scene_index_all_full.json"
DEFAULT_OUT_JSON = ROOT / "analysis" / "oot3d_native_scene_source_index.json"
DEFAULT_OUT_MD = ROOT / "analysis" / "oot3d_native_scene_source_index.md"
DEFAULT_SCENE_CSV = ROOT / "analysis" / "oot3d_native_scene_source_index_scenes.csv"
DEFAULT_SETUP_CSV = ROOT / "analysis" / "oot3d_native_scene_source_index_setups.csv"
DEFAULT_ROOM_CSV = ROOT / "analysis" / "oot3d_native_scene_source_index_rooms.csv"
DEFAULT_COMMAND_CSV = ROOT / "analysis" / "oot3d_native_scene_source_index_commands.csv"
DEFAULT_SOURCE_ROOT = Path("tools/oot3d/decomp_support/src/assets/scenes")

COMMAND_PAYLOAD_SUFFIX = {
    0x00: "spawns",
    0x01: "standard_actors",
    0x06: "entrances",
    0x07: "special_files",
    0x0D: "paths",
    0x0E: "transition_actors",
    0x0F: "light_settings",
    0x11: "skybox_settings",
    0x13: "exits",
    0x15: "sound_settings",
    0x17: "cutscenes",
    0x19: "misc_settings",
}

PAYLOAD_COUNT_FIELDS = {
    "special_files": "specialFileCount",
    "paths": "pathCount",
    "standard_actors": "standardActorCount",
    "spawns": "spawnCount",
    "entrances": "entranceCount",
    "transition_actors": "transitionCount",
    "light_settings": "lightSettingsCount",
    "exits": "exitCount",
    "skybox_settings": "skyboxSettingsCount",
    "sound_settings": "soundSettingsCount",
    "cutscenes": "cutsceneCount",
    "misc_settings": "miscSettingsCount",
}

SOURCE_SUPPORT_CLOSED = {
    "code_bin_confirmed",
    "code_bin_handler_confirmed",
    "native_control_marker",
}


def c_identifier(value: str) -> str:
    normalized = []
    for char in value.lower():
        normalized.append(char if char.isalnum() else "_")
    result = "".join(normalized).strip("_")
    while "__" in result:
        result = result.replace("__", "_")
    return result or "unknown"


def source_basename(scene_path: str) -> str:
    return Path(scene_path).stem


def scene_prefix(scene_path: str) -> str:
    return f"oot3d_{c_identifier(source_basename(scene_path))}"


def scene_index_symbol(scene_path: str) -> str:
    return f"oot3d_scene_index_{c_identifier(source_basename(scene_path))}"


def source_c_file(scene_path: str) -> str:
    return str(DEFAULT_SOURCE_ROOT / f"{source_basename(scene_path)}.c")


def setup_symbol(scene_path: str) -> str:
    return f"{scene_prefix(scene_path)}_setups"


def setup_payload_symbol(scene_path: str, setup_index: int, suffix: str) -> str:
    return f"{scene_prefix(scene_path)}_setup_{setup_index}_{suffix}"


def room_actor_symbol(scene_path: str, room_path: str) -> str:
    return f"{scene_prefix(scene_path)}_{c_identifier(source_basename(room_path))}_actors"


def room_object_symbol(scene_path: str, room_path: str) -> str:
    return f"{scene_prefix(scene_path)}_{c_identifier(source_basename(room_path))}_objects"


def as_list(value: Any) -> list[Any]:
    return value if isinstance(value, list) else []


def as_dict(value: Any) -> dict[str, Any]:
    return value if isinstance(value, dict) else {}


def int_value(value: Any, default: int = 0) -> int:
    try:
        return int(value)
    except (TypeError, ValueError):
        return default


def selected_candidate(decoded: dict[str, Any]) -> dict[str, Any]:
    return as_dict(decoded.get("selected_candidate"))


def selected_entry_count(decoded: dict[str, Any]) -> int:
    selected = selected_candidate(decoded)
    if selected:
        return int_value(selected.get("entry_count"))
    return 0


def decoded_payload_count(command_id: int, decoded: dict[str, Any]) -> int:
    if command_id in {0x00, 0x01, 0x06}:
        return selected_entry_count(decoded)
    if command_id == 0x04:
        return len(as_list(decoded.get("room_references")))
    if command_id == 0x07:
        return 1 if decoded.get("status") == "decoded_special_files" else 0
    if command_id == 0x0D:
        return len(as_list(decoded.get("raw_records")))
    if command_id == 0x0E:
        return int_value(decoded.get("entry_count"))
    if command_id == 0x0F:
        return int_value(decoded.get("record_count"))
    if command_id == 0x11:
        return 1 if decoded.get("status") == "decoded_skybox_settings" else 0
    if command_id == 0x13:
        return len(as_list(decoded.get("values")))
    if command_id == 0x15:
        return 1 if decoded.get("status") == "decoded_sound_settings" else 0
    if command_id == 0x17:
        return 1 if decoded.get("status") == "decoded_cutscene_reference" else 0
    if command_id == 0x19:
        return 1 if decoded.get("status") == "decoded_misc_settings" else 0
    return 0


def command_payload_symbol(scene_path: str, setup_index: int, command_id: int, decoded_count: int) -> str:
    if command_id == 0x04 and decoded_count:
        return f"{scene_prefix(scene_path)}_room_refs"
    suffix = COMMAND_PAYLOAD_SUFFIX.get(command_id)
    if suffix and decoded_count:
        return setup_payload_symbol(scene_path, setup_index, suffix)
    return ""


def join_counts(counter: Counter[str] | dict[str, int]) -> str:
    return "; ".join(f"{key}:{value}" for key, value in sorted(counter.items()))


def join_unique(values: list[str], limit: int | None = None) -> str:
    seen: set[str] = set()
    result: list[str] = []
    for value in values:
        if value and value not in seen:
            seen.add(value)
            result.append(value)
            if limit is not None and len(result) >= limit:
                break
    return "; ".join(result)


def room_selected_payload(room: dict[str, Any]) -> tuple[dict[str, Any], dict[str, Any]]:
    payload = as_dict(room.get("room_actor_list"))
    return payload, as_dict(payload.get("selected_candidate"))


def room_object_names(selected: dict[str, Any]) -> list[str]:
    prefix = as_dict(selected.get("object_prefix"))
    names = []
    for key in ("object_ids", "unknown_object_ids"):
        for entry in as_list(prefix.get(key)):
            if not isinstance(entry, dict):
                continue
            name = str(entry.get("object_name") or "")
            if name and name != "OBJECT_NONE":
                names.append(name)
    return names


def room_actor_names(selected: dict[str, Any]) -> list[str]:
    counts = selected.get("actor_name_counts")
    if isinstance(counts, dict):
        return [str(key) for key in sorted(counts)]
    names = []
    for entry in as_list(selected.get("entries")):
        if isinstance(entry, dict):
            names.append(str(entry.get("actor_name") or ""))
    return names


def command_record(
    scene_path: str,
    scene_stem: str,
    setup_index: int,
    command_index: int,
    command: dict[str, Any],
) -> dict[str, Any]:
    command_id = int_value(command.get("command_id"), -1)
    decoded = as_dict(command.get("decoded"))
    evidence = as_dict(command.get("decompilation_evidence"))
    support_level = str(evidence.get("support_level", "unmapped_command"))
    decoded_status = str(decoded.get("status", "not_decoded"))
    decoded_count = decoded_payload_count(command_id, decoded)
    return {
        "scene_path": scene_path,
        "scene_stem": scene_stem,
        "source_basename": source_basename(scene_path),
        "setup_index": setup_index,
        "command_index": command_index,
        "command_id": command_id,
        "command_id_hex": str(command.get("command_id_hex", f"0x{command_id:02x}")),
        "command_name": str(command.get("command_name", "unknown")),
        "offset_hex": str(command.get("offset_hex", "")),
        "argument_hex": str(command.get("argument_hex", "")),
        "parameter": int_value(command.get("parameter")),
        "decoded_status": decoded_status,
        "decoded_count": decoded_count,
        "decoded_expected_count": int_value(decoded.get("expected_count"), decoded_count),
        "support_level": support_level,
        "payload_symbol": command_payload_symbol(scene_path, setup_index, command_id, decoded_count),
        "export_structs": join_unique([str(value) for value in as_list(evidence.get("export_structs"))]),
        "open_questions": join_unique([str(value) for value in as_list(evidence.get("open_questions"))]),
    }


def setup_record(
    record: dict[str, Any],
    setup: dict[str, Any],
    command_rows: list[dict[str, Any]],
) -> dict[str, Any]:
    scene_path = str(record.get("scene_path", ""))
    setup_index = int_value(setup.get("index"), -1)
    payload_counts: Counter[str] = Counter()
    support_counts: Counter[str] = Counter()
    decoded_status_counts: Counter[str] = Counter()
    command_ids = []

    for row in command_rows:
        if row["scene_path"] != scene_path or row["setup_index"] != setup_index:
            continue
        command_id = int_value(row.get("command_id"), -1)
        suffix = COMMAND_PAYLOAD_SUFFIX.get(command_id)
        if suffix:
            payload_counts[suffix] += int_value(row.get("decoded_count"))
        support_counts[str(row.get("support_level", ""))] += 1
        decoded_status_counts[str(row.get("decoded_status", ""))] += 1
        command_ids.append(str(row.get("command_id_hex", "")))

    unresolved = sum(
        count
        for level, count in support_counts.items()
        if level and level not in SOURCE_SUPPORT_CLOSED
    )
    row = {
        "scene_path": scene_path,
        "scene_stem": str(record.get("scene_stem", "")),
        "source_basename": source_basename(scene_path),
        "source_c_file": source_c_file(scene_path),
        "setup_index": setup_index,
        "setup_role": str(setup.get("setup_role", "")),
        "setup_symbol": setup_symbol(scene_path),
        "command_count": int_value(setup.get("command_count"), len(as_list(setup.get("commands")))),
        "command_ids": "; ".join(command_ids),
        "support_levels": join_counts(support_counts),
        "decoded_statuses": join_counts(decoded_status_counts),
        "unresolved_command_count": unresolved,
        "payload_counts": join_counts(payload_counts),
    }
    for suffix, field_name in PAYLOAD_COUNT_FIELDS.items():
        row[field_name] = payload_counts.get(suffix, 0)
        row[f"{suffix}_symbol"] = setup_payload_symbol(scene_path, setup_index, suffix) if payload_counts.get(suffix, 0) else ""
    return row


def room_record(record: dict[str, Any], room: dict[str, Any]) -> dict[str, Any]:
    scene_path = str(record.get("scene_path", ""))
    room_path = str(room.get("room_path", ""))
    payload, selected = room_selected_payload(room)
    object_names = room_object_names(selected)
    actor_names = room_actor_names(selected)
    return {
        "scene_path": scene_path,
        "scene_stem": str(record.get("scene_stem", "")),
        "source_basename": source_basename(scene_path),
        "source_c_file": source_c_file(scene_path),
        "room_path": room_path,
        "room_index": int_value(room.get("room_index"), -1),
        "room_size": int_value(room.get("room_size")),
        "room_actor_symbol": room_actor_symbol(scene_path, room_path) if selected else "",
        "room_object_symbol": room_object_symbol(scene_path, room_path) if object_names else "",
        "actor_list_status": str(payload.get("status", "missing")),
        "actor_list_confidence": str(selected.get("confidence", "")),
        "actor_count": int_value(selected.get("entry_count")),
        "object_count": len(object_names),
        "unknown_object_id_count": int_value(selected.get("unknown_object_id_count")),
        "object_names": join_unique(object_names),
        "actor_names": join_unique(actor_names),
        "embedded_cmb_count": int_value(room.get("embedded_cmb_count")),
    }


def build_index(index: dict[str, Any]) -> dict[str, Any]:
    scene_rows: list[dict[str, Any]] = []
    setup_rows: list[dict[str, Any]] = []
    room_rows: list[dict[str, Any]] = []
    command_rows: list[dict[str, Any]] = []
    command_matrix: dict[str, dict[str, Any]] = {}
    support_counts: Counter[str] = Counter()
    decoded_status_counts: Counter[str] = Counter()
    payload_totals: Counter[str] = Counter()
    path_point_status_counts: Counter[str] = Counter()

    for record in as_list(index.get("records")):
        if not isinstance(record, dict):
            continue
        scene_path = str(record.get("scene_path", ""))
        scene_stem = str(record.get("scene_stem", ""))
        scene_support_counts: Counter[str] = Counter()
        scene_payload_counts: Counter[str] = Counter()
        scene_command_count = 0

        for setup in as_list(record.get("setups")):
            if not isinstance(setup, dict):
                continue
            setup_index = int_value(setup.get("index"), -1)
            for command_index, command in enumerate(as_list(setup.get("commands"))):
                if not isinstance(command, dict):
                    continue
                row = command_record(scene_path, scene_stem, setup_index, command_index, command)
                command_rows.append(row)
                scene_command_count += 1
                support_level = str(row["support_level"])
                decoded_status = str(row["decoded_status"])
                support_counts[support_level] += 1
                decoded_status_counts[decoded_status] += 1
                scene_support_counts[support_level] += 1

                suffix = COMMAND_PAYLOAD_SUFFIX.get(int_value(row["command_id"], -1))
                if suffix:
                    count = int_value(row["decoded_count"])
                    payload_totals[suffix] += count
                    scene_payload_counts[suffix] += count
                if int_value(row["command_id"], -1) == 0x0D:
                    decoded = as_dict(command.get("decoded"))
                    for path_record in as_list(decoded.get("raw_records")):
                        if isinstance(path_record, dict):
                            path_point_status_counts[str(path_record.get("points_status", "unknown"))] += 1

                matrix_key = str(row["command_id_hex"])
                matrix = command_matrix.setdefault(
                    matrix_key,
                    {
                        "command_id_hex": matrix_key,
                        "command_name": row["command_name"],
                        "command_count": 0,
                        "scene_count": 0,
                        "setup_count": 0,
                        "decoded_payload_total": 0,
                        "support_levels": Counter(),
                        "decoded_statuses": Counter(),
                        "scenes": set(),
                        "setups": set(),
                    },
                )
                matrix["command_count"] += 1
                matrix["decoded_payload_total"] += int_value(row["decoded_count"])
                matrix["support_levels"][support_level] += 1
                matrix["decoded_statuses"][decoded_status] += 1
                matrix["scenes"].add(scene_path)
                matrix["setups"].add(f"{scene_path}#{setup_index}")

        for setup in as_list(record.get("setups")):
            if isinstance(setup, dict):
                setup_rows.append(setup_record(record, setup, command_rows))

        for room in as_list(record.get("rooms")):
            if isinstance(room, dict):
                room_rows.append(room_record(record, room))

        unresolved = sum(
            count
            for level, count in scene_support_counts.items()
            if level and level not in SOURCE_SUPPORT_CLOSED
        )
        actor_count = sum(
            int_value(room.get("actor_count"))
            for room in room_rows
            if room.get("scene_path") == scene_path
        )
        object_count = sum(
            int_value(room.get("object_count"))
            for room in room_rows
            if room.get("scene_path") == scene_path
        )
        scene_rows.append(
            {
                "scene_path": scene_path,
                "scene_stem": scene_stem,
                "source_basename": source_basename(scene_path),
                "source_c_file": source_c_file(scene_path),
                "scene_index_symbol": scene_index_symbol(scene_path),
                "setup_symbol": setup_symbol(scene_path),
                "room_refs_symbol": f"{scene_prefix(scene_path)}_room_refs",
                "rooms_symbol": f"{scene_prefix(scene_path)}_rooms",
                "scene_size": int_value(record.get("scene_size")),
                "setup_count": len(as_list(record.get("setups"))),
                "room_count": len(as_list(record.get("rooms"))),
                "command_count": scene_command_count,
                "room_actor_count": actor_count,
                "room_object_count": object_count,
                "support_levels": join_counts(scene_support_counts),
                "unresolved_command_count": unresolved,
                "payload_counts": join_counts(scene_payload_counts),
                "collision_candidate_count": len(as_list(record.get("collision_header_candidates"))),
            }
        )

    command_matrix_rows = []
    for key in sorted(command_matrix, key=lambda value: int(value, 16)):
        row = command_matrix[key]
        support_counter = row["support_levels"]
        decoded_counter = row["decoded_statuses"]
        command_matrix_rows.append(
            {
                "command_id_hex": key,
                "command_name": row["command_name"],
                "command_count": row["command_count"],
                "scene_count": len(row["scenes"]),
                "setup_count": len(row["setups"]),
                "decoded_payload_total": row["decoded_payload_total"],
                "support_levels": join_counts(support_counter),
                "decoded_statuses": join_counts(decoded_counter),
                "open_state": "closed"
                if all(level in SOURCE_SUPPORT_CLOSED for level in support_counter)
                else "semantic_or_consumer_pending",
            }
        )

    summary = {
        "format": "oot3d_native_scene_source_index_v1",
        "source_index": str(DEFAULT_INDEX),
        "scene_count": len(scene_rows),
        "setup_count": len(setup_rows),
        "room_binding_count": len(room_rows),
        "unique_room_file_count": int_value(index.get("unique_room_file_count")),
        "available_unique_room_file_count": int_value(index.get("available_unique_room_file_count")),
        "command_count": len(command_rows),
        "room_actor_count": sum(int_value(row.get("actor_count")) for row in room_rows),
        "room_object_count": sum(int_value(row.get("object_count")) for row in room_rows),
        "payload_totals": dict(sorted(payload_totals.items())),
        "path_point_status_counts": dict(sorted(path_point_status_counts.items())),
        "support_level_counts": dict(sorted(support_counts.items())),
        "decoded_status_counts": dict(sorted(decoded_status_counts.items())),
        "unresolved_command_count": sum(
            count for level, count in support_counts.items() if level not in SOURCE_SUPPORT_CLOSED
        ),
        "generated_outputs": {
            "json": str(DEFAULT_OUT_JSON),
            "markdown": str(DEFAULT_OUT_MD),
            "scene_csv": str(DEFAULT_SCENE_CSV),
            "setup_csv": str(DEFAULT_SETUP_CSV),
            "room_csv": str(DEFAULT_ROOM_CSV),
            "command_csv": str(DEFAULT_COMMAND_CSV),
        },
        "provenance": {
            "asset_source": str(index.get("scene_root", "")),
            "c_output_dir": str(index.get("c_output_dir", "")),
            "source_policy": index.get("source_policy", ""),
            "n64_policy": "semantic labels only; rows are derived from OOT3D ZSI and OOT3D code.bin evidence",
        },
    }

    return {
        "summary": summary,
        "scenes": scene_rows,
        "setups": setup_rows,
        "rooms": room_rows,
        "commands": command_rows,
        "command_matrix": command_matrix_rows,
    }


def write_csv(path: Path, rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fields: list[str] = []
    seen: set[str] = set()
    for row in rows:
        for key in row:
            if key not in seen:
                seen.add(key)
                fields.append(key)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        for row in rows:
            writer.writerow({field: row.get(field, "") for field in fields})


def markdown_table(rows: list[dict[str, Any]], fields: list[str], limit: int | None = None) -> list[str]:
    selected = rows if limit is None else rows[:limit]
    lines = [
        "| " + " | ".join(fields) + " |",
        "| " + " | ".join("---" for _ in fields) + " |",
    ]
    for row in selected:
        values = []
        for field in fields:
            value = str(row.get(field, ""))
            values.append(value.replace("|", "\\|"))
        lines.append("| " + " | ".join(values) + " |")
    if limit is not None and len(rows) > limit:
        lines.append(f"\n_Omitted {len(rows) - limit} rows; see CSV/JSON outputs._")
    return lines


def write_markdown(path: Path, built: dict[str, Any]) -> None:
    summary = built["summary"]
    command_matrix = built["command_matrix"]
    scene_rows = built["scenes"]
    open_commands = [row for row in command_matrix if row["open_state"] != "closed"]

    lines = [
        "# OOT3D Native Scene Source Index",
        "",
        "This report reshapes the decoded native OOT3D ZSI scene data into source-oriented indices for decompilation.",
        "It does not substitute N64 assets; N64 names are useful only as secondary labels when already present in the decoder evidence.",
        "",
        "## Summary",
        "",
        f"- Scenes: {summary['scene_count']}",
        f"- Setups: {summary['setup_count']}",
        f"- Room bindings: {summary['room_binding_count']}",
        f"- Unique room files: {summary['unique_room_file_count']}",
        f"- Scene commands: {summary['command_count']}",
        f"- Room actor entries: {summary['room_actor_count']}",
        f"- Room object bank entries: {summary['room_object_count']}",
        f"- Commands still semantic/consumer pending: {summary['unresolved_command_count']}",
        f"- Path point statuses: `{json.dumps(summary['path_point_status_counts'], sort_keys=True)}`",
        f"- Source C dir: `{summary['provenance']['c_output_dir']}`",
        "",
        "## Generated Files",
        "",
    ]
    for label, output in summary["generated_outputs"].items():
        lines.append(f"- {label}: `{output}`")

    lines.extend(
        [
            "",
            "## Command Matrix",
            "",
            *markdown_table(
                command_matrix,
                [
                    "command_id_hex",
                    "command_name",
                    "command_count",
                    "scene_count",
                    "decoded_payload_total",
                    "support_levels",
                    "open_state",
                ],
            ),
            "",
            "## Open Source Equivalents",
            "",
        ]
    )
    if open_commands:
        lines.extend(
            markdown_table(
                open_commands,
                [
                    "command_id_hex",
                    "command_name",
                    "command_count",
                    "decoded_payload_total",
                    "support_levels",
                    "decoded_statuses",
                ],
            )
        )
    else:
        lines.append("All scene commands currently map to closed support levels.")

    lines.extend(
        [
            "",
            "## Scene Source Map",
            "",
            *markdown_table(
                scene_rows,
                [
                    "scene_path",
                    "source_c_file",
                    "scene_index_symbol",
                    "setup_count",
                    "room_count",
                    "room_object_count",
                    "room_actor_count",
                    "unresolved_command_count",
                    "payload_counts",
                ],
                limit=140,
            ),
            "",
            "## Notes",
            "",
            "- Per-scene C arrays remain in `tools/oot3d/decomp_support/src/assets/scenes/` and are keyed by native OOT3D ZSI basename.",
            "- `scene_index_registry.c` is the global runtime-style registry; this report is the review/query layer over the same native data.",
            "- Commands marked pending are not missing from the asset export; their remaining work is final source naming/consumer semantics.",
        ]
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def main() -> int:
    index = json.loads(DEFAULT_INDEX.read_text(encoding="utf-8"))
    built = build_index(index)

    DEFAULT_OUT_JSON.parent.mkdir(parents=True, exist_ok=True)
    DEFAULT_OUT_JSON.write_text(
        json.dumps(built, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
        newline="\n",
    )
    write_csv(DEFAULT_SCENE_CSV, built["scenes"])
    write_csv(DEFAULT_SETUP_CSV, built["setups"])
    write_csv(DEFAULT_ROOM_CSV, built["rooms"])
    write_csv(DEFAULT_COMMAND_CSV, built["commands"])
    write_markdown(DEFAULT_OUT_MD, built)

    summary = built["summary"]
    print(
        "wrote OOT3D native scene source index: "
        f"{summary['scene_count']} scenes, "
        f"{summary['setup_count']} setups, "
        f"{summary['room_binding_count']} room bindings, "
        f"{summary['command_count']} commands"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
