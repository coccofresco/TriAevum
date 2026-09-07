#!/usr/bin/env python3
"""Summarize body-shape evidence for unresolved tail-call target identities."""

from __future__ import annotations

import argparse
import csv
import json
import re
from pathlib import Path
from typing import Any

from classify_direct_tail_call_target_identities import (
    DEFAULT_DISASSEMBLY,
    DEFAULT_TARGETS,
    disassembly_callsite_index,
    normalize_addr,
    read_json,
)
from probe_direct_source_subregions import rel
from sweep_direct_source_subregion_ranges import list_value
from synthesize_target_disassembly import DEFAULT_CODE_BIN, DEFAULT_OBJDUMP, disassemble_range


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_IDENTITY = ROOT / "analysis" / "direct_tail_call_target_identity.json"
DEFAULT_OUT_JSON = ROOT / "analysis" / "direct_tail_call_target_body_shapes.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "direct_tail_call_target_body_shapes.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "direct_tail_call_target_body_shapes.md"
CALL_RE = re.compile(r"\bblx?\s+(?P<dest>0x[0-9a-fA-F]+)\b")
BRANCH_RE = re.compile(r"\bb(?:x|l|l?x|eq|ne|cs|cc|mi|pl|vs|vc|hi|ls|ge|lt|gt|le)?\s+(?P<dest>0x[0-9a-fA-F]+)\b")
MOV_IMM_RE = re.compile(r"\bmov(?:s|ne|eq|ls|hi)?\s+r1,\s*#(?P<value>\d+)\b")


