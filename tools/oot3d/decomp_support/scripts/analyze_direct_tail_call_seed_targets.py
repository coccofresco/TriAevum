#!/usr/bin/env python3
"""Resolve raw target call/branch destinations for the best tail-call seed."""

from __future__ import annotations

import argparse
import csv
import json
import re
from pathlib import Path
from typing import Any

from compare_runtime_objects import normalize_op
from probe_direct_source_subregions import rel
from sweep_direct_source_subregion_ranges import float_value, int_value, list_value, repo_path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_QUALIFICATION = ROOT / "analysis" / "direct_control_flow_subblock_seed_qualification.json"
DEFAULT_DISASSEMBLY = ROOT / "ghidra_export" / "disassembly.txt"
DEFAULT_MANUAL_SYMBOLS = ROOT / "symbols" / "manual_symbols.csv"
DEFAULT_OUT_JSON = ROOT / "analysis" / "direct_tail_call_seed_targets.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "direct_tail_call_seed_targets.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "direct_tail_call_seed_targets.md"
DISASM_RE = re.compile(r"^(?P<addr>[0-9a-fA-F]{8}):\s+(?P<op>.+?)\s*$")
DUMP_HEADER_RE = re.compile(r"^[0-9a-fA-F]+\s+<(?P<name>[^>]+)>:\s*$")
DUMP_INSN_RE = re.compile(r"^\s*(?P<offset>[0-9a-fA-F]+):\s+(?:[0-9a-fA-F]{2,8}\s+)+\t(?P<op>.+?)\s*$")
DUMP_RELOC_RE = re.compile(r"^\s*(?P<offset>[0-9a-fA-F]+):\s+R_ARM_(?:CALL|JUMP24)\s+(?P<symbol>\S+)\s*$")
DEST_RE = re.compile(r"\b(?P<mnemonic>b[a-z]*|blx?)\s+(?P<dest>0x[0-9a-fA-F]+)\b")


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


def read_symbol_names(path: Path) -> dict[str, str]:
    names: dict[str, str] = {}
    if not path.is_file():
        return names
    with path.open(newline="", encoding="utf-8-sig") as handle:
        for row in csv.DictReader(handle):
            if row.get("kind") == "function":
                names[normalize_addr(row.get("entry"))] = str(row.get("new_name", "") or row.get("old_name", ""))
    return names


def best_seed(data: dict[str, Any]) -> dict[str, Any]:
    summary = data.get("summary") if isinstance(data.get("summary"), dict) else {}
    wanted_range = str(summary.get("best_seed_source_range", ""))
    wanted_addr = normalize_addr(summary.get("best_seed_target_start_addr", ""))
    for row in list_value(data.get("rows", [])):
        if not isinstance(row, dict):
            continue
        if str(row.get("source_range", "")) == wanted_range and normalize_addr(row.get("target_start_addr")) == wanted_addr:
            return row
    rows = [row for row in list_value(data.get("rows", [])) if isinstance(row, dict)]
    return rows[0] if rows else {}


def read_raw_target_slice(path: Path, start_addr: str, end_addr: str) -> list[dict[str, Any]]:
    start = int(normalize_addr(start_addr), 16)
    end = int(normalize_addr(end_addr), 16)
    rows: list[dict[str, Any]] = []
    if not path.is_file():
        return rows
    for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = DISASM_RE.match(raw)
        if not match:
            continue
        addr = int(match.group("addr"), 16)
        if start <= addr <= end:
            op = match.group("op").strip()
            dest = branch_destination(op)
            rows.append(
                {
                    "slice_index": len(rows),
                    "addr": f"{addr:08x}",
                    "raw_op": op,
                    "normalized_op": normalize_op(op),
                    "branch_mnemonic": dest[0],
                    "destination_addr": dest[1],
                }
            )
    return rows


def branch_destination(op: str) -> tuple[str, str]:
    match = DEST_RE.search(op.strip().lower())
    if not match:
        return "", ""
    return match.group("mnemonic"), normalize_addr(match.group("dest"))


