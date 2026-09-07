#!/usr/bin/env python3
"""Build a source-oriented runtime context map for OOT3D cutscene data.

This is intentionally evidence-first: it links native scene command 0x17
references to the OOT3D code.bin helpers that store, advance, and interpret the
active cutscene payload. It does not promote strict-N64-compatible decodes as a
replacement for OOT3D payload semantics.
"""

from __future__ import annotations

import csv
import json
import re
from collections import Counter
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CUTSCENE_TABLE = ROOT / "analysis" / "scene_cutscene_source_table.json"
DEFAULT_OFFSET_SCAN = ROOT / "analysis" / "scene_cutscene_context_offset_accesses.csv"
DEFAULT_EXPORT_DIR = ROOT / "analysis" / "scene_cutscene_context_ghidra_export"
DEFAULT_OUT_JSON = ROOT / "analysis" / "scene_cutscene_runtime_context.json"
DEFAULT_OUT_MD = ROOT / "analysis" / "scene_cutscene_runtime_context.md"
DEFAULT_OUT_FUNCTIONS_CSV = ROOT / "analysis" / "scene_cutscene_runtime_context_functions.csv"
DEFAULT_OUT_FIELDS_CSV = ROOT / "analysis" / "scene_cutscene_runtime_context_fields.csv"
DEFAULT_OUT_WORKORDERS_CSV = ROOT / "analysis" / "scene_cutscene_runtime_context_workorders.csv"

PLAY_CUTSCENE_BASE_OFFSET = 0x2298
PLAY_CUTSCENE_PTR_OFFSET = 0x229C
PLAY_CUTSCENE_FLAG_OFFSET = 0x22A0
PLAY_CUTSCENE_CAMERA_FLAG_OFFSET = 0x22A8
PLAY_CUTSCENE_END_COUNTER_OFFSET = 0x22AC
PLAY_CUTSCENE_CAMERA_ID_OFFSET = 0x22BC

FUNCTION_ROLES = {
    "0023449c": {
        "role": "scene_command_17_relocator",
        "confidence": "confirmed",
        "reason": "relocates command +4 by scene base and stores the active cutscene pointer through 0x0037573C",
    },
    "002c5ba0": {
        "role": "cutscene_payload_interpreter",
        "confidence": "confirmed",
        "reason": "reads the OOT3D cutscene payload header and dispatches timeline command groups",
    },
    "00321f50": {
        "role": "cutscene_frame_advance_loop",
        "confidence": "confirmed",
        "reason": "advances csCtx frame 0x20 and calls Cutscene_ProcessCommands(play, csCtx, play+0x229C)",
    },
    "00357ea0": {
        "role": "active_cutscene_pointer_getter",
        "confidence": "confirmed",
        "reason": "returns play+0x229C",
    },
    "0037573c": {
        "role": "active_cutscene_pointer_setter",
        "confidence": "confirmed",
        "reason": "stores play+0x229C and clears play+0x22AC",
    },
    "0044f00c": {
        "role": "event_cutscene_pointer_setter",
        "confidence": "candidate_confirmed_store",
        "reason": "chooses an indexed cutscene entry and stores play+0x229C/play+0x22AC under event checks",
    },
    "00491384": {
        "role": "direct_cutscene_event_dispatch",
        "confidence": "candidate_confirmed_store",
        "reason": "increments play+0x22AC, updates play+0x22A0/0x22A8, and can replace play+0x229C through indexed cutscene data",
    },
    "002e2e60": {
        "role": "gameplay_state_gate_consumer",
        "confidence": "context",
        "reason": "uses the active cutscene pointer getter in broader gameplay gating; not the payload interpreter",
    },
    "003575e8": {
        "role": "same_offset_false_positive",
        "confidence": "diagnostic_only",
        "reason": "uses offsets 0x22A8/0x22AC from a non-PlayState base, so it is excluded from the cutscene context",
    },
    "00357fd0": {
        "role": "same_offset_helper_family",
        "confidence": "diagnostic_only",
        "reason": "included by the focused export but not promoted without PlayState-base evidence",
    },
}

