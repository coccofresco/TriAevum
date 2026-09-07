#!/usr/bin/env python3
"""Build OOT3D scene-command decompilation workorders from native ZSI indices.

The input scene index is generated from original OOT3D ZSI files. This script
does not promote N64 semantics; it aggregates already exported native-asset
decode evidence and code.bin support levels into a work queue for focused
Ghidra/code.bin lowering.
"""

from __future__ import annotations

import csv
import json
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_INDEX = ROOT / "analysis" / "oot3d_native_scene_index_all_summary.json"
DEFAULT_OUT_JSON = ROOT / "analysis" / "scene_command_decompilation_workorders.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "scene_command_decompilation_workorders.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "scene_command_decompilation_workorders.md"
DEFAULT_SPLITS_CSV = ROOT / "analysis" / "scene_command_handler_function_splits.csv"
DEFAULT_ENTRIES_TXT = ROOT / "analysis" / "scene_command_handler_export_entries.txt"
DEFAULT_CODE_BIN = Path(r"E:\ppssppvr\oot3d_decomp\work\extract\exefs\code.bin")
DEFAULT_MANUAL_SYMBOLS = ROOT / "symbols" / "manual_symbols.csv"
CODE_LOAD_BASE = 0x00100000
SCENE_COMMAND_HANDLER_TABLE = 0x0053CC84
SCENE_COMMAND_HANDLER_ENTRY_COUNT = 0x1A
GHIDRA_EXPORT_TEMPLATE = (
    "powershell -NoProfile -ExecutionPolicy Bypass -File "
    "tools\\oot3d\\decomp_support\\scripts\\ghidra-export-selected.ps1 "
    "-GhidraRoot 'E:\\azahar pcvr\\tools\\pcvr-re-tools\\ghidra' "
    "-JdkRoot 'E:\\azahar pcvr\\tools\\pcvr-re-tools\\jdk21' "
    "-ProjectDir 'E:\\ppssppvr\\oot3d_decomp\\work\\ghidra_project' "
    "-ProjectName 'oot3d_code' "
    "-ExportDir '{export_dir}' "
    "-Entries {entry} -IncludeCallers -IncludeCallees -KeepSelectedArtifacts"
)

SUPPORT_ORDER = {
    "native_asset_decoded_semantic_pending": 0,
    "code_bin_handler_confirmed_semantic_pending": 5,
    "native_asset_decoded_code_bin_pending": 10,
    "unmapped_command": 20,
    "code_bin_handler_confirmed": 70,
    "native_control_marker": 80,
    "code_bin_confirmed": 90,
}


def load_index(path: Path) -> dict[str, Any]:
    if not path.is_file():
        raise FileNotFoundError(f"{path}: scene index JSON not found")
    return json.loads(path.read_text(encoding="utf-8"))


