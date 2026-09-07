#!/usr/bin/env python3
"""Cross-reference native OOT3D path-list records with actor path-index users.

This report is intentionally source-oriented. It does not infer new path data
or repair invalid point offsets; it records which decoded actor entries can
refer to which native path records using path-index formulas observed in
OOT3D code.bin consumer bodies.
"""

from __future__ import annotations

import csv
import json
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any, Callable


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_INDEX = ROOT / "analysis" / "oot3d_native_scene_index_all_full.json"
DEFAULT_OUT_JSON = ROOT / "analysis" / "scene_path_actor_usage.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "scene_path_actor_usage.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "scene_path_actor_usage.md"


def int_value(value: Any, default: int = 0) -> int:
    try:
        return int(value)
    except (TypeError, ValueError):
        return default


def as_dict(value: Any) -> dict[str, Any]:
    return value if isinstance(value, dict) else {}


def as_list(value: Any) -> list[Any]:
    return value if isinstance(value, list) else []


def u16(value: int) -> int:
    return value & 0xFFFF


def s16(value: int) -> int:
    value &= 0xFFFF
    return value - 0x10000 if value & 0x8000 else value


def high8(value: int) -> int:
    return (u16(value) >> 8) & 0xFF


def low8(value: int) -> int:
    return u16(value) & 0xFF


def low8_path_index(entry: dict[str, Any]) -> dict[str, Any]:
    params = int_value(entry.get("params"))
    index = low8(params)
    if index == 0xFF:
        return {"status": "sentinel_no_path", "path_index": None, "reason": "low byte is 0xff sentinel"}
    return {"status": "resolved", "path_index": index, "reason": "path index = params & 0xff"}


def high8_path_index(entry: dict[str, Any]) -> dict[str, Any]:
    params = int_value(entry.get("params"))
    index = high8(params)
    if index == 0xFF:
        return {"status": "sentinel_no_path", "path_index": None, "reason": "high byte is 0xff sentinel"}
    return {"status": "resolved", "path_index": index, "reason": "path index = (params >> 8) & 0xff"}


def high5_path_index(entry: dict[str, Any]) -> dict[str, Any]:
    params = int_value(entry.get("params"))
    index = high8(params) & 0x1F
    if index == 0x1F:
        return {"status": "sentinel_no_path", "path_index": None, "reason": "high 5 bits are 0x1f sentinel"}
    return {"status": "resolved", "path_index": index, "reason": "path index = ((params >> 8) & 0x1f)"}


def en_bb_path_index(entry: dict[str, Any]) -> dict[str, Any]:
    params = s16(int_value(entry.get("params")))
    if params != -5:
        return {"status": "not_this_mode", "path_index": None, "reason": "EnBb path consumer is only reached in params == -5 mode"}
    return {
        "status": "needs_formula_review",
        "path_index": None,
        "reason": "code.bin body derives actor+0x570 before the path read; formula is not yet safely promoted",
    }


def unresolved_formula(reason: str) -> Callable[[dict[str, Any]], dict[str, Any]]:
    def resolver(_entry: dict[str, Any]) -> dict[str, Any]:
        return {"status": "needs_formula_review", "path_index": None, "reason": reason}

    return resolver


PATH_ACTOR_FORMULAS: dict[str, dict[str, Any]] = {
    "ACTOR_EN_GOROIWA": {
        "consumer": "EnGoroiwa_Init 0x002103C8",
        "formula": "low8(params), 0xff sentinel",
        "resolver": low8_path_index,
    },
    "ACTOR_EN_MD": {
        "consumer": "EnMd_Init/EnMd_Update 0x0016604C/0x001B736C",
        "formula": "high8(params), 0xff sentinel",
        "resolver": high8_path_index,
    },
    "ACTOR_OBJ_BEAN": {
        "consumer": "ObjBean_Init 0x001E12B0",
        "formula": "((params >> 8) & 0x1f), 0x1f sentinel",
        "resolver": high5_path_index,
    },
    "ACTOR_EN_KZ": {
        "consumer": "EnKz_Mweep 0x003B8E4C",
        "formula": "high8(params), 0xff sentinel",
        "resolver": high8_path_index,
    },
    "ACTOR_EN_DAIKU_KAKARIKO": {
        "consumer": "EnDaikuKakariko_Run 0x001CE204",
        "formula": "high8(params), 0xff sentinel",
        "resolver": high8_path_index,
    },
    "ACTOR_EN_DAIKU": {
        "consumer": "EnDaikuKakariko_Run 0x001CE204 candidate",
        "formula": "high8(params), 0xff sentinel",
        "resolver": high8_path_index,
    },
    "ACTOR_EN_BB": {
        "consumer": "EnBb_Init 0x00162328",
        "formula": "mode-gated; not promoted",
        "resolver": en_bb_path_index,
    },
    "ACTOR_EN_CS": {
        "consumer": "EnCs_Walk 0x00178F88",
        "formula": "actor+0x214 field; init path-index source not yet exported",
        "resolver": unresolved_formula("EnCs_Walk uses actor+0x214; initializer that writes it is still unresolved"),
    },
}