HEADER_LAYOUT = [
    {
        "offset": 0x00,
        "offset_hex": "0x00",
        "width": 4,
        "read_type": "u32",
        "consumer_local": "auStack_40",
        "semantic": "native_header_word0_pending",
        "evidence": "Cutscene_ProcessCommands: FUN_00470778(auStack_40, param_3, 4)",
    },
    {
        "offset": 0x04,
        "offset_hex": "0x04",
        "width": 2,
        "read_type": "u16",
        "consumer_local": "auStack_44",
        "semantic": "native_header_half0_pending",
        "evidence": "Cutscene_ProcessCommands: FUN_00470778(auStack_44, param_3 + 4, 2)",
    },
    {
        "offset": 0x06,
        "offset_hex": "0x06",
        "width": 2,
        "read_type": "u16",
        "consumer_local": "auStack_48",
        "semantic": "native_header_half1_pending",
        "evidence": "Cutscene_ProcessCommands: FUN_00470778(auStack_48, param_3 + 6, 2)",
    },
    {
        "offset": 0x08,
        "offset_hex": "0x08",
        "width": 4,
        "read_type": "s32",
        "consumer_local": "local_4c",
        "semantic": "command_count",
        "evidence": "Cutscene_ProcessCommands loops over local_4c command records starting at param_3 + 0x10",
    },
    {
        "offset": 0x0C,
        "offset_hex": "0x0c",
        "width": 4,
        "read_type": "s32",
        "consumer_local": "local_58",
        "semantic": "end_frame",
        "evidence": "Cutscene_ProcessCommands stores local_58 to csCtx+0x18 and compares it with csCtx frame at +0x20",
    },
    {
        "offset": 0x10,
        "offset_hex": "0x10",
        "width": 0,
        "read_type": "command_stream",
        "consumer_local": "puVar15",
        "semantic": "timeline_command_stream",
        "evidence": "Cutscene_ProcessCommands initializes puVar15 = (uint *)(param_3 + 0x10)",
    },
]

FIELD_ROWS = [
    {
        "offset": PLAY_CUTSCENE_PTR_OFFSET,
        "offset_hex": "0x229c",
        "name": "active_cutscene_data",
        "type": "void* pending",
        "confidence": "confirmed",
        "evidence": "scene command 0x17 setter writes it, getter returns it, frame advance loop passes it to Cutscene_ProcessCommands",
        "producer_entries": "0023449c;0037573c;0044f00c;00491384",
        "consumer_entries": "00357ea0;00321f50;002c5ba0",
    },
    {
        "offset": PLAY_CUTSCENE_FLAG_OFFSET,
        "offset_hex": "0x22a0",
        "name": "cutscene_state_or_completion_flag",
        "type": "u8 pending",
        "confidence": "confirmed_field_pending_name",
        "evidence": "Cutscene_ProcessCommands writes 3 after end-frame handling; direct dispatcher writes 4 and later 3",
        "producer_entries": "002c5ba0;00491384",
        "consumer_entries": "0037571c;002e2e60",
    },
    {
        "offset": PLAY_CUTSCENE_CAMERA_FLAG_OFFSET,
        "offset_hex": "0x22a8",
        "name": "cutscene_camera_reset_flag",
        "type": "u8 pending",
        "confidence": "confirmed_field_pending_name",
        "evidence": "cleared when Gameplay_GetCamera(active camera id) reports camera setting 0x25",
        "producer_entries": "002c5ba0;00491384",
        "consumer_entries": "pending",
    },
    {
        "offset": PLAY_CUTSCENE_END_COUNTER_OFFSET,
        "offset_hex": "0x22ac",
        "name": "cutscene_end_counter",
        "type": "s32 pending",
        "confidence": "confirmed",
        "evidence": "setter clears it; interpreters increment it during end/transition handling",
        "producer_entries": "0037573c;002c5ba0;0044f00c;00491384",
        "consumer_entries": "pending",
    },
    {
        "offset": PLAY_CUTSCENE_CAMERA_ID_OFFSET,
        "offset_hex": "0x22bc",
        "name": "active_cutscene_camera_id",
        "type": "s16/s32 pending",
        "confidence": "confirmed_field_pending_name",
        "evidence": "passed to Gameplay_GetCamera before camera setting 0x25 check",
        "producer_entries": "pending",
        "consumer_entries": "002c5ba0;00491384",
    },
]