def read_compiled_slice(path: Path, function_name: str, skip: int, count: int) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    reloc_by_offset: dict[str, str] = {}
    if not path.is_file():
        return rows
    current = False
    for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
        header = DUMP_HEADER_RE.match(raw)
        if header:
            current = header.group("name") == function_name
            continue
        if not current:
            continue
        reloc = DUMP_RELOC_RE.match(raw)
        if reloc:
            reloc_by_offset[reloc.group("offset").lower()] = reloc.group("symbol")
            continue
        insn = DUMP_INSN_RE.match(raw)
        if insn:
            op = insn.group("op").strip()
            if not op.startswith(".word"):
                rows.append(
                    {
                        "ordinal": len(rows),
                        "offset": insn.group("offset").lower(),
                        "raw_op": op,
                        "normalized_op": normalize_op(op),
                    }
                )
    sliced = rows[skip : skip + count]
    for row in sliced:
        row["relocation_symbol"] = reloc_by_offset.get(str(row.get("offset", "")).lower(), "")
        row["slice_index"] = len([item for item in sliced if int_value(item.get("ordinal")) < int_value(row.get("ordinal"))])
    return sliced


def classify_target_destination(mnemonic: str, addr: str, target_start: str, target_end: str) -> str:
    if not addr:
        return ""
    if mnemonic in {"bl", "blx"}:
        return "external-target-call"
    value = int(normalize_addr(addr), 16)
    start = int(normalize_addr(target_start), 16)
    end = int(normalize_addr(target_end), 16)
    if start <= value <= end:
        return "local-target-slice"
    return "local-target-branch"


def build_rows(seed: dict[str, Any], symbols: dict[str, str], target_rows: list[dict[str, Any]], compiled_rows: list[dict[str, Any]]) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    count = max(len(target_rows), len(compiled_rows))
    for index in range(count):
        target = target_rows[index] if index < len(target_rows) else {}
        compiled = compiled_rows[index] if index < len(compiled_rows) else {}
        dest = str(target.get("destination_addr", ""))
        mnemonic = str(target.get("branch_mnemonic", ""))
        rows.append(
            {
                "slice_index": index,
                "source_range": seed.get("source_range", ""),
                "target_addr": target.get("addr", ""),
                "target_raw_op": target.get("raw_op", ""),
                "target_normalized_op": target.get("normalized_op", ""),
                "target_destination_addr": dest,
                "target_destination_symbol": symbols.get(normalize_addr(dest), "") if dest else "",
                "target_destination_class": classify_target_destination(
                    mnemonic,
                    dest,
                    seed.get("target_start_addr", ""),
                    seed.get("target_end_addr", ""),
                ),
                "compiled_offset": compiled.get("offset", ""),
                "compiled_raw_op": compiled.get("raw_op", ""),
                "compiled_normalized_op": compiled.get("normalized_op", ""),
                "compiled_relocation_symbol": compiled.get("relocation_symbol", ""),
                "normalized_match": target.get("normalized_op", "") == compiled.get("normalized_op", ""),
            }
        )
    return rows


def build_report(args: argparse.Namespace) -> dict[str, Any]:
    qualification = read_json(args.qualification, {})
    seed = best_seed(qualification)
    symbols = read_symbol_names(args.manual_symbols)
    target_rows = read_raw_target_slice(args.disassembly, seed.get("target_start_addr", ""), seed.get("target_end_addr", ""))
    compiled_rows = read_compiled_slice(
        repo_path(seed.get("dump", "")),
        str(seed.get("function_name", "")),
        int_value(seed.get("compiled_skip")),
        int_value(seed.get("compare_instruction_count")),
    )
    rows = build_rows(seed, symbols, target_rows, compiled_rows)
    external_targets = sorted(
        {
            row.get("target_destination_addr", "")
            for row in rows
            if row.get("target_destination_class") == "external-target-call"
        }
    )
    local_branch_targets = sorted(
        {
            row.get("target_destination_addr", "")
            for row in rows
            if row.get("target_destination_class") in {"local-target-slice", "local-target-branch"}
        }
    )
    unknown_external_targets = [
        addr for addr in external_targets if not symbols.get(normalize_addr(addr), "")
    ]
    compiled_relocations = sorted({row.get("compiled_relocation_symbol", "") for row in rows if row.get("compiled_relocation_symbol")})
    summary = {
        "seed_source_range": seed.get("source_range", ""),
        "seed_target_range": f"{seed.get('target_start_addr', '')}-{seed.get('target_end_addr', '')}",
        "seed_class": seed.get("seed_class", ""),
        "seed_lcs_window_ratio": seed.get("lcs_window_ratio", 0.0),
        "rows": len(rows),
        "target_branch_or_call_rows": sum(1 for row in rows if row.get("target_destination_addr")),
        "target_external_call_targets": len(external_targets),
        "target_unknown_external_call_targets": len(unknown_external_targets),
        "target_unknown_external_call_addrs": unknown_external_targets,
        "target_local_branch_targets": len(local_branch_targets),
        "target_local_branch_addrs": local_branch_targets,
        "compiled_relocation_symbols": compiled_relocations,
        "normalized_match_rows": sum(1 for row in rows if row.get("normalized_match")),
        "next_gate": "Name or classify unknown OOT3D call targets, then rerun branch/tail isolation with call identity evidence.",
    }
    return {
        "format": "oot3d_direct_tail_call_seed_targets_v1",
        "inputs": {
            "qualification": rel(args.qualification),
            "disassembly": rel(args.disassembly),
            "manual_symbols": rel(args.manual_symbols),
        },
        "summary": summary,
        "rows": rows,
    }