def load_manual_symbol_names(path: Path) -> dict[int, str]:
    if not path.is_file():
        return {}
    names: dict[int, str] = {}
    with path.open(newline="", encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            try:
                entry = int(str(row.get("entry", "")).strip(), 16)
            except ValueError:
                continue
            name = row.get("new_name") or row.get("old_name") or ""
            if name:
                names[entry] = name
    return names


def read_scene_command_handler_table(
    code_bin: Path,
    *,
    symbol_names: dict[int, str],
) -> dict[int, dict[str, Any]]:
    if not code_bin.is_file():
        return {}
    data = code_bin.read_bytes()
    table_offset = SCENE_COMMAND_HANDLER_TABLE - CODE_LOAD_BASE
    if table_offset < 0 or table_offset + SCENE_COMMAND_HANDLER_ENTRY_COUNT * 4 > len(data):
        raise ValueError(
            f"scene command handler table 0x{SCENE_COMMAND_HANDLER_TABLE:08x} is outside {code_bin}"
        )

    handlers: dict[int, dict[str, Any]] = {}
    loaded_end = CODE_LOAD_BASE + len(data)
    for command_id in range(SCENE_COMMAND_HANDLER_ENTRY_COUNT):
        slot_address = SCENE_COMMAND_HANDLER_TABLE + command_id * 4
        slot_offset = table_offset + command_id * 4
        handler_address = int.from_bytes(data[slot_offset : slot_offset + 4], "little")
        in_code = CODE_LOAD_BASE <= handler_address < loaded_end
        handlers[command_id] = {
            "handler_table_address": f"0x{SCENE_COMMAND_HANDLER_TABLE:08x}",
            "handler_table_slot_address": f"0x{slot_address:08x}",
            "handler_table_slot_file_offset": f"0x{slot_offset:08x}",
            "handler_address": f"0x{handler_address:08x}",
            "handler_entry": f"{handler_address:08x}" if handler_address else "",
            "handler_name": symbol_names.get(handler_address, f"FUN_{handler_address:08x}" if handler_address else ""),
            "handler_in_code_bin": in_code,
            "code_bin": str(code_bin),
            "code_load_base": f"0x{CODE_LOAD_BASE:08x}",
        }
    return handlers


def ghidra_export_command(command_id_hex: str, handler_entry: str) -> str:
    if not handler_entry:
        return ""
    export_dir = f"analysis\\scene_command_handler_{command_id_hex}_ghidra_export"
    return GHIDRA_EXPORT_TEMPLATE.format(export_dir=export_dir, entry=handler_entry)


def c_identifier(value: str) -> str:
    normalized = []
    for char in value.lower():
        normalized.append(char if char.isalnum() else "_")
    identifier = "".join(normalized).strip("_")
    while "__" in identifier:
        identifier = identifier.replace("__", "_")
    return identifier or "unknown"


def handler_split_name(row: dict[str, Any]) -> str:
    command_id = f"{int(row['command_id']):02x}"
    command_name = c_identifier(str(row.get("command_name", "unknown")))
    return f"oot3d_scene_cmd_{command_id}_{command_name}"


def unique_list(values: list[str]) -> list[str]:
    seen: set[str] = set()
    result: list[str] = []
    for value in values:
        if value and value not in seen:
            seen.add(value)
            result.append(value)
    return result


def support_priority(level: str) -> int:
    return SUPPORT_ORDER.get(level, 50)


def workorder_priority(level: str, command_count: int, scene_count: int) -> float:
    score = float(support_priority(level))
    score -= min(command_count, 500) / 1000.0
    score -= min(scene_count, 200) / 2000.0
    return round(score, 4)


def suggested_gate(level: str, command_id_hex: str, open_questions: list[str]) -> str:
    if level == "code_bin_confirmed":
        return "Keep as reference evidence; use it as the handler-map template for pending commands."
    if level == "native_control_marker":
        return "No data handler lowering needed unless the setup command dispatcher itself is being decompiled."
    if level == "code_bin_handler_confirmed_semantic_pending":
        return "Use the exported OOT3D handler and downstream consumers to resolve the remaining raw field semantics."
    if level == "code_bin_handler_confirmed":
        return "Promote handler stores and relocations into final source field names; validate downstream consumers as needed."
    if "semantic_pending" in level:
        return "Resolve native consumer semantics in code.bin before naming fields; keep generated C payload raw meanwhile."
    if open_questions:
        return open_questions[0]
    return f"Identify and lower the OOT3D scene command {command_id_hex} handler in code.bin."


def build_workorders(index: dict[str, Any]) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    symbol_names = load_manual_symbol_names(DEFAULT_MANUAL_SYMBOLS)
    handlers = read_scene_command_handler_table(DEFAULT_CODE_BIN, symbol_names=symbol_names)
    groups: dict[int, dict[str, Any]] = {}
    per_command_scenes: dict[int, set[str]] = defaultdict(set)
    per_command_setups: dict[int, set[str]] = defaultdict(set)
    decoded_status_counts: dict[int, Counter[str]] = defaultdict(Counter)
    support_counts: Counter[str] = Counter()

    for record in index.get("records", []):
        scene_path = str(record.get("scene_path", ""))
        scene_stem = str(record.get("scene_stem", Path(scene_path).stem))
        for setup in record.get("setups", []):
            setup_index = int(setup.get("index", -1))
            setup_key = f"{scene_path}#setup{setup_index}"
            for command in setup.get("commands", []):
                command_id = int(command.get("command_id", -1))
                if command_id < 0:
                    continue
                command_id_hex = str(command.get("command_id_hex", f"0x{command_id:02x}"))
                evidence = command.get("decompilation_evidence") or {}
                if not isinstance(evidence, dict):
                    evidence = {}
                support_level = str(evidence.get("support_level", "unmapped_command"))
                decoded = command.get("decoded") or {}
                decoded_status = decoded.get("status", "not_decoded") if isinstance(decoded, dict) else "not_decoded"
                handler = handlers.get(command_id, {})

                group = groups.setdefault(
                    command_id,
                    {
                        "command_id": command_id,
                        "command_id_hex": command_id_hex,
                        "command_name": command.get("command_name", "unknown"),
                        "support_level": support_level,
                        "command_count": 0,
                        "scene_count": 0,
                        "setup_count": 0,
                        "decoded_status_counts": {},
                        "export_structs": [],
                        "native_asset_evidence": [],
                        "code_bin_evidence": [],
                        "open_questions": [],
                        "handler": dict(handler),
                        "ghidra_export_command": ghidra_export_command(
                            command_id_hex,
                            str(handler.get("handler_entry", "")),
                        ),
                        "sample_locations": [],
                    },
                )

                group["command_count"] += 1
                if support_priority(support_level) < support_priority(str(group["support_level"])):
                    group["support_level"] = support_level
                group["export_structs"].extend(str(value) for value in evidence.get("export_structs", []))
                group["native_asset_evidence"].extend(str(value) for value in evidence.get("native_asset_evidence", []))
                group["code_bin_evidence"].extend(str(value) for value in evidence.get("code_bin_evidence", []))
                group["open_questions"].extend(str(value) for value in evidence.get("open_questions", []))

                per_command_scenes[command_id].add(scene_stem)
                per_command_setups[command_id].add(setup_key)
                decoded_status_counts[command_id][str(decoded_status)] += 1
                support_counts[support_level] += 1

                if len(group["sample_locations"]) < 8:
                    group["sample_locations"].append(
                        {
                            "scene_path": scene_path,
                            "setup_index": setup_index,
                            "command_offset_hex": command.get("offset_hex", ""),
                            "argument_hex": command.get("argument_hex", ""),
                            "parameter": command.get("parameter", 0),
                            "decoded_status": decoded_status,
                        }
                    )

    workorders: list[dict[str, Any]] = []
    for command_id, group in groups.items():
        group["scene_count"] = len(per_command_scenes[command_id])
        group["setup_count"] = len(per_command_setups[command_id])
        group["decoded_status_counts"] = dict(sorted(decoded_status_counts[command_id].items()))
        handler_info = group.get("handler")
        if isinstance(handler_info, dict) and handler_info.get("handler_entry"):
            handler_info["split_function_name"] = handler_split_name(group)
        for key in ("export_structs", "native_asset_evidence", "code_bin_evidence", "open_questions"):
            group[key] = unique_list(group[key])
        group["priority_score"] = workorder_priority(
            str(group["support_level"]),
            int(group["command_count"]),
            int(group["scene_count"]),
        )
        group["suggested_gate"] = suggested_gate(
            str(group["support_level"]),
            str(group["command_id_hex"]),
            group["open_questions"],
        )
        workorders.append(group)

    workorders.sort(
        key=lambda row: (
            float(row["priority_score"]),
            support_priority(str(row["support_level"])),
            -int(row["command_count"]),
            int(row["command_id"]),
        )
    )
    for rank, row in enumerate(workorders, start=1):
        row["rank"] = rank

    summary = {
        "source_index": str(DEFAULT_INDEX),
        "scene_count": index.get("scene_count", 0),
        "setup_count": index.get("setup_count", 0),
        "command_total": sum(int(row["command_count"]) for row in workorders),
        "workorder_count": len(workorders),
        "handler_table": {
            "code_bin": str(DEFAULT_CODE_BIN),
            "code_load_base": f"0x{CODE_LOAD_BASE:08x}",
            "address": f"0x{SCENE_COMMAND_HANDLER_TABLE:08x}",
            "entry_count": SCENE_COMMAND_HANDLER_ENTRY_COUNT,
            "resolved_handler_count": sum(1 for row in workorders if row.get("handler", {}).get("handler_in_code_bin")),
        },
        "support_level_counts": dict(sorted(support_counts.items())),
        "pending_workorder_count": sum(
            1
            for row in workorders
            if row["support_level"] not in {"code_bin_confirmed", "native_control_marker"}
        ),
        "top_pending_command": next(
            (
                {
                    "command_id_hex": row["command_id_hex"],
                    "command_name": row["command_name"],
                    "support_level": row["support_level"],
                    "command_count": row["command_count"],
                    "handler": row.get("handler", {}),
                    "ghidra_export_command": row.get("ghidra_export_command", ""),
                    "suggested_gate": row["suggested_gate"],
                }
                for row in workorders
                if row["support_level"] not in {"code_bin_confirmed", "native_control_marker"}
            ),
            None,
        ),
    }
    return workorders, summary


def write_json(path: Path, rows: list[dict[str, Any]], summary: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps({"summary": summary, "workorders": rows}, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


def write_csv(path: Path, rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fields = [
        "rank",
        "priority_score",
        "command_id_hex",
        "command_name",
        "support_level",
        "handler_address",
        "handler_name",
        "split_function_name",
        "handler_table_slot_address",
        "handler_in_code_bin",
        "command_count",
        "scene_count",
        "setup_count",
        "decoded_status_counts",
        "export_structs",
        "open_questions",
        "suggested_gate",
        "ghidra_export_command",
    ]
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        for row in rows:
            writer.writerow(
                {
                    "rank": row["rank"],
                    "priority_score": row["priority_score"],
                    "command_id_hex": row["command_id_hex"],
                    "command_name": row["command_name"],
                    "support_level": row["support_level"],
                    "handler_address": row.get("handler", {}).get("handler_address", ""),
                    "handler_name": row.get("handler", {}).get("handler_name", ""),
                    "split_function_name": row.get("handler", {}).get("split_function_name", ""),
                    "handler_table_slot_address": row.get("handler", {}).get("handler_table_slot_address", ""),
                    "handler_in_code_bin": row.get("handler", {}).get("handler_in_code_bin", ""),
                    "command_count": row["command_count"],
                    "scene_count": row["scene_count"],
                    "setup_count": row["setup_count"],
                    "decoded_status_counts": json.dumps(row["decoded_status_counts"], sort_keys=True),
                    "export_structs": " | ".join(row["export_structs"]),
                    "open_questions": " | ".join(row["open_questions"]),
                    "suggested_gate": row["suggested_gate"],
                    "ghidra_export_command": row.get("ghidra_export_command", ""),
                }
            )


def write_markdown(path: Path, rows: list[dict[str, Any]], summary: dict[str, Any]) -> None:
    lines = [
        "# Scene Command Decompilation Workorders",
        "",
        "This generated queue is derived from native OOT3D ZSI scene indices. It ranks scene commands by the current decompilation evidence state and preserves N64 only as non-promoting semantic context.",
        "",
        "## Summary",
        "",
        f"- Source index: `{summary['source_index']}`",
        f"- Scenes: {summary['scene_count']}",
        f"- Setups: {summary['setup_count']}",
        f"- Commands: {summary['command_total']}",
        f"- Workorders: {summary['workorder_count']}",
        f"- Pending workorders: {summary['pending_workorder_count']}",
        f"- Handler table: `{summary['handler_table']['address']}` in `{summary['handler_table']['code_bin']}`",
        f"- Resolved handler entries: {summary['handler_table']['resolved_handler_count']}",
        "",
        "## Support Levels",
        "",
    ]
    for key, count in summary["support_level_counts"].items():
        lines.append(f"- `{key}`: {count}")

    top = summary.get("top_pending_command")
    if top:
        lines.extend(
            [
                "",
                "## Next Pending Workorder",
                "",
                f"- Command: `{top['command_id_hex']}` `{top['command_name']}`",
                f"- Support: `{top['support_level']}`",
                f"- Occurrences: {top['command_count']}",
                f"- Handler: `{top['handler'].get('handler_address', '')}` `{top['handler'].get('split_function_name', top['handler'].get('handler_name', ''))}`",
                f"- Gate: {top['suggested_gate']}",
                "",
                "Focused Ghidra export:",
                "",
                "```powershell",
                top.get("ghidra_export_command", ""),
                "```",
            ]
        )

    lines.extend(
        [
            "",
            "## Queue",
            "",
            "| Rank | Score | Command | Name | Handler | Support | Count | Scenes | Setups | Structs | Gate |",
            "| ---: | ---: | --- | --- | --- | --- | ---: | ---: | ---: | --- | --- |",
        ]
    )
    for row in rows:
        handler = row.get("handler", {})
        lines.append(
            "| {rank} | {score:.4f} | `{command}` | `{name}` | `{handler_address}`<br>`{handler_name}` | `{support}` | {count} | {scenes} | {setups} | {structs} | {gate} |".format(
                rank=row["rank"],
                score=float(row["priority_score"]),
                command=row["command_id_hex"],
                name=row["command_name"],
                handler_address=handler.get("handler_address", ""),
                handler_name=handler.get("split_function_name", handler.get("handler_name", "")),
                support=row["support_level"],
                count=row["command_count"],
                scenes=row["scene_count"],
                setups=row["setup_count"],
                structs="<br>".join(row["export_structs"]) or "-",
                gate=str(row["suggested_gate"]).replace("|", "\\|"),
            )
        )

    lines.extend(["", "## Samples", ""])
    for row in rows[:8]:
        lines.append(f"### {row['command_id_hex']} {row['command_name']}")
        lines.append("")
        for sample in row["sample_locations"][:4]:
            lines.append(
                "- `{scene}` setup {setup} offset `{offset}` arg `{argument}` param `{parameter}` status `{status}`".format(
                    scene=sample["scene_path"],
                    setup=sample["setup_index"],
                    offset=sample["command_offset_hex"],
                    argument=sample["argument_hex"],
                    parameter=sample["parameter"],
                    status=sample["decoded_status"],
                )
            )
        lines.append("")

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines).rstrip() + "\n", encoding="utf-8")


def write_function_splits(path: Path, rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fields = [
        "entry",
        "old_name",
        "new_name",
        "kind",
        "confidence",
        "source_file",
        "notes",
        "approved",
    ]
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        for row in rows:
            handler = row.get("handler", {})
            entry = str(handler.get("handler_entry", ""))
            if not entry:
                continue
            writer.writerow(
                {
                    "entry": entry,
                    "old_name": handler.get("handler_name", f"FUN_{entry}"),
                    "new_name": handler_split_name(row),
                    "kind": "function",
                    "confidence": "high",
                    "source_file": "analysis/scene_command_decompilation_workorders.md",
                    "notes": (
                        f"Scene command {row['command_id_hex']} {row['command_name']} handler from "
                        f"native code.bin table {handler.get('handler_table_address')} slot "
                        f"{handler.get('handler_table_slot_address')}; support={row['support_level']}."
                    ),
                    "approved": "yes",
                }
            )


def write_entries_file(path: Path, rows: list[dict[str, Any]]) -> None:
    entries = [
        str(row.get("handler", {}).get("handler_entry", ""))
        for row in rows
        if row.get("handler", {}).get("handler_entry")
    ]
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(entries) + "\n", encoding="utf-8")


def main() -> int:
    index = load_index(DEFAULT_INDEX)
    rows, summary = build_workorders(index)
    write_json(DEFAULT_OUT_JSON, rows, summary)
    write_csv(DEFAULT_OUT_CSV, rows)
    write_markdown(DEFAULT_OUT_MD, rows, summary)
    write_function_splits(DEFAULT_SPLITS_CSV, rows)
    write_entries_file(DEFAULT_ENTRIES_TXT, rows)
    print(DEFAULT_OUT_JSON)
    print(DEFAULT_OUT_CSV)
    print(DEFAULT_OUT_MD)
    print(DEFAULT_SPLITS_CSV)
    print(DEFAULT_ENTRIES_TXT)
    print(json.dumps(summary, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