WORKORDERS = [
    {
        "priority": 1,
        "title": "Lift Cutscene_ProcessCommands to an OOT3D-native payload interpreter",
        "entry": "002c5ba0",
        "deliverable": "source-level Oot3dCutsceneHeader plus command-group decoder using the 0x10-byte native prefix",
        "evidence": "header reads at +0x00/+0x04/+0x06/+0x08/+0x0C and command stream at +0x10",
    },
    {
        "priority": 2,
        "title": "Replace strict-N64 cutscene header probing with the OOT3D-native header path",
        "entry": "002c5ba0",
        "deliverable": "decoder that treats strict-N64-compatible payloads as a subset, not the primary format",
        "evidence": "current source table leaves 101 header-candidate rows because it probes deltas instead of the proven OOT3D header",
    },
    {
        "priority": 3,
        "title": "Name and split the frame advance loop",
        "entry": "00321f50",
        "deliverable": "source-level update helper for csCtx frame advancement and cutscene playback speed behavior",
        "evidence": "increments csCtx+0x20 and calls Cutscene_ProcessCommands with play+0x229C",
    },
    {
        "priority": 4,
        "title": "Resolve direct event dispatchers that can replace the active cutscene pointer",
        "entry": "0044f00c;00491384",
        "deliverable": "source-level event/direct cutscene trigger tables linked back to indexed native cutscene entries",
        "evidence": "both functions write play+0x229C/play+0x22AC without going through scene command 0x17",
    },
    {
        "priority": 5,
        "title": "Promote field names only after consumer confirmation",
        "entry": "002e2e60;0037571c;00357ea0;0037573c",
        "deliverable": "PlayState cutscene context struct with confirmed field widths and exact names",
        "evidence": "0x229C and 0x22AC are confirmed; 0x22A0/0x22A8/0x22BC need final semantic naming",
    },
]

FUNCTION_NAME_RE = re.compile(r"(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s*\(")
ENTRY_FILE_RE = re.compile(r"_(?P<entry>[0-9a-fA-F]{8})_(?P<name>.+)\.c$")
COMMAND_ID_RE = re.compile(r"(?:local_50 ==|case)\s+(?P<value>0x[0-9a-fA-F]+|\d+)")


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


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def read_csv(path: Path) -> list[dict[str, str]]:
    if not path.is_file():
        return []
    with path.open("r", encoding="utf-8", newline="") as handle:
        return list(csv.DictReader(handle))


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


def hex_id(value: int) -> str:
    return f"0x{value:02x}"


def md_prefix(value: Any, limit: int = 40) -> str:
    text = str(value or "")
    return text if len(text) <= limit else f"{text[:limit]}..."


def extract_function_calls(content: str, self_name: str) -> list[str]:
    ignored = {"if", "for", "while", "switch", "return", "sizeof", "do", self_name}
    calls: list[str] = []
    seen: set[str] = set()
    for match in FUNCTION_NAME_RE.finditer(content):
        name = match.group("name")
        if name in ignored or name.startswith("undefined"):
            continue
        if name not in seen:
            calls.append(name)
            seen.add(name)
    return calls


def extract_command_ids(content: str) -> list[dict[str, Any]]:
    ids: set[int] = set()
    for match in COMMAND_ID_RE.finditer(content):
        raw = match.group("value")
        ids.add(int(raw, 16) if raw.lower().startswith("0x") else int(raw))
    return [{"command_id": value, "command_id_hex": hex_id(value)} for value in sorted(ids)]


def extract_evidence_snippets(content: str) -> list[str]:
    markers = [
        "param_1 + 0x229c",
        "param_1 + 0x22ac",
        "param_1 + 0x22a8",
        "param_1 + 0x22a0",
        "Cutscene_ProcessCommands",
        "oot3d_set_field_229c_clear_22ac",
        "FUN_00470778",
        "Gameplay_GetCamera",
        "oot3d_get_indexed_field_60_entry",
    ]
    snippets: list[str] = []
    seen: set[str] = set()
    for statement in content.split(";"):
        normalized = " ".join(statement.replace("\r", "\n").split())
        if any(marker in normalized for marker in markers):
            if normalized.startswith("{"):
                normalized = normalized[1:].strip()
            if normalized and normalized not in seen:
                snippets.append(normalized[:240])
                seen.add(normalized)
        if len(snippets) >= 8:
            break
    return snippets


def load_function_index(export_dir: Path) -> dict[str, dict[str, str]]:
    result: dict[str, dict[str, str]] = {}
    for row in read_csv(export_dir / "functions_selected.csv"):
        entry = row.get("entry", "").lower()
        if entry:
            result[entry] = row
    return result


