"""Extract a runtime map from decompiled OOT3D scene-command handlers.

The input is the focused Ghidra export produced from the native code.bin scene
command handler table. The output is deliberately evidence-oriented: it records
which command bytes are read, which Play-state offsets are written, and which
helper calls/unresolved symbols still need naming before fields are promoted
from raw native payloads to final source-level structs.
"""

from __future__ import annotations

import json
import re
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_WORKORDERS = ROOT / "analysis" / "scene_command_decompilation_workorders.json"
DEFAULT_EXPORT_DIR = ROOT / "analysis" / "scene_command_handlers_ghidra_export"
DEFAULT_OUT_JSON = ROOT / "analysis" / "scene_command_handler_runtime_map.json"
DEFAULT_OUT_MD = ROOT / "analysis" / "scene_command_handler_runtime_map.md"

HANDLER_FILE_RE = re.compile(
    r"(?P<entry>[0-9a-f]{8})_oot3d_scene_cmd_(?P<command_id>[0-9a-f]{2})_(?P<name>.+)\.c$",
    re.IGNORECASE,
)
COMMAND_READ_RE = re.compile(
    r"\*\((?:byte|char|short|ushort|int|uint|undefined1|undefined2|undefined4)[^)]*\)"
    r"\(param_3 \+ (?P<offset>0x[0-9a-fA-F]+|\d+)\)"
)
CALL_RE = re.compile(r"\b(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s*\(")
DAT_RE = re.compile(r"\bDAT_[0-9a-fA-F]+\b")
FUN_RE = re.compile(r"\bFUN_[0-9a-fA-F]+\b")


def load_workorders(path: Path) -> dict[str, dict[str, Any]]:
    if not path.is_file():
        raise FileNotFoundError(f"{path}: workorder JSON not found")
    payload = json.loads(path.read_text(encoding="utf-8"))
    result: dict[str, dict[str, Any]] = {}
    for row in payload.get("workorders", []):
        handler = row.get("handler") if isinstance(row, dict) else None
        if not isinstance(handler, dict):
            continue
        entry = str(handler.get("handler_entry", "")).lower()
        if entry:
            result[entry] = row
    return result


def normalize_statement(statement: str) -> str:
    normalized = " ".join(statement.replace("\r", "\n").split())
    if "{" in normalized:
        normalized = normalized.rsplit("{", 1)[1].strip()
    return normalized


def unique(values: list[str]) -> list[str]:
    seen: set[str] = set()
    result: list[str] = []
    for value in values:
        if value and value not in seen:
            seen.add(value)
            result.append(value)
    return result


def offset_value(raw: str) -> int:
    raw = raw.strip()
    return int(raw, 16) if raw.lower().startswith("0x") else int(raw)


def extract_command_reads(content: str) -> list[dict[str, Any]]:
    offsets = sorted({offset_value(match.group("offset")) for match in COMMAND_READ_RE.finditer(content)})
    return [{"offset": offset, "offset_hex": f"0x{offset:02x}"} for offset in offsets]


def classify_store_target(left: str) -> dict[str, str]:
    play_offset = re.search(r"param_2 \+ (?P<offset>0x[0-9a-fA-F]+|\d+)", left)
    if play_offset:
        offset = offset_value(play_offset.group("offset"))
        return {"kind": "play_offset", "offset_hex": f"0x{offset:x}"}

    symbolic = re.search(r"(?P<symbol>DAT_[0-9a-fA-F]+) \+ param_2", left)
    if symbolic:
        return {"kind": "play_symbolic_offset", "symbol": symbolic.group("symbol")}

    symbolic = re.search(r"param_2 \+ (?P<symbol>DAT_[0-9a-fA-F]+)", left)
    if symbolic:
        return {"kind": "play_symbolic_offset", "symbol": symbolic.group("symbol")}

    if "param_2" in left:
        return {"kind": "play_expression"}
    return {"kind": "other"}


def extract_runtime_stores(content: str) -> list[dict[str, Any]]:
    stores: list[dict[str, Any]] = []
    for statement in content.split(";"):
        normalized = normalize_statement(statement)
        if "param_2" not in normalized or "=" not in normalized:
            continue
        equals_index = normalized.find("=")
        store_index = normalized.rfind("*(", 0, equals_index)
        if store_index < 0:
            continue
        left = normalized[store_index:equals_index]
        right = normalized[equals_index + 1 :]
        target = classify_store_target(left)
        if target["kind"] == "other":
            continue
        stores.append(
            {
                "target": left.strip(),
                "value": right.strip(),
                **target,
            }
        )
    return stores


def extract_scene_relative_expressions(content: str) -> list[str]:
    statements: list[str] = []
    for statement in content.split(";"):
        normalized = normalize_statement(statement)
        if "param_1 + *(int *)(param_3 + 4)" in normalized:
            statements.append(normalized)
        elif "*(int *)(param_3 + 4) + param_1" in normalized:
            statements.append(normalized)
    return unique(statements)


def extract_calls(content: str, function_name: str) -> list[str]:
    ignored = {
        "if",
        "for",
        "while",
        "do",
        "return",
        "sizeof",
        "switch",
        function_name,
    }
    calls = [match.group("name") for match in CALL_RE.finditer(content)]
    return unique([name for name in calls if name not in ignored and not name.startswith("undefined")])