def write_json(path: Path, data: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8", newline="\n")


def write_csv(path: Path, rows: list[dict[str, Any]], fieldnames: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow({field: row.get(field, "") for field in fieldnames})


def target_addrs(identity: dict[str, Any], seed_targets: dict[str, Any]) -> list[str]:
    result: list[str] = []
    for row in list_value(identity.get("rows", [])):
        if isinstance(row, dict) and row.get("target_addr"):
            result.append(normalize_addr(row.get("target_addr")))
    for row in list_value(seed_targets.get("rows", [])):
        if not isinstance(row, dict):
            continue
        if row.get("target_destination_class") == "external-target-call":
            result.append(normalize_addr(row.get("target_destination_addr")))
    return sorted(set(result))


def trim_body(lines: list[str]) -> list[str]:
    result: list[str] = []
    saw_pop_lr = False
    for line in lines:
        result.append(line)
        op = line.split(":", 1)[1].strip().lower() if ":" in line else line.lower()
        if op.startswith("pop") and "pc" in op:
            break
        if op.startswith("pop") and "lr" in op:
            saw_pop_lr = True
            continue
        if saw_pop_lr and op.startswith("b\t"):
            break
    return result


def disassemble_body(code: bytes, code_base: int, objdump: Path, addr: str, bytes_to_read: int) -> list[str]:
    entry = int(addr, 16)
    offset = entry - code_base
    if offset < 0 or offset >= len(code):
        return []
    lines = disassemble_range(objdump, code[offset : offset + bytes_to_read], entry, bytes_to_read)
    return trim_body(lines)


def body_features(addr: str, body: list[str]) -> dict[str, Any]:
    ops = [line.split(":", 1)[1].strip().lower() for line in body if ":" in line]
    calls: list[str] = []
    branches: list[str] = []
    for op in ops:
        call = CALL_RE.search(op)
        if call:
            calls.append(normalize_addr(call.group("dest")))
        branch = BRANCH_RE.search(op)
        if branch and not op.startswith("bl"):
            branches.append(normalize_addr(branch.group("dest")))
    action_state_shape = (
        any("ldrb" in op and "#496" in op for op in ops)
        and any("cmp" in op and "r0, r1" in op for op in ops)
        and any("strb" in op and ("r6" in op or "r1" in op) and "#496" in op for op in ops)
    )
    tail_branch = branches[-1] if branches and ops and ops[-1].startswith("b\t") else ""
    has_tail_to_action_state = tail_branch == "00340bdc"
    debug_guard_shape = (
        any("tst" in op and "#1" in op for op in ops)
        and "003679b4" in calls
        and "0036788c" in calls
    )
    if addr == "00340bdc" and action_state_shape:
        assessment = "reject-play_get_camera-state-mode-helper"
        blocker = "body updates a state/mode byte and side-effect table; positional Play_GetCamera evidence is unsafe"
    elif addr == "003725e0" and has_tail_to_action_state:
        assessment = "possible-func_80839ffc-family-wrapper"
        blocker = "wrapper ends by setting action-state 0 through 00340bdc, but selected action identity is not fully recovered"
    else:
        assessment = "unresolved-body-shape"
        blocker = "body shape is not yet tied to a known N64 helper"
    return {
        "target_addr": addr,
        "instruction_count": len(body),
        "calls": ";".join(calls),
        "branches": ";".join(branches),
        "tail_branch": tail_branch,
        "has_action_state_shape": action_state_shape,
        "has_debug_guard_shape": debug_guard_shape,
        "has_tail_to_action_state_helper": has_tail_to_action_state,
        "assessment": assessment,
        "body_blocker": blocker,
        "body_excerpt": " | ".join(body[:18]),
    }


def callsite_arg_summary(callsites: list[dict[str, str]], disassembly: Path, target_addr: str) -> dict[str, Any]:
    if not disassembly.is_file():
        return {"r1_immediates": "", "player_function_r1_immediates": "", "sample_callsites": ""}
    lines = disassembly.read_text(encoding="utf-8", errors="replace").splitlines()
    by_addr = {normalize_addr(line.split(":", 1)[0]): i for i, line in enumerate(lines) if re.match(r"^[0-9a-fA-F]{8}:", line)}
    all_values: list[str] = []
    player_values: list[str] = []
    for site in callsites:
        site_addr = normalize_addr(site.get("callsite_addr"))
        idx = by_addr.get(site_addr)
        value = ""
        if idx is not None:
            for prev in reversed(lines[max(0, idx - 4) : idx]):
                if ":" not in prev:
                    continue
                match = MOV_IMM_RE.search(prev.lower())
                if match:
                    value = match.group("value")
                    break
        if value:
            all_values.append(value)
            if normalize_addr(site.get("function_entry")) == "00473ef8":
                player_values.append(value)
    return {
        "r1_immediates": ";".join(sorted(set(all_values), key=lambda item: int(item))),
        "player_function_r1_immediates": ";".join(sorted(set(player_values), key=lambda item: int(item))),
        "sample_callsites": ";".join(
            f"{row.get('callsite_addr', '')}@{row.get('function_name', '')}" for row in callsites[:10]
        ),
    }


def build_report(args: argparse.Namespace) -> dict[str, Any]:
    identity = read_json(args.identity, {})
    seed_targets = read_json(args.targets, {})
    callsite_by_dest, _ = disassembly_callsite_index(args.disassembly)
    code = args.code_bin.read_bytes()
    code_base = int(str(args.code_base), 0)
    rows: list[dict[str, Any]] = []
    for addr in target_addrs(identity, seed_targets):
        body = disassemble_body(code, code_base, args.objdump, addr, args.bytes)
        row = body_features(addr, body)
        row.update(callsite_arg_summary(callsite_by_dest.get(addr, []), args.disassembly, addr))
        row["global_callsite_count"] = len(callsite_by_dest.get(addr, []))
        row["player_function_callsite_count"] = sum(
            1 for site in callsite_by_dest.get(addr, []) if normalize_addr(site.get("function_entry")) == "00473ef8"
        )
        rows.append(row)
    unsafe_positional = sum(1 for row in rows if str(row.get("assessment", "")).startswith("reject-"))
    partial_wrappers = sum(1 for row in rows if str(row.get("assessment", "")).startswith("possible-"))
    summary = {
        "targets": len(rows),
        "unsafe_positional_identities": unsafe_positional,
        "partial_wrapper_candidates": partial_wrappers,
        "state_mode_helpers": sum(1 for row in rows if row.get("has_action_state_shape")),
        "tail_to_state_mode_helpers": sum(1 for row in rows if row.get("has_tail_to_action_state_helper")),
        "next_gate": "Resolve the state/mode table and wrapper identities before promoting the tail-call seed.",
    }
    return {
        "format": "oot3d_direct_tail_call_target_body_shapes_v1",
        "inputs": {
            "identity": rel(args.identity),
            "targets": rel(args.targets),
            "disassembly": rel(args.disassembly),
            "code_bin": str(args.code_bin),
        },
        "summary": summary,
        "rows": rows,
    }


def write_markdown(path: Path, data: dict[str, Any]) -> None:
    summary = data["summary"]
    lines = [
        "# Direct Tail-Call Target Body Shapes",
        "",
        "This report inspects the bodies of unresolved OOT3D tail-call targets so positional matches do not become unsafe names.",
        "",
        "## Summary",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Targets | {summary['targets']} |",
        f"| Unsafe positional identities | {summary['unsafe_positional_identities']} |",
        f"| Partial wrapper candidates | {summary['partial_wrapper_candidates']} |",
        f"| State/mode helpers | {summary['state_mode_helpers']} |",
        f"| Tail-to-state/mode helpers | {summary['tail_to_state_mode_helpers']} |",
        "",
        f"Next gate: {summary['next_gate']}",
        "",
        "## Targets",
        "",
        "| Target | Assessment | Calls | Tail branch | Global calls | Player calls | r1 immediates | Blocker |",
        "| --- | --- | --- | --- | ---: | ---: | --- | --- |",
    ]
    for row in data["rows"]:
        lines.append(
            f"| `{row.get('target_addr', '')}` | `{row.get('assessment', '')}` | "
            f"`{row.get('calls', '')}` | `{row.get('tail_branch', '')}` | "
            f"{row.get('global_callsite_count', '')} | {row.get('player_function_callsite_count', '')} | "
            f"`{row.get('player_function_r1_immediates', '')}` | {row.get('body_blocker', '')} |"
        )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--identity", type=Path, default=DEFAULT_IDENTITY)
    parser.add_argument("--targets", type=Path, default=DEFAULT_TARGETS)
    parser.add_argument("--disassembly", type=Path, default=DEFAULT_DISASSEMBLY)
    parser.add_argument("--code-bin", type=Path, default=DEFAULT_CODE_BIN)
    parser.add_argument("--code-base", default="0x00100000")
    parser.add_argument("--objdump", type=Path, default=DEFAULT_OBJDUMP)
    parser.add_argument("--bytes", type=int, default=0x180)
    parser.add_argument("--out-json", type=Path, default=DEFAULT_OUT_JSON)
    parser.add_argument("--out-csv", type=Path, default=DEFAULT_OUT_CSV)
    parser.add_argument("--out-md", type=Path, default=DEFAULT_OUT_MD)
    args = parser.parse_args()

    if not args.code_bin.is_file():
        raise SystemExit(f"missing code image: {args.code_bin}")
    if not args.objdump.is_file():
        raise SystemExit(f"missing objdump: {args.objdump}")

    data = build_report(args)
    fields = [
        "target_addr",
        "assessment",
        "instruction_count",
        "calls",
        "branches",
        "tail_branch",
        "has_action_state_shape",
        "has_debug_guard_shape",
        "has_tail_to_action_state_helper",
        "global_callsite_count",
        "player_function_callsite_count",
        "r1_immediates",
        "player_function_r1_immediates",
        "sample_callsites",
        "body_blocker",
        "body_excerpt",
    ]
    write_json(args.out_json, data)
    write_csv(args.out_csv, data["rows"], fields)
    write_markdown(args.out_md, data)
    summary = data["summary"]
    print(
        "direct tail-call target body shapes: "
        f"{summary['targets']} targets, {summary['unsafe_positional_identities']} unsafe positional, "
        f"{summary['partial_wrapper_candidates']} partial wrappers"
    )
    print(str(args.out_md.relative_to(ROOT)).replace("\\", "/"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