def build_function_rows(export_dir: Path) -> list[dict[str, Any]]:
    function_index = load_function_index(export_dir)
    decompiled_dir = export_dir / "decompiled"
    rows: list[dict[str, Any]] = []
    if not decompiled_dir.is_dir():
        return rows
    for path in sorted(decompiled_dir.glob("*.c")):
        match = ENTRY_FILE_RE.search(path.name)
        if not match:
            continue
        entry = match.group("entry").lower()
        if entry not in FUNCTION_ROLES:
            continue
        meta = FUNCTION_ROLES[entry]
        content = path.read_text(encoding="utf-8")
        name = function_index.get(entry, {}).get("name") or match.group("name").removesuffix(".c")
        command_ids = extract_command_ids(content) if entry == "002c5ba0" else []
        rows.append(
            {
                "entry": entry,
                "entry_hex": f"0x{int(entry, 16):08x}",
                "name": name,
                "role": meta["role"],
                "confidence": meta["confidence"],
                "reason": meta["reason"],
                "calls_text": ";".join(extract_function_calls(content, name)),
                "evidence_snippets": extract_evidence_snippets(content),
                "evidence_snippets_text": " | ".join(extract_evidence_snippets(content)),
                "recognized_command_count": len(command_ids),
                "recognized_command_ids_text": ",".join(row["command_id_hex"] for row in command_ids),
                "decompiled_file": str(path.relative_to(ROOT)),
            }
        )
    rows.sort(key=lambda row: (row["confidence"] == "diagnostic_only", row["entry"]))
    return rows


def build_offset_summary(offset_scan_path: Path) -> dict[str, Any]:
    rows = read_csv(offset_scan_path)
    counts = Counter(row.get("matched_offset", "") for row in rows if row.get("matched_offset"))
    focused_entries = {entry.lower() for entry in FUNCTION_ROLES}
    focused_rows = [row for row in rows if row.get("entry", "").lower() in focused_entries]
    return {
        "path": str(offset_scan_path.relative_to(ROOT)),
        "raw_access_count": len(rows),
        "raw_counts_by_offset": dict(sorted(counts.items())),
        "focused_access_count": len(focused_rows),
        "focused_accesses": focused_rows,
    }


def build_spot04_rows(cutscene_table: dict[str, Any]) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for row in as_list(cutscene_table.get("cutscene_rows")):
        row = as_dict(row)
        if row.get("scene_path") != "spot04_info.zsi":
            continue
        rows.append(
            {
                "cutscene_source_index": int_value(row.get("cutscene_source_index")),
                "setup_index": int_value(row.get("setup_index")),
                "command_argument_hex": row.get("command_argument_hex", ""),
                "payload_symbol": row.get("payload_symbol", ""),
                "validation_status": row.get("validation_status", ""),
                "strict_decoded": bool(row.get("strict_decoded")),
                "strict_delta_hex": row.get("strict_delta_hex", ""),
                "strict_command_count": int_value(row.get("strict_command_count")),
                "strict_camera_point_count": int_value(row.get("strict_camera_point_count")),
                "raw_prefix_hex": row.get("raw_prefix_hex", ""),
            }
        )
    rows.sort(key=lambda row: row["setup_index"])
    return rows


