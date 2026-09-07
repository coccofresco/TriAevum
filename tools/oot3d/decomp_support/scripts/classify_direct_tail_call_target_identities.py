#!/usr/bin/env python3
"""Classify unresolved OOT3D call targets from the direct tail-call seed report."""

from __future__ import annotations

import argparse
import csv
import json
import re
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any

from probe_direct_source_subregions import rel
from sweep_direct_source_subregion_ranges import int_value, list_value


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_TARGETS = ROOT / "analysis" / "direct_tail_call_seed_targets.json"
DEFAULT_ADAPTERS = ROOT / "metadata" / "n64_to_oot3d_adapters.csv"
DEFAULT_MANUAL_SYMBOLS = ROOT / "symbols" / "manual_symbols.csv"
DEFAULT_DISASSEMBLY = ROOT / "ghidra_export" / "disassembly.txt"
DEFAULT_OUT_JSON = ROOT / "analysis" / "direct_tail_call_target_identity.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "direct_tail_call_target_identity.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "direct_tail_call_target_identity.md"
HEADER_RE = re.compile(r"^//\s+(?P<name>\S+)\s+@\s+(?P<entry>[0-9a-fA-F]+)\s*$")
INSN_RE = re.compile(r"^(?P<addr>[0-9a-fA-F]{8}):\s+(?P<op>.+?)\s*$")
CALL_RE = re.compile(r"\bblx?\s+(?P<dest>0x[0-9a-fA-F]+)\b")


def read_json(path: Path, default: Any) -> Any:
    if not path.is_file():
        return default
    value = json.loads(path.read_text(encoding="utf-8-sig"))
    return value if isinstance(value, dict) else default


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


def normalize_addr(value: Any) -> str:
    text = str(value or "").strip().lower()
    if text.startswith("0x"):
        text = text[2:]
    return text.zfill(8)


def fun_name(addr: str) -> str:
    return f"FUN_{normalize_addr(addr)}"


def read_manual_symbols(path: Path) -> dict[str, str]:
    result: dict[str, str] = {}
    if not path.is_file():
        return result
    with path.open(newline="", encoding="utf-8-sig") as handle:
        for row in csv.DictReader(handle):
            if row.get("kind") == "function":
                result[normalize_addr(row.get("entry"))] = str(row.get("new_name", "") or row.get("old_name", ""))
    return result


def read_adapter_rows(path: Path) -> dict[str, list[dict[str, str]]]:
    result: dict[str, list[dict[str, str]]] = defaultdict(list)
    if not path.is_file():
        return result
    with path.open(newline="", encoding="utf-8-sig") as handle:
        for row in csv.DictReader(handle):
            target = str(row.get("oot3d_symbol", ""))
            if target.startswith("FUN_"):
                result[target.lower()].append(row)
    return result


def disassembly_callsite_index(path: Path) -> tuple[dict[str, list[dict[str, str]]], dict[str, tuple[str, str]]]:
    callsites: dict[str, list[dict[str, str]]] = defaultdict(list)
    function_by_addr: dict[str, tuple[str, str]] = {}
    current_name = ""
    current_entry = ""
    if not path.is_file():
        return callsites, function_by_addr
    for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
        header = HEADER_RE.match(raw)
        if header:
            current_name = header.group("name")
            current_entry = normalize_addr(header.group("entry"))
            continue
        insn = INSN_RE.match(raw)
        if not insn:
            continue
        addr = normalize_addr(insn.group("addr"))
        function_by_addr[addr] = (current_entry, current_name)
        op = insn.group("op").strip()
        call = CALL_RE.search(op.lower())
        if call:
            dest = normalize_addr(call.group("dest"))
            callsites[dest].append(
                {
                    "callsite_addr": addr,
                    "function_entry": current_entry,
                    "function_name": current_name,
                    "op": op,
                }
            )
    return callsites, function_by_addr