def path_records_for_setup(setup: dict[str, Any]) -> list[dict[str, Any]]:
    for command in as_list(setup.get("commands")):
        if int_value(command.get("command_id"), -1) != 0x0D:
            continue
        decoded = as_dict(command.get("decoded"))
        return [row for row in as_list(decoded.get("raw_records")) if isinstance(row, dict)]
    return []


def actor_entries_from_setup(scene_path: str, setup: dict[str, Any]) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    setup_index = int_value(setup.get("index"), -1)
    for command in as_list(setup.get("commands")):
        if not isinstance(command, dict):
            continue
        command_id = int_value(command.get("command_id"), -1)
        decoded = as_dict(command.get("decoded"))
        entries: list[Any] = []
        source_kind = ""
        if command_id in {0x00, 0x01}:
            selected = as_dict(decoded.get("selected_candidate"))
            entries = as_list(selected.get("entries"))
            source_kind = "spawn_list" if command_id == 0x00 else "standard_actor_list"
        elif command_id == 0x0E:
            entries = as_list(decoded.get("entries"))
            source_kind = "transition_actor_list"
        else:
            continue
        for entry in entries:
            if not isinstance(entry, dict):
                continue
            rows.append(
                {
                    **entry,
                    "scene_path": scene_path,
                    "setup_index": setup_index,
                    "source_kind": source_kind,
                    "source_index": int_value(entry.get("index"), -1),
                    "room_path": "",
                    "room_index": "",
                }
            )
    return rows


def setup_room_paths(setup: dict[str, Any]) -> set[str]:
    paths: set[str] = set()
    for command in as_list(setup.get("commands")):
        if not isinstance(command, dict) or int_value(command.get("command_id"), -1) != 0x04:
            continue
        decoded = as_dict(command.get("decoded"))
        for reference in as_list(decoded.get("room_references")):
            if not isinstance(reference, dict):
                continue
            for key in ("matched_local_room_path", "room_file", "path"):
                value = str(reference.get(key, ""))
                if value:
                    paths.add(value)
    return paths


def actor_entries_from_rooms(
    scene_path: str,
    setup_index: int,
    rooms: list[Any],
    active_room_paths: set[str],
) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for room in rooms:
        if not isinstance(room, dict):
            continue
        room_path = str(room.get("room_path", ""))
        if active_room_paths and room_path not in active_room_paths:
            continue
        payload = as_dict(room.get("room_actor_list"))
        selected = as_dict(payload.get("selected_candidate"))
        for entry in as_list(selected.get("entries")):
            if not isinstance(entry, dict):
                continue
            rows.append(
                {
                    **entry,
                    "scene_path": scene_path,
                    "setup_index": setup_index,
                    "source_kind": "room_actor_list_from_setup_room_list",
                    "source_index": int_value(entry.get("index"), -1),
                    "room_path": room_path,
                    "room_index": room.get("room_index", ""),
                }
            )
    return rows