def build_context(
    cutscene_table_path: Path = DEFAULT_CUTSCENE_TABLE,
    offset_scan_path: Path = DEFAULT_OFFSET_SCAN,
    export_dir: Path = DEFAULT_EXPORT_DIR,
) -> dict[str, Any]:
    cutscene_table = load_json(cutscene_table_path)
    function_rows = build_function_rows(export_dir)
    interpreter = next((row for row in function_rows if row["entry"] == "002c5ba0"), {})
    recognized_command_ids = [
        {"command_id": int(value, 16), "command_id_hex": value}
        for value in str(interpreter.get("recognized_command_ids_text", "")).split(",")
        if value
    ]
    summary = as_dict(cutscene_table.get("summary"))
    spot04_rows = build_spot04_rows(cutscene_table)
    return {
        "format": "oot3d_scene_cutscene_runtime_context_v1",
        "source_policy": {
            "native_code_source": "OOT3D ExeFS code.bin imported into Ghidra",
            "asset_source": "OOT3D native ZSI scene command 0x17 rows from scene_cutscene_source_table.json",
            "n64_policy": "N64 source can inform labels only when strategically useful; this report is built from OOT3D assets and OOT3D code.bin evidence.",
        },
        "inputs": {
            "cutscene_table": str(cutscene_table_path.relative_to(ROOT)),
            "offset_scan": str(offset_scan_path.relative_to(ROOT)),
            "ghidra_export": str(export_dir.relative_to(ROOT)),
        },
        "summary": {
            "cutscene_reference_count": int_value(summary.get("cutscene_source_row_count")),
            "strict_decoded_cutscene_count": int_value(summary.get("strict_decoded_cutscene_count")),
            "oot3d_native_pending_cutscene_count": int_value(summary.get("cutscene_source_row_count"))
            - int_value(summary.get("strict_decoded_cutscene_count")),
            "spot04_cutscene_count": len(spot04_rows),
            "spot04_strict_decoded_count": sum(1 for row in spot04_rows if row["strict_decoded"]),
            "runtime_function_count": len(function_rows),
            "confirmed_runtime_function_count": sum(1 for row in function_rows if row["confidence"] == "confirmed"),
            "recognized_interpreter_command_count": len(recognized_command_ids),
        },
        "play_cutscene_context_base": {
            "base_offset": PLAY_CUTSCENE_BASE_OFFSET,
            "base_offset_hex": "0x2298",
            "evidence": "Cutscene_ProcessCommands computes local_30 = play + 0x2298 and accesses fields relative to that base.",
        },
        "fields": FIELD_ROWS,
        "native_payload_header": HEADER_LAYOUT,
        "recognized_interpreter_commands": recognized_command_ids,
        "functions": function_rows,
        "offset_scan_summary": build_offset_summary(offset_scan_path),
        "spot04_cutscene_rows": spot04_rows,
        "workorders": WORKORDERS,
    }


