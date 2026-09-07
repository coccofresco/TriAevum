"""Summarize OOT3D transition request helper evidence.

The exit-list consumer routes through two small helpers at 0x003348E8 and
0x003716F0. This report gathers their bodies, observed callsites, and argument
patterns from focused Ghidra exports so scene-exit semantics can refer to
runtime field roles instead of raw offsets alone.
"""

from __future__ import annotations

import json
import re
from collections import Counter
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
ANALYSIS = ROOT / "analysis"
DEFAULT_OUT_JSON = ANALYSIS / "transition_request_helper_semantics.json"
DEFAULT_OUT_MD = ANALYSIS / "transition_request_helper_semantics.md"

HELPERS = {
    "FUN_003348e8": {
        "entry": "003348e8",
        "role": "request_transition",
        "stores": {
            "arg1": "play+0x5c32 pending entrance/index value",
            "arg2": "play+0x5c2d transition trigger/state byte",
        },
    },
    "FUN_003716f0": {
        "entry": "003716f0",
        "role": "request_transition_with_effect",
        "stores": {
            "arg1": "play+0x5c32 pending entrance/index value",
            "arg2": "play+0x5c2d transition trigger/state byte",
            "arg3": "play+0x5c76 transition effect/type byte",
        },
    },
}

DECOMPILED_DIRS = [
    ANALYSIS / "scene_command_index_consumers_ghidra_export" / "decompiled",
    ANALYSIS / "light_runtime_transition_consumer_ghidra_export" / "decompiled",
    ANALYSIS / "shadow_route_focus_ghidra_export" / "decompiled",
    ANALYSIS / "shadow_runtime_record_helpers_ghidra_export" / "decompiled",
    ANALYSIS / "shadow_factory_context_helpers_ghidra_export" / "decompiled",
    ANALYSIS / "runtime_draw_handle_constructor_ghidra_export" / "decompiled",
    ANALYSIS / "draw_handle_builder_ghidra_export" / "decompiled",
]

FILE_RE = re.compile(r"(?P<ordinal>\d+)_(?P<entry>[0-9a-f]{8})_(?P<name>.+)\.c$", re.IGNORECASE)
CALL_START_RE = re.compile(r"\b(?P<helper>FUN_003348e8|FUN_003716f0)\s*\(")
FIELD_RE = re.compile(r"\+ 0x(?P<offset>5c32|5c2d|5c76)\b", re.IGNORECASE)
DECOMPILED_TYPE_PREFIXES = (
    "bool",
    "byte",
    "char",
    "double",
    "float",
    "int",
    "long",
    "short",
    "uchar",
    "uint",
    "ulong",
    "undefined",
    "undefined1",
    "undefined2",
    "undefined4",
    "undefined8",
    "ushort",
    "void",
)


def split_args(text: str) -> list[str]:
    args: list[str] = []
    current: list[str] = []
    depth = 0
    for char in text:
        if char == "," and depth == 0:
            args.append("".join(current).strip())
            current = []
            continue
        current.append(char)
        if char == "(":
            depth += 1
        elif char == ")" and depth:
            depth -= 1
    tail = "".join(current).strip()
    if tail:
        args.append(tail)
    return args


def file_info(path: Path) -> dict[str, str]:
    match = FILE_RE.match(path.name)
    if not match:
        return {"entry": "", "function": path.stem}
    return {"entry": match.group("entry").lower(), "function": match.group("name")}


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def iter_decompiled_files() -> list[Path]:
    files: list[Path] = []
    seen: set[Path] = set()
    for directory in DECOMPILED_DIRS:
        if not directory.is_dir():
            continue
        for path in sorted(directory.glob("*.c")):
            if path not in seen:
                seen.add(path)
                files.append(path)
    return files


def collect_call_expression(lines: list[str], start: int, helper: str) -> str:
    collected: list[str] = []
    for index in range(start, min(len(lines), start + 8)):
        collected.append(lines[index].strip())
        if ";" in lines[index]:
            break
    text = " ".join(collected)
    prefix = f"{helper}("
    start_index = text.find(prefix)
    if start_index < 0:
        return text
    text = text[start_index + len(prefix) :]
    end_index = text.rfind(")")
    if end_index >= 0:
        text = text[:end_index]
    return " ".join(text.split())


def is_helper_definition_line(line: str, helper: str) -> bool:
    stripped = line.strip()
    helper_offset = stripped.find(helper)
    if helper_offset <= 0:
        return False
    prefix = stripped[:helper_offset].strip()
    if "=" in prefix or "(" in prefix or "," in prefix:
        return False
    first_token = prefix.replace("*", " ").split()[0] if prefix.replace("*", " ").split() else ""
    return first_token in DECOMPILED_TYPE_PREFIXES


def collect_calls() -> list[dict[str, Any]]:
    calls: list[dict[str, Any]] = []
    seen: set[tuple[str, int, str]] = set()
    for path in iter_decompiled_files():
        rel = path.relative_to(ROOT).as_posix()
        info = file_info(path)
        lines = read_text(path).splitlines()
        for line_number, line in enumerate(lines, start=1):
            match = CALL_START_RE.search(line)
            if not match:
                continue
            helper = match.group("helper")
            if is_helper_definition_line(line, helper):
                continue
            key = (rel, line_number, helper)
            if key in seen:
                continue
            seen.add(key)
            expression = collect_call_expression(lines, line_number - 1, helper)
            args = split_args(expression)
            calls.append(
                {
                    "helper": helper,
                    "helper_entry": HELPERS[helper]["entry"],
                    "caller_entry": info["entry"],
                    "caller_function": info["function"],
                    "file": rel,
                    "line": line_number,
                    "args": args,
                    "arg_count": len(args),
                    "expression": expression,
                }
            )
    return calls