def seed_positional_rows(data: dict[str, Any]) -> dict[str, list[dict[str, Any]]]:
    result: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for row in list_value(data.get("rows", [])):
        if not isinstance(row, dict):
            continue
        if row.get("target_destination_class") == "external-target-call":
            result[normalize_addr(row.get("target_destination_addr"))].append(row)
    return result


def classify_target(
    addr: str,
    *,
    manual_symbols: dict[str, str],
    adapter_rows: dict[str, list[dict[str, str]]],
    positional_rows: list[dict[str, Any]],
    callsites: list[dict[str, str]],
) -> dict[str, Any]:
    name = manual_symbols.get(addr, "")
    adapters = adapter_rows.get(fun_name(addr).lower(), [])
    adapter_n64 = sorted(
        {
            str(row.get("n64_symbol", "") or row.get("n64_pattern", ""))
            for row in adapters
            if row.get("n64_symbol") or row.get("n64_pattern")
        }
    )
    adapter_notes = sorted({row.get("notes", "") for row in adapters if row.get("notes")})
    positional_symbols = sorted(
        {
            str(row.get("compiled_relocation_symbol", ""))
            for row in positional_rows
            if row.get("compiled_relocation_symbol")
        }
    )
    positional_match_rows = sum(1 for row in positional_rows if row.get("normalized_match"))
    player_callsites = [row for row in callsites if row.get("function_entry") == "00473ef8"]

    if name:
        identity_class = "manual-symbol-known"
        confidence = "high"
        blocker = ""
    elif adapters and positional_symbols and not set(adapter_n64).intersection(positional_symbols):
        identity_class = "known-adapter-conflicts-with-tail-slot"
        confidence = "medium"
        blocker = "adapter identity conflicts with positional tail-call slot"
    elif adapters:
        identity_class = "known-adapter-target"
        confidence = "medium"
        blocker = ""
    elif positional_symbols:
        identity_class = "unresolved-positional-candidate"
        confidence = "low"
        blocker = "no manual symbol or adapter evidence for target address"
    else:
        identity_class = "unresolved-target"
        confidence = "low"
        blocker = "no identity evidence beyond callsite frequency"

    return {
        "target_addr": addr,
        "target_fun_name": fun_name(addr),
        "identity_class": identity_class,
        "confidence": confidence,
        "manual_symbol": name,
        "adapter_n64_symbols": ";".join(adapter_n64),
        "adapter_notes": ";".join(adapter_notes),
        "positional_compiled_symbols": ";".join(positional_symbols),
        "positional_rows": len(positional_rows),
        "positional_match_rows": positional_match_rows,
        "global_callsite_count": len(callsites),
        "player_function_callsite_count": len(player_callsites),
        "player_function_callsites": ";".join(row.get("callsite_addr", "") for row in player_callsites),
        "sample_callsites": ";".join(
            f"{row.get('callsite_addr', '')}@{row.get('function_name', '')}" for row in callsites[:8]
        ),
        "blocker": blocker,
    }


def build_report(args: argparse.Namespace) -> dict[str, Any]:
    targets = read_json(args.targets, {})
    external_addrs = [
        normalize_addr(addr)
        for addr in list_value(targets.get("summary", {}).get("target_unknown_external_call_addrs", []))
    ]
    # Include all external target rows, even if future symbol data removes them from the unknown list.
    for addr in seed_positional_rows(targets):
        if addr not in external_addrs:
            external_addrs.append(addr)
    callsite_by_dest, _ = disassembly_callsite_index(args.disassembly)
    manual_symbols = read_manual_symbols(args.manual_symbols)
    adapters = read_adapter_rows(args.adapters)
    positional_by_dest = seed_positional_rows(targets)
    rows = [
        classify_target(
            addr,
            manual_symbols=manual_symbols,
            adapter_rows=adapters,
            positional_rows=positional_by_dest.get(addr, []),
            callsites=callsite_by_dest.get(addr, []),
        )
        for addr in sorted(set(external_addrs))
    ]
    classes = Counter(str(row.get("identity_class", "")) for row in rows)
    unresolved = [row for row in rows if str(row.get("identity_class", "")).startswith("unresolved")]
    conflicts = [row for row in rows if "conflict" in str(row.get("identity_class", ""))]
    summary = {
        "targets": len(rows),
        "identity_classes": dict(sorted(classes.items())),
        "unresolved_targets": len(unresolved),
        "conflicting_targets": len(conflicts),
        "known_targets": sum(1 for row in rows if str(row.get("identity_class", "")).startswith("known")),
        "manual_symbol_targets": sum(1 for row in rows if row.get("manual_symbol")),
        "next_gate": "Resolve unresolved target identities or exclude conflicting adapter targets before naming tail-call slots.",
    }
    return {
        "format": "oot3d_direct_tail_call_target_identity_v1",
        "inputs": {
            "targets": rel(args.targets),
            "adapters": rel(args.adapters),
            "manual_symbols": rel(args.manual_symbols),
            "disassembly": rel(args.disassembly),
        },
        "summary": summary,
        "rows": rows,
    }