def write_markdown(path: Path, payload: dict[str, Any]) -> None:
    summary = as_dict(payload.get("summary"))
    lines = [
        "# OOT3D Scene Cutscene Runtime Context",
        "",
        "This generated report links native scene command `0x17` cutscene references to the OOT3D `code.bin` runtime consumers. It is a decompilation work map, not a replacement decoder inferred from N64 payloads.",
        "",
        "## Summary",
        "",
        f"- Cutscene references: {summary.get('cutscene_reference_count')}",
        f"- Strict-N64-compatible subdecodes kept as subset evidence: {summary.get('strict_decoded_cutscene_count')}",
        f"- OOT3D-native payloads still needing native interpreter lowering: {summary.get('oot3d_native_pending_cutscene_count')}",
        f"- `spot04_info.zsi` cutscene references: {summary.get('spot04_cutscene_count')}",
        f"- Runtime functions mapped: {summary.get('runtime_function_count')}",
        f"- Interpreter command IDs observed in `Cutscene_ProcessCommands`: {summary.get('recognized_interpreter_command_count')}",
        "",
        "## Confirmed Runtime Path",
        "",
        "1. Scene command `0x17` handler `0x0023449C` relocates command word `+4` by the scene base.",
        "2. Helper `0x0037573C` stores the relocated pointer at `play+0x229C` and clears `play+0x22AC`.",
        "3. Frame loop `0x00321F50` advances `csCtx+0x20` and calls `Cutscene_ProcessCommands(play, csCtx, *(play+0x229C))`.",
        "4. Interpreter `0x002C5BA0` reads the OOT3D payload header and dispatches command groups from `payload+0x10`.",
        "",
        "## Play Cutscene Fields",
        "",
        "| Offset | Name | Confidence | Evidence |",
        "| --- | --- | --- | --- |",
    ]
    for row in as_list(payload.get("fields")):
        row = as_dict(row)
        lines.append(
            f"| `{row.get('offset_hex')}` | `{row.get('name')}` | `{row.get('confidence')}` | {row.get('evidence')} |"
        )

    lines.extend(
        [
            "",
            "## Native Payload Header",
            "",
            "| Offset | Width | Type | Semantic | Evidence |",
            "| --- | ---: | --- | --- | --- |",
        ]
    )
    for row in as_list(payload.get("native_payload_header")):
        row = as_dict(row)
        lines.append(
            f"| `{row.get('offset_hex')}` | {row.get('width')} | `{row.get('read_type')}` | `{row.get('semantic')}` | {row.get('evidence')} |"
        )

    lines.extend(
        [
            "",
            "## Runtime Functions",
            "",
            "| Entry | Name | Role | Confidence | File |",
            "| --- | --- | --- | --- | --- |",
        ]
    )
    for row in as_list(payload.get("functions")):
        row = as_dict(row)
        lines.append(
            f"| `{row.get('entry_hex')}` | `{row.get('name')}` | `{row.get('role')}` | `{row.get('confidence')}` | `{row.get('decompiled_file')}` |"
        )

    lines.extend(
        [
            "",
            "## `spot04_info.zsi` Cutscene Worklist",
            "",
            "| Setup | Offset | Status | Strict decoded | Strict commands | Camera points | Prefix |",
            "| ---: | ---: | --- | --- | ---: | ---: | --- |",
        ]
    )
    for row in as_list(payload.get("spot04_cutscene_rows")):
        row = as_dict(row)
        lines.append(
            f"| {row.get('setup_index')} | `{row.get('command_argument_hex')}` | `{row.get('validation_status')}` | {row.get('strict_decoded')} | {row.get('strict_command_count')} | {row.get('strict_camera_point_count')} | `{md_prefix(row.get('raw_prefix_hex'))}` |"
        )

    commands = [as_dict(row).get("command_id_hex", "") for row in as_list(payload.get("recognized_interpreter_commands"))]
    command_groups = ", ".join(f"`{command}`" for command in commands)
    lines.extend(
        [
            "",
            "## Interpreter Command IDs",
            "",
            command_groups,
            "",
            "## Workorders",
            "",
            "| Priority | Entry | Task | Deliverable |",
            "| ---: | --- | --- | --- |",
        ]
    )
    for row in as_list(payload.get("workorders")):
        row = as_dict(row)
        lines.append(
            f"| {row.get('priority')} | `{row.get('entry')}` | {row.get('title')} | {row.get('deliverable')} |"
        )
    lines.append("")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def write_outputs(payload: dict[str, Any]) -> None:
    write_json(DEFAULT_OUT_JSON, payload)
    write_markdown(DEFAULT_OUT_MD, payload)
    write_csv(
        DEFAULT_OUT_FUNCTIONS_CSV,
        [as_dict(row) for row in as_list(payload.get("functions"))],
        [
            "entry",
            "entry_hex",
            "name",
            "role",
            "confidence",
            "reason",
            "calls_text",
            "evidence_snippets_text",
            "recognized_command_count",
            "recognized_command_ids_text",
            "decompiled_file",
        ],
    )
    write_csv(
        DEFAULT_OUT_FIELDS_CSV,
        [as_dict(row) for row in as_list(payload.get("fields"))],
        [
            "offset",
            "offset_hex",
            "name",
            "type",
            "confidence",
            "evidence",
            "producer_entries",
            "consumer_entries",
        ],
    )
    write_csv(
        DEFAULT_OUT_WORKORDERS_CSV,
        [as_dict(row) for row in as_list(payload.get("workorders"))],
        ["priority", "title", "entry", "deliverable", "evidence"],
    )


def validate(payload: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    summary = as_dict(payload.get("summary"))
    if int_value(summary.get("cutscene_reference_count")) != 112:
        errors.append("cutscene reference count changed from audited baseline of 112")
    if int_value(summary.get("spot04_cutscene_count")) != 10:
        errors.append("spot04 cutscene reference count changed from audited baseline of 10")
    entries = {as_dict(row).get("entry") for row in as_list(payload.get("functions"))}
    for required in ("0023449c", "002c5ba0", "00321f50", "00357ea0", "0037573c"):
        if required not in entries:
            errors.append(f"required runtime function {required} missing from export map")
    header_offsets = [as_dict(row).get("offset") for row in as_list(payload.get("native_payload_header"))]
    if header_offsets != [0, 4, 6, 8, 12, 16]:
        errors.append("native payload header layout offsets changed")
    if int_value(summary.get("recognized_interpreter_command_count")) < 100:
        errors.append("interpreter command-id extraction found too few command IDs")
    return errors


def main() -> int:
    payload = build_context()
    errors = validate(payload)
    if errors:
        for error in errors:
            print(f"error: {error}")
        return 1
    write_outputs(payload)
    summary = as_dict(payload.get("summary"))
    print(
        "wrote scene cutscene runtime context: "
        f"{summary.get('cutscene_reference_count')} refs, "
        f"{summary.get('runtime_function_count')} functions, "
        f"{summary.get('recognized_interpreter_command_count')} interpreter command IDs"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