def write_markdown(path: Path, data: dict[str, Any]) -> None:
    summary = data["summary"]
    lines = [
        "# Direct Tail-Call Seed Targets",
        "",
        "This report resolves raw target branch/call destinations for the best qualified tail-call seed.",
        "",
        "## Summary",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Rows | {summary['rows']} |",
        f"| Target branch/call rows | {summary['target_branch_or_call_rows']} |",
        f"| External target call destinations | {summary['target_external_call_targets']} |",
        f"| Unknown external target call destinations | {summary['target_unknown_external_call_targets']} |",
        f"| Local target branch destinations | {summary['target_local_branch_targets']} |",
        f"| Normalized positional matches | {summary['normalized_match_rows']} |",
        "",
        "## Rows",
        "",
        "| Index | Target | Dest | Dest symbol | Compiled | Relocation | Match |",
        "| ---: | --- | --- | --- | --- | --- | ---: |",
    ]
    for row in data["rows"]:
        lines.append(
            f"| {row.get('slice_index', '')} | `{row.get('target_addr', '')}: {row.get('target_raw_op', '')}` | "
            f"`{row.get('target_destination_addr', '')}` | `{row.get('target_destination_symbol', '')}` | "
            f"`{row.get('compiled_offset', '')}: {row.get('compiled_raw_op', '')}` | "
            f"`{row.get('compiled_relocation_symbol', '')}` | {row.get('normalized_match', '')} |"
        )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qualification", type=Path, default=DEFAULT_QUALIFICATION)
    parser.add_argument("--disassembly", type=Path, default=DEFAULT_DISASSEMBLY)
    parser.add_argument("--manual-symbols", type=Path, default=DEFAULT_MANUAL_SYMBOLS)
    parser.add_argument("--out-json", type=Path, default=DEFAULT_OUT_JSON)
    parser.add_argument("--out-csv", type=Path, default=DEFAULT_OUT_CSV)
    parser.add_argument("--out-md", type=Path, default=DEFAULT_OUT_MD)
    args = parser.parse_args()

    data = build_report(args)
    fields = [
        "slice_index",
        "source_range",
        "target_addr",
        "target_raw_op",
        "target_normalized_op",
        "target_destination_addr",
        "target_destination_symbol",
        "target_destination_class",
        "compiled_offset",
        "compiled_raw_op",
        "compiled_normalized_op",
        "compiled_relocation_symbol",
        "normalized_match",
    ]
    write_json(args.out_json, data)
    write_csv(args.out_csv, data["rows"], fields)
    write_markdown(args.out_md, data)
    summary = data["summary"]
    print(
        "direct tail-call seed targets: "
        f"{summary['target_external_call_targets']} external targets, "
        f"{summary['target_unknown_external_call_targets']} unknown, "
        f"{summary['normalized_match_rows']}/{summary['rows']} positional matches"
    )
    print(str(args.out_md.relative_to(ROOT)).replace("\\", "/"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