def write_markdown(path: Path, data: dict[str, Any]) -> None:
    summary = data["summary"]
    lines = [
        "# Direct Tail-Call Target Identity",
        "",
        "This report classifies unresolved OOT3D call targets from the tail-call seed using manual symbols, adapter metadata, and callsite frequency.",
        "",
        "## Summary",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Targets | {summary['targets']} |",
        f"| Known targets | {summary['known_targets']} |",
        f"| Unresolved targets | {summary['unresolved_targets']} |",
        f"| Conflicting targets | {summary['conflicting_targets']} |",
        "",
        "## Targets",
        "",
        "| Target | Class | Confidence | Adapter N64 | Positional symbols | Global calls | Player calls | Blocker |",
        "| --- | --- | --- | --- | --- | ---: | ---: | --- |",
    ]
    for row in data["rows"]:
        lines.append(
            f"| `{row.get('target_addr', '')}` `{row.get('target_fun_name', '')}` | "
            f"`{row.get('identity_class', '')}` | {row.get('confidence', '')} | "
            f"`{row.get('adapter_n64_symbols', '')}` | `{row.get('positional_compiled_symbols', '')}` | "
            f"{row.get('global_callsite_count', '')} | {row.get('player_function_callsite_count', '')} | "
            f"{row.get('blocker', '')} |"
        )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--targets", type=Path, default=DEFAULT_TARGETS)
    parser.add_argument("--adapters", type=Path, default=DEFAULT_ADAPTERS)
    parser.add_argument("--manual-symbols", type=Path, default=DEFAULT_MANUAL_SYMBOLS)
    parser.add_argument("--disassembly", type=Path, default=DEFAULT_DISASSEMBLY)
    parser.add_argument("--out-json", type=Path, default=DEFAULT_OUT_JSON)
    parser.add_argument("--out-csv", type=Path, default=DEFAULT_OUT_CSV)
    parser.add_argument("--out-md", type=Path, default=DEFAULT_OUT_MD)
    args = parser.parse_args()

    data = build_report(args)
    fields = [
        "target_addr",
        "target_fun_name",
        "identity_class",
        "confidence",
        "manual_symbol",
        "adapter_n64_symbols",
        "adapter_notes",
        "positional_compiled_symbols",
        "positional_rows",
        "positional_match_rows",
        "global_callsite_count",
        "player_function_callsite_count",
        "player_function_callsites",
        "sample_callsites",
        "blocker",
    ]
    write_json(args.out_json, data)
    write_csv(args.out_csv, data["rows"], fields)
    write_markdown(args.out_md, data)
    summary = data["summary"]
    print(
        "direct tail-call target identity: "
        f"{summary['targets']} targets, {summary['known_targets']} known, "
        f"{summary['unresolved_targets']} unresolved, {summary['conflicting_targets']} conflicting"
    )
    print(str(args.out_md.relative_to(ROOT)).replace("\\", "/"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