def classify_reference(
    entry: dict[str, Any],
    path_records: list[dict[str, Any]],
) -> dict[str, Any]:
    actor_name = str(entry.get("actor_name", ""))
    formula = PATH_ACTOR_FORMULAS.get(actor_name)
    if formula is None:
        return {}
    resolved = formula["resolver"](entry)
    path_index = resolved.get("path_index")
    row = {
        "scene_path": entry["scene_path"],
        "setup_index": entry["setup_index"],
        "source_kind": entry["source_kind"],
        "source_index": entry["source_index"],
        "room_path": entry.get("room_path", ""),
        "room_index": entry.get("room_index", ""),
        "actor_name": actor_name,
        "actor_id": int_value(entry.get("actor_id")),
        "params": s16(int_value(entry.get("params"))),
        "params_hex": f"0x{u16(int_value(entry.get('params'))):04x}",
        "consumer": formula["consumer"],
        "formula": formula["formula"],
        "reference_status": resolved["status"],
        "path_index": "" if path_index is None else int(path_index),
        "reason": resolved["reason"],
        "path_record_status": "",
        "path_point_count": "",
        "path_raw_points_offset_hex": "",
    }
    if resolved["status"] != "resolved":
        return row
    index = int(path_index)
    if index < 0 or index >= len(path_records):
        row["reference_status"] = "path_index_out_of_range"
        row["reason"] = f"path index {index} outside setup path count {len(path_records)}"
        return row
    path_record = path_records[index]
    row["path_record_status"] = str(path_record.get("points_status", "unknown"))
    row["path_point_count"] = int_value(path_record.get("point_count"))
    row["path_raw_points_offset_hex"] = str(path_record.get("points_offset_hex", ""))
    return row


def build_report(index_path: Path) -> dict[str, Any]:
    payload = json.loads(index_path.read_text(encoding="utf-8"))
    rows: list[dict[str, Any]] = []
    path_record_rows: list[dict[str, Any]] = []
    path_record_reference_counts: Counter[tuple[str, int, int]] = Counter()

    for record in as_list(payload.get("records")):
        if not isinstance(record, dict):
            continue
        scene_path = str(record.get("scene_path", ""))
        rooms = as_list(record.get("rooms"))
        for setup in as_list(record.get("setups")):
            if not isinstance(setup, dict):
                continue
            setup_index = int_value(setup.get("index"), -1)
            path_records = path_records_for_setup(setup)
            active_room_paths = setup_room_paths(setup)
            for path_index, path_record in enumerate(path_records):
                path_record_rows.append(
                    {
                        "scene_path": scene_path,
                        "setup_index": setup_index,
                        "path_index": path_index,
                        "points_status": str(path_record.get("points_status", "")),
                        "point_count": int_value(path_record.get("point_count")),
                        "raw_points_offset_hex": str(path_record.get("points_offset_hex", "")),
                    }
                )
            actors = actor_entries_from_setup(scene_path, setup)
            actors.extend(actor_entries_from_rooms(scene_path, setup_index, rooms, active_room_paths))
            for actor in actors:
                row = classify_reference(actor, path_records)
                if not row:
                    continue
                rows.append(row)
                if row["reference_status"] == "resolved":
                    path_record_reference_counts[(scene_path, setup_index, int(row["path_index"]))] += 1

    path_status_counts = Counter(row["points_status"] for row in path_record_rows)
    reference_status_counts = Counter(row["reference_status"] for row in rows)
    referenced_path_status_counts = Counter(row["path_record_status"] for row in rows if row["reference_status"] == "resolved")
    invalid_records = [
        row for row in path_record_rows if row["points_status"] == "invalid_points_offset"
    ]
    referenced_invalid = []
    unreferenced_invalid = []
    for row in invalid_records:
        key = (row["scene_path"], int(row["setup_index"]), int(row["path_index"]))
        out = dict(row)
        out["known_formula_reference_count"] = path_record_reference_counts.get(key, 0)
        if out["known_formula_reference_count"]:
            referenced_invalid.append(out)
        else:
            unreferenced_invalid.append(out)

    return {
        "format": "oot3d_scene_path_actor_usage_v1",
        "source_policy": {
            "native_asset_source": str(index_path),
            "native_code_evidence": "scene_command_index_consumers_ghidra_export path consumer bodies",
            "n64_policy": "N64 source is not used by this report.",
        },
        "formulas": {
            actor: {
                "consumer": str(info["consumer"]),
                "formula": str(info["formula"]),
            }
            for actor, info in sorted(PATH_ACTOR_FORMULAS.items())
        },
        "summary": {
            "path_record_count": len(path_record_rows),
            "path_record_status_counts": dict(sorted(path_status_counts.items())),
            "actor_path_reference_count": len(rows),
            "actor_reference_status_counts": dict(sorted(reference_status_counts.items())),
            "referenced_path_status_counts": dict(sorted(referenced_path_status_counts.items())),
            "invalid_path_record_count": len(invalid_records),
            "referenced_invalid_path_record_count": len(referenced_invalid),
            "unreferenced_invalid_path_record_count": len(unreferenced_invalid),
            "caveat": "Room actors are associated only with setups whose native room_list references their room. Unreferenced means unreferenced by the currently promoted actor path-index formulas.",
        },
        "references": rows,
        "invalid_path_records_referenced_by_known_formulas": referenced_invalid,
        "invalid_path_records_not_referenced_by_known_formulas": unreferenced_invalid,
    }