def helper_body(helper: str) -> dict[str, Any]:
    entry = HELPERS[helper]["entry"]
    for path in iter_decompiled_files():
        if entry not in path.name.lower():
            continue
        text = read_text(path)
        if helper not in text:
            continue
        offsets = sorted({f"0x{match.group('offset').lower()}" for match in FIELD_RE.finditer(text)})
        return {
            "file": path.relative_to(ROOT).as_posix(),
            "field_offsets": offsets,
            "body_excerpt": "\n".join(text.strip().splitlines()[:16]),
        }
    return {"file": "", "field_offsets": [], "body_excerpt": ""}


def summarize_args(calls: list[dict[str, Any]], arg_index: int) -> dict[str, int]:
    counts: Counter[str] = Counter()
    for call in calls:
        args = call["args"]
        if len(args) > arg_index:
            counts[str(args[arg_index])] += 1
    return dict(sorted(counts.items(), key=lambda item: (-item[1], item[0])))


def build_report() -> dict[str, Any]:
    calls = collect_calls()
    by_helper: dict[str, list[dict[str, Any]]] = {helper: [] for helper in HELPERS}
    for call in calls:
        by_helper.setdefault(call["helper"], []).append(call)

    helpers: dict[str, Any] = {}
    for helper, meta in HELPERS.items():
        helper_calls = by_helper.get(helper, [])
        helpers[helper] = {
            **meta,
            "body": helper_body(helper),
            "call_count": len(helper_calls),
            "caller_count": len({call["caller_entry"] for call in helper_calls if call["caller_entry"]}),
            "arg1_counts": summarize_args(helper_calls, 1),
            "arg2_counts": summarize_args(helper_calls, 2),
            "arg3_counts": summarize_args(helper_calls, 3),
            "sample_calls": helper_calls[:60],
        }

    field_roles = {
        "play+0x5c32": {
            "role": "pending transition entrance/index",
            "evidence": [
                "0x003348E8 and 0x003716F0 refuse to overwrite it when signed value is already non-negative.",
                "0x002E2E60 copies it into a global next-entrance slot during transition completion.",
            ],
        },
        "play+0x5c2d": {
            "role": "pending transition trigger/state byte",
            "evidence": [
                "0x003348E8 and 0x003716F0 store helper arg2 here.",
                "0x002E2E60 starts transition processing when it is nonzero and resets it to zero after handling.",
                "Observed helper calls overwhelmingly pass 0x14.",
            ],
        },
        "play+0x5c76": {
            "role": "transition effect/type byte",
            "evidence": [
                "0x003716F0 stores helper arg3 here.",
                "0x002E2E60 converts it to float local_54 and dispatches transition visual/effect setup from it.",
            ],
        },
    }

    return {
        "format": "oot3d_transition_request_helper_semantics_v1",
        "source_policy": {
            "source": "focused Ghidra decompiled exports from OOT3D code.bin",
            "n64_policy": "N64 names are not used to promote these field roles.",
        },
        "helpers": helpers,
        "field_roles": field_roles,
        "total_call_count": len(calls),
    }


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def write_markdown(path: Path, payload: dict[str, Any]) -> None:
    lines = [
        "# Transition Request Helper Semantics",
        "",
        "Generated from focused OOT3D `code.bin` Ghidra exports.",
        "",
        "## Field Roles",
        "",
        "| Field | Role | Evidence |",
        "| --- | --- | --- |",
    ]
    for field, info in payload["field_roles"].items():
        evidence = "<br>".join(info["evidence"])
        lines.append(f"| `{field}` | {info['role']} | {evidence} |")

    lines.extend(["", "## Helpers", ""])
    for helper, info in payload["helpers"].items():
        lines.extend(
            [
                f"### {helper}",
                "",
                f"- Entry: `0x{info['entry']}`",
                f"- Role: `{info['role']}`",
                f"- Calls found: {info['call_count']}",
                f"- Callers found: {info['caller_count']}",
                f"- Body file: `{info['body']['file']}`",
                "",
                "Stores:",
            ]
        )
        for arg, role in info["stores"].items():
            lines.append(f"- `{arg}` -> {role}")
        lines.extend(["", "Argument value counts:", ""])
        for key in ("arg1_counts", "arg2_counts", "arg3_counts"):
            if info[key]:
                lines.append(f"- `{key}`: `{json.dumps(info[key], sort_keys=True)}`")
        lines.extend(["", "Sample calls:", "", "| Caller | Line | Expression |", "| --- | ---: | --- |"])
        for call in info["sample_calls"][:25]:
            expr = str(call["expression"]).replace("|", "\\|")
            lines.append(f"| `{call['caller_function']}` | {call['line']} | `{expr}` |")
        lines.append("")

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines).rstrip() + "\n", encoding="utf-8")


def main() -> int:
    payload = build_report()
    write_json(DEFAULT_OUT_JSON, payload)
    write_markdown(DEFAULT_OUT_MD, payload)
    print(DEFAULT_OUT_JSON)
    print(DEFAULT_OUT_MD)
    print(json.dumps({"total_call_count": payload["total_call_count"]}, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