def build_runtime_map(
    export_dir: Path = DEFAULT_EXPORT_DIR,
    workorders_path: Path = DEFAULT_WORKORDERS,
) -> dict[str, Any]:
    workorders = load_workorders(workorders_path)
    decompiled_dir = export_dir / "decompiled"
    if not decompiled_dir.is_dir():
        raise FileNotFoundError(f"{decompiled_dir}: decompiled handler directory not found")

    handlers: list[dict[str, Any]] = []
    for path in sorted(decompiled_dir.glob("*_oot3d_scene_cmd_*.c")):
        match = HANDLER_FILE_RE.search(path.name)
        if not match:
            continue
        entry = match.group("entry").lower()
        command_id = int(match.group("command_id"), 16)
        command_name = match.group("name")
        function_name = f"oot3d_scene_cmd_{command_id:02x}_{command_name}"
        content = path.read_text(encoding="utf-8")
        workorder = workorders.get(entry, {})
        unresolved = unique(DAT_RE.findall(content) + FUN_RE.findall(content))
        stores = extract_runtime_stores(content)
        handlers.append(
            {
                "command_id": command_id,
                "command_id_hex": f"0x{command_id:02x}",
                "command_name": command_name,
                "handler_entry": entry,
                "handler_name": function_name,
                "support_level": workorder.get("support_level", "unknown"),
                "runtime_store_count": len(stores),
                "runtime_stores": stores,
                "command_reads": extract_command_reads(content),
                "scene_relative_pointer_expressions": extract_scene_relative_expressions(content),
                "calls": extract_calls(content, function_name),
                "unresolved_symbols": unresolved,
                "decompiled_file": str(path.relative_to(ROOT)),
            }
        )

    handlers.sort(key=lambda row: int(row["command_id"]))
    return {
        "format": "oot3d_scene_command_handler_runtime_map_v1",
        "source_policy": {
            "native_code_source": "OOT3D ExeFS code.bin imported into Ghidra",
            "handler_table": "0x0053CC84",
            "asset_source": "Generated scene command workorders from native OOT3D ZSI indices",
            "n64_policy": "N64 source may be used for semantic hints only; this map is extracted from OOT3D code.bin output.",
        },
        "source_export_dir": str(export_dir),
        "workorders": str(workorders_path),
        "handler_count": len(handlers),
        "handlers": handlers,
    }


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def write_markdown(path: Path, payload: dict[str, Any]) -> None:
    lines = [
        "# Scene Command Handler Runtime Map",
        "",
        "This generated report is extracted from the focused Ghidra export of OOT3D `code.bin` scene-command handlers. It is an evidence map for promoting native ZSI scene commands into source-level data definitions.",
        "",
        "## Summary",
        "",
        f"- Handler table: `{payload['source_policy']['handler_table']}`",
        f"- Export dir: `{payload['source_export_dir']}`",
        f"- Handler count: {payload['handler_count']}",
        "",
        "## Handlers",
        "",
        "| Command | Handler | Reads | Stores | Calls | Unresolved |",
        "| --- | --- | --- | --- | --- | --- |",
    ]
    for handler in payload["handlers"]:
        reads = ", ".join(f"`{row['offset_hex']}`" for row in handler["command_reads"]) or "-"
        stores = ", ".join(
            f"`{store.get('offset_hex', store.get('symbol', store['kind']))}`" for store in handler["runtime_stores"]
        ) or "-"
        calls = ", ".join(f"`{call}`" for call in handler["calls"]) or "-"
        unresolved = ", ".join(f"`{symbol}`" for symbol in handler["unresolved_symbols"]) or "-"
        lines.append(
            f"| `{handler['command_id_hex']}` `{handler['command_name']}` | `{handler['handler_entry']}` `{handler['handler_name']}` | {reads} | {stores} | {calls} | {unresolved} |"
        )

    lines.extend(["", "## Runtime Stores", ""])
    for handler in payload["handlers"]:
        lines.append(f"### {handler['command_id_hex']} {handler['command_name']}")
        lines.append("")
        if handler["scene_relative_pointer_expressions"]:
            lines.append("Scene-relative pointer expressions:")
            for expression in handler["scene_relative_pointer_expressions"]:
                lines.append(f"- `{expression}`")
            lines.append("")
        if handler["runtime_stores"]:
            lines.append("| Target | Value |")
            lines.append("| --- | --- |")
            for store in handler["runtime_stores"]:
                lines.append(f"| `{store['target']}` | `{store['value']}` |")
        else:
            lines.append("- No direct `param_2` runtime store extracted.")
        lines.append("")

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines).rstrip() + "\n", encoding="utf-8")


def main() -> int:
    payload = build_runtime_map()
    write_json(DEFAULT_OUT_JSON, payload)
    write_markdown(DEFAULT_OUT_MD, payload)
    print(DEFAULT_OUT_JSON)
    print(DEFAULT_OUT_MD)
    print(json.dumps({"handler_count": payload["handler_count"]}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