CSV_COLUMNS = [
    "scene_path",
    "setup_index",
    "source_kind",
    "source_index",
    "room_path",
    "room_index",
    "actor_name",
    "params_hex",
    "consumer",
    "formula",
    "reference_status",
    "path_index",
    "path_record_status",
    "path_point_count",
    "path_raw_points_offset_hex",
    "reason",
]


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def write_csv(path: Path, rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=CSV_COLUMNS)
        writer.writeheader()
        for row in rows:
            writer.writerow({key: row.get(key, "") for key in CSV_COLUMNS})


def write_markdown(path: Path, payload: dict[str, Any]) -> None:
    summary = payload["summary"]
    lines = [
        "# Scene Path Actor Usage",
        "",
        "Generated from native OOT3D ZSI actor entries plus OOT3D `code.bin` path consumers.",
        "",
        "## Summary",
        "",
        f"- Path records: {summary['path_record_count']} `{json.dumps(summary['path_record_status_counts'], sort_keys=True)}`",
        f"- Actor path references: {summary['actor_path_reference_count']} `{json.dumps(summary['actor_reference_status_counts'], sort_keys=True)}`",
        f"- Referenced path statuses: `{json.dumps(summary['referenced_path_status_counts'], sort_keys=True)}`",
        f"- Invalid-offset path records: {summary['invalid_path_record_count']}",
        f"- Invalid-offset records referenced by promoted formulas: {summary['referenced_invalid_path_record_count']}",
        f"- Invalid-offset records not referenced by promoted formulas: {summary['unreferenced_invalid_path_record_count']}",
        "",
        "Caveat: room actors are associated only with setups whose native `room_list` references their room. `unreferenced` here means unreferenced by the currently promoted OOT3D actor path-index formulas, not proven unreachable by every runtime path user.",
        "",
        "## Promoted Formulas",
        "",
        "| Actor | Consumer | Formula |",
        "| --- | --- | --- |",
    ]
    for actor, info in payload["formulas"].items():
        lines.append(f"| `{actor}` | {info['consumer']} | `{info['formula']}` |")

    lines.extend(
        [
            "",
            "## Referenced Invalid-Offset Records",
            "",
        ]
    )
    referenced_invalid = payload["invalid_path_records_referenced_by_known_formulas"]
    if referenced_invalid:
        lines.extend(["| Scene | Setup | Path | References | Raw offset |", "| --- | ---: | ---: | ---: | --- |"])
        for row in referenced_invalid:
            lines.append(
                f"| `{row['scene_path']}` | {row['setup_index']} | {row['path_index']} | "
                f"{row['known_formula_reference_count']} | `{row['raw_points_offset_hex']}` |"
            )
    else:
        lines.append("- None from the currently promoted formulas.")

    lines.extend(
        [
            "",
            "## Remaining Work",
            "",
            "- Resolve `ACTOR_EN_CS` initialization for actor+0x214 path index.",
            "- Resolve `ACTOR_EN_BB` mode-gated actor+0x570 path index before using it for reachability.",
            "- Inspect non-actor runtime path readers only to complete path reachability; `0x0D path_list` scene-command support is already closed from native ZSI plus OOT3D `code.bin` evidence.",
        ]
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines).rstrip() + "\n", encoding="utf-8")


def main() -> int:
    payload = build_report(DEFAULT_INDEX)
    write_json(DEFAULT_OUT_JSON, payload)
    write_csv(DEFAULT_OUT_CSV, payload["references"])
    write_markdown(DEFAULT_OUT_MD, payload)
    print(DEFAULT_OUT_JSON)
    print(DEFAULT_OUT_CSV)
    print(DEFAULT_OUT_MD)
    print(json.dumps(payload["summary"], indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
