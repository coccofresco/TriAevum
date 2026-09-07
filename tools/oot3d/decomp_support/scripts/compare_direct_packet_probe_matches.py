#!/usr/bin/env python3
"""Compare compiled direct packet probes against OOT3D target functions."""

from __future__ import annotations

import argparse
import csv
import json
import os
import shutil
import subprocess
from collections import Counter
from pathlib import Path
from typing import Any

from compare_runtime_objects import (
    OBJDUMP_HEADER_RE,
    OBJDUMP_INSN_RE,
    apply_target_aliases,
    compare_ops,
    normalize_op,
    read_manual_symbol_aliases,
    read_target_functions,
)


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_PROBE_JSON = ROOT / "analysis" / "direct_packet_compile_probe.json"
DEFAULT_PORT_MAP = ROOT / "metadata" / "n64_port_map.csv"
DEFAULT_TARGET_DISASSEMBLY = ROOT / "ghidra_export" / "disassembly.txt"
DEFAULT_MANUAL_SYMBOLS = ROOT / "symbols" / "manual_symbols.csv"
DEFAULT_DUMP_DIR = ROOT / "build" / "direct_packet_compile_probe" / "dumps"
DEFAULT_OUT_JSON = ROOT / "analysis" / "direct_packet_probe_match.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "direct_packet_probe_match.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "direct_packet_probe_match.md"

COMPARE_CATEGORIES = {"exact-c", "codegen-near", "structural-near", "semantic-started", "semantic-gap"}


def rel(path: Path) -> str:
    try:
        return str(path.relative_to(ROOT)).replace("\\", "/")
    except ValueError:
        return str(path).replace("\\", "/")


def repo_path(value: str | Path) -> Path:
    path = Path(value)
    if path.is_absolute():
        return path
    return ROOT / path


def read_json(path: Path, default: Any) -> Any:
    if not path.is_file():
        return default
    return json.loads(path.read_text(encoding="utf-8"))


def read_csv(path: Path) -> list[dict[str, str]]:
    if not path.is_file():
        return []
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def write_json(path: Path, data: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def bool_value(value: Any) -> bool:
    if isinstance(value, bool):
        return value
    return str(value).strip().lower() in {"1", "true", "yes"}


def int_value(value: Any) -> int:
    try:
        return int(value or 0)
    except (TypeError, ValueError):
        return 0


def float_value(value: Any) -> float:
    try:
        return float(value or 0.0)
    except (TypeError, ValueError):
        return 0.0


def find_tool(name: str, prefix: str) -> str:
    exe = f"{prefix}-{name}"
    found = shutil.which(exe)
    if found:
        return found

    candidates: list[Path] = []
    devkitarm = os.environ.get("DEVKITARM")
    if devkitarm:
        candidates.append(Path(devkitarm) / "bin" / f"{exe}.exe")
    candidates.append(Path("C:/devkitPro/devkitARM/bin") / f"{exe}.exe")
    for msys in ("mingw64", "ucrt64", "clang64"):
        candidates.append(Path(f"C:/msys64/{msys}/bin") / f"{exe}.exe")

    for candidate in candidates:
        if candidate.is_file():
            return str(candidate)
    raise RuntimeError(f"could not find {exe}")


def dump_path_for(row: dict[str, Any], dump_dir: Path) -> Path:
    dump = str(row.get("dump", ""))
    if dump:
        return repo_path(dump)

    obj = repo_path(str(row.get("object", "")))
    if obj.name:
        return dump_dir / f"{obj.stem}.dump"
    return dump_dir / f"{row.get('entry', 'unknown')}_{row.get('name', 'unknown')}.dump"


def ensure_objdump_dumps(rows: list[dict[str, Any]], args: argparse.Namespace) -> int:
    compiled_rows = [row for row in rows if bool_value(row.get("compiled"))]
    missing = [row for row in compiled_rows if not dump_path_for(row, args.dump_dir).is_file()]
    if not args.refresh_dumps and not missing:
        return 0

    objdump = find_tool("objdump", args.tool_prefix)
    env = os.environ.copy()
    env["PATH"] = str(Path(objdump).parent) + os.pathsep + env.get("PATH", "")
    args.dump_dir.mkdir(parents=True, exist_ok=True)

    refreshed = 0
    for row in compiled_rows:
        dump_path = dump_path_for(row, args.dump_dir)
        if dump_path.is_file() and not args.refresh_dumps:
            continue

        obj = repo_path(str(row.get("object", "")))
        if not obj.is_file():
            continue
        completed = subprocess.run([objdump, "-dr", str(obj)], cwd=ROOT, text=True, capture_output=True, env=env)
        if completed.returncode != 0:
            continue
        dump_path.parent.mkdir(parents=True, exist_ok=True)
        dump_path.write_text(completed.stdout, encoding="utf-8")
        row["dump"] = rel(dump_path)
        refreshed += 1
    return refreshed


def mapped_rows_by_entry(rows: list[dict[str, str]]) -> dict[str, dict[str, str]]:
    return {row.get("oot3d_entry", "").lower(): row for row in rows if row.get("oot3d_entry")}


def read_objdump_file(dump_path: Path) -> dict[str, dict[str, object]]:
    functions: dict[str, dict[str, object]] = {}
    current_name: str | None = None
    for raw_line in dump_path.read_text(encoding="utf-8", errors="replace").splitlines():
        header = OBJDUMP_HEADER_RE.match(raw_line)
        if header:
            current_name = header.group("name")
            functions[current_name] = {
                "dump": rel(dump_path),
                "ops": [],
            }
            continue

        if current_name is None:
            continue

        insn = OBJDUMP_INSN_RE.match(raw_line)
        if insn:
            op = insn.group("op").strip()
            if not op.startswith(".word"):
                functions[current_name]["ops"].append(normalize_op(op))
    return functions


def lane_function(row: dict[str, Any]) -> str:
    lane = str(row.get("lane", ""))
    if "::" in lane:
        return lane.rsplit("::", 1)[1]
    return ""


def first_present_symbol(functions: dict[str, dict[str, object]], candidates: list[str]) -> str:
    seen: set[str] = set()
    for candidate in candidates:
        if not candidate or candidate in seen:
            continue
        seen.add(candidate)
        if candidate in functions:
            return candidate
    return ""


def classify_compare(row: dict[str, Any] | None) -> str:
    if not row:
        return "not-compared"
    if row.get("exact_match"):
        return "exact-c"

    target_count = int_value(row.get("target_instruction_count"))
    compiled_count = int_value(row.get("compiled_instruction_count"))
    count_delta = abs(target_count - compiled_count)
    prefix = int_value(row.get("matching_prefix"))
    suffix = int_value(row.get("matching_suffix"))
    lcs_ratio = float_value(row.get("lcs_target_ratio"))
    longest_run = row.get("longest_common_run", {})
    run_length = int_value(longest_run.get("length")) if isinstance(longest_run, dict) else 0

    if count_delta <= 3 and (lcs_ratio >= 0.50 or prefix >= 5 or suffix >= 5 or run_length >= 8):
        return "codegen-near"
    if lcs_ratio >= 0.35 or run_length >= 5:
        return "structural-near"
    if lcs_ratio >= 0.15 or run_length >= 3:
        return "semantic-started"
    return "semantic-gap"


def first_difference_text(row: dict[str, Any] | None) -> str:
    if not row:
        return ""
    diff = row.get("first_difference")
    if not isinstance(diff, dict):
        return ""
    return f"{diff.get('index')}: {diff.get('target')} vs {diff.get('compiled')}"


def run_text(row: dict[str, Any] | None) -> str:
    if not row:
        return ""
    run = row.get("longest_common_run")
    if not isinstance(run, dict):
        return ""
    return f"{run.get('length', '')}@{run.get('target_index', '')}/{run.get('compiled_index', '')}"


def build_compare_rows(args: argparse.Namespace) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    probe_data = read_json(args.probe_json, {})
    probe_rows = list(probe_data.get("rows", [])) if isinstance(probe_data, dict) else []
    refreshed_dumps = ensure_objdump_dumps(probe_rows, args)

    port_map = mapped_rows_by_entry(read_csv(args.port_map))
    target_functions = apply_target_aliases(
        read_target_functions(args.target_disassembly),
        read_manual_symbol_aliases(args.manual_symbols),
    )
    compiled_by_dump: dict[Path, dict[str, dict[str, object]]] = {}

    rows: list[dict[str, Any]] = []
    for probe in probe_rows:
        entry = str(probe.get("entry", "")).lower()
        mapped = port_map.get(entry, {})
        target_name = mapped.get("oot3d_name") or str(probe.get("name", ""))
        n64_name = mapped.get("n64_name") or lane_function(probe)
        dump_path = dump_path_for(probe, args.dump_dir)
        base = {
            "entry": entry,
            "oot3d_name": target_name,
            "n64_name": n64_name,
            "domain": probe.get("domain", ""),
            "lane": probe.get("lane", ""),
            "map_status": mapped.get("status", ""),
            "n64_source": mapped.get("n64_source", ""),
            "port_file": mapped.get("port_file", ""),
            "packet": probe.get("packet", ""),
            "probe_source": probe.get("probe_source", ""),
            "object": probe.get("object", ""),
            "dump": rel(dump_path),
            "rewrite_count": int_value(probe.get("rewrite_count")),
            "adapter_count": int_value(probe.get("adapter_count")),
            "shape_anchors": int_value(probe.get("shape_anchors")),
            "compile_primary_blocker": probe.get("primary_blocker", ""),
            "compile_error_count": int_value(probe.get("error_count")),
            "compile_warning_count": int_value(probe.get("warning_count")),
            "compiled_probe": bool_value(probe.get("compiled")),
            "dumped_probe": dump_path.is_file(),
        }

        if not bool_value(probe.get("compiled")):
            rows.append({**base, "category": "compile-blocked"})
            continue
        if not dump_path.is_file():
            rows.append({**base, "category": "dump-missing"})
            continue
        if target_name not in target_functions:
            rows.append({**base, "category": "target-missing"})
            continue
        if dump_path not in compiled_by_dump:
            compiled_by_dump[dump_path] = read_objdump_file(dump_path)
        compiled_functions = compiled_by_dump[dump_path]
        compiled_name = first_present_symbol(
            compiled_functions,
            [
                n64_name,
                target_name,
                str(probe.get("name", "")),
                lane_function(probe),
            ],
        )
        if not compiled_name:
            rows.append({**base, "category": "compiled-symbol-missing"})
            continue

        target = target_functions[target_name]
        compiled = compiled_functions[compiled_name]
        compare = compare_ops(target["ops"], compiled["ops"])
        rows.append(
            {
                **base,
                "category": classify_compare(compare),
                "target_entry": target.get("entry", entry),
                "compiled_symbol": compiled_name,
                "compiled_dump": compiled.get("dump", rel(dump_path)),
                "target_instruction_count": int_value(compare.get("target_instruction_count")),
                "compiled_instruction_count": int_value(compare.get("compiled_instruction_count")),
                "matching_prefix": int_value(compare.get("matching_prefix")),
                "matching_suffix": int_value(compare.get("matching_suffix")),
                "lcs_instruction_count": int_value(compare.get("lcs_instruction_count")),
                "lcs_target_ratio": float_value(compare.get("lcs_target_ratio")),
                "longest_common_run": run_text(compare),
                "longest_common_run_raw": compare.get("longest_common_run"),
                "exact_match": bool_value(compare.get("exact_match")),
                "first_difference": first_difference_text(compare),
                "first_difference_raw": compare.get("first_difference"),
            }
        )

    rows.sort(key=sort_key)
    summary = summarize(rows, probe_data, refreshed_dumps)
    return rows, summary


def sort_key(row: dict[str, Any]) -> tuple[int, str, str]:
    order = {
        "exact-c": 0,
        "codegen-near": 1,
        "structural-near": 2,
        "semantic-started": 3,
        "semantic-gap": 4,
        "compiled-symbol-missing": 5,
        "target-missing": 6,
        "dump-missing": 7,
        "compile-blocked": 8,
    }
    return (order.get(str(row.get("category")), 99), str(row.get("domain", "")), str(row.get("entry", "")))


def summarize(rows: list[dict[str, Any]], probe_data: Any, refreshed_dumps: int) -> dict[str, Any]:
    counts = Counter(str(row.get("category", "")) for row in rows)
    compared = sum(counts[category] for category in COMPARE_CATEGORIES)
    exact_instructions = sum(
        int_value(row.get("target_instruction_count")) for row in rows if row.get("category") == "exact-c"
    )
    compared_instructions = sum(
        int_value(row.get("target_instruction_count")) for row in rows if row.get("category") in COMPARE_CATEGORIES
    )
    return {
        "packets": len(rows),
        "compiled_probes": sum(1 for row in rows if row.get("compiled_probe")),
        "dumped_probes": sum(1 for row in rows if row.get("dumped_probe")),
        "compared_probes": compared,
        "exact_c": counts["exact-c"],
        "codegen_near": counts["codegen-near"],
        "structural_near": counts["structural-near"],
        "semantic_started": counts["semantic-started"],
        "semantic_gap": counts["semantic-gap"],
        "compare_blocked": counts["compiled-symbol-missing"] + counts["target-missing"] + counts["dump-missing"],
        "compile_blocked": counts["compile-blocked"],
        "compared_target_instructions": compared_instructions,
        "exact_c_target_instructions": exact_instructions,
        "categories": dict(counts),
        "refreshed_dumps": refreshed_dumps,
        "probe_summary": probe_data.get("summary", {}) if isinstance(probe_data, dict) else {},
    }


def write_csv_report(path: Path, rows: list[dict[str, Any]]) -> None:
    fieldnames = [
        "category",
        "entry",
        "oot3d_name",
        "n64_name",
        "compiled_symbol",
        "domain",
        "target_instruction_count",
        "compiled_instruction_count",
        "matching_prefix",
        "matching_suffix",
        "lcs_instruction_count",
        "lcs_target_ratio",
        "longest_common_run",
        "first_difference",
        "rewrite_count",
        "adapter_count",
        "shape_anchors",
        "compiled_probe",
        "dumped_probe",
        "compile_primary_blocker",
        "compile_error_count",
        "compile_warning_count",
        "n64_source",
        "port_file",
        "map_status",
        "packet",
        "probe_source",
        "object",
        "dump",
        "compiled_dump",
    ]
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow({field: row.get(field, "") for field in fieldnames})


def write_markdown(path: Path, rows: list[dict[str, Any]], summary: dict[str, Any]) -> None:
    lines = [
        "# Direct Packet Probe Match Gate",
        "",
        "This report compares compiled throwaway direct packets against the OOT3D target disassembly.",
        "It uses `metadata/n64_port_map.csv` to compare N64 source function names with their OOT3D target symbols.",
        "",
        "## Summary",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Packets in probe report | {summary['packets']} |",
        f"| Compiled probes | {summary['compiled_probes']} |",
        f"| Dumped probes | {summary['dumped_probes']} |",
        f"| Compared probes | {summary['compared_probes']} |",
        f"| Exact C functions | {summary['exact_c']} |",
        f"| Exact C target instructions | {summary['exact_c_target_instructions']} |",
        f"| Codegen-near functions | {summary['codegen_near']} |",
        f"| Structural-near functions | {summary['structural_near']} |",
        f"| Semantic-started functions | {summary['semantic_started']} |",
        f"| Semantic-gap functions | {summary['semantic_gap']} |",
        f"| Compare-blocked compiled probes | {summary['compare_blocked']} |",
        f"| Compile-blocked packets | {summary['compile_blocked']} |",
        f"| Refreshed dumps | {summary['refreshed_dumps']} |",
        "",
        "## Compared Probe Queue",
        "",
        "| Category | OOT3D | N64 function | Insns | LCS | Longest run | First difference | Dump |",
        "| --- | --- | --- | ---: | ---: | --- | --- | --- |",
    ]

    compared_rows = [row for row in rows if row.get("category") in COMPARE_CATEGORIES]
    if compared_rows:
        for row in compared_rows:
            lines.append(
                f"| `{row['category']}` | `{row['entry']}` `{row['oot3d_name']}` | `{row['n64_name']}` | "
                f"{row.get('target_instruction_count', 0)}/{row.get('compiled_instruction_count', 0)} | "
                f"{float_value(row.get('lcs_target_ratio')):.4f} | {row.get('longest_common_run', '')} | "
                f"`{row.get('first_difference', '')}` | `{row.get('dump', '')}` |"
            )
    else:
        lines.append("| _none_ |  |  |  |  |  |  |  |")

    blocked_compare = [row for row in rows if row.get("category") not in COMPARE_CATEGORIES and row.get("category") != "compile-blocked"]
    if blocked_compare:
        lines.extend(
            [
                "",
                "## Compare Blockers",
                "",
                "| Category | OOT3D | N64 function | Object | Dump |",
                "| --- | --- | --- | --- | --- |",
            ]
        )
        for row in blocked_compare:
            lines.append(
                f"| `{row['category']}` | `{row['entry']}` `{row['oot3d_name']}` | `{row['n64_name']}` | "
                f"`{row.get('object', '')}` | `{row.get('dump', '')}` |"
            )

    compile_blocked = [row for row in rows if row.get("category") == "compile-blocked"]
    if compile_blocked:
        lines.extend(
            [
                "",
                "## Compile Blockers",
                "",
                "| Domain | Packets | Primary blockers |",
                "| --- | ---: | --- |",
            ]
        )
        by_domain: dict[str, list[dict[str, Any]]] = {}
        for row in compile_blocked:
            by_domain.setdefault(str(row.get("domain", "")), []).append(row)
        for domain, domain_rows in sorted(by_domain.items()):
            blockers = Counter(str(row.get("compile_primary_blocker", "")) for row in domain_rows)
            blocker_text = ", ".join(f"{name}: {count}" for name, count in blockers.items() if name) or "none"
            lines.append(f"| `{domain}` | {len(domain_rows)} | {blocker_text} |")

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_reports(args: argparse.Namespace, rows: list[dict[str, Any]], summary: dict[str, Any]) -> None:
    write_json(args.out_json, {"summary": summary, "rows": rows})
    write_csv_report(args.out_csv, rows)
    write_markdown(args.out_md, rows, summary)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe-json", type=Path, default=DEFAULT_PROBE_JSON)
    parser.add_argument("--port-map", type=Path, default=DEFAULT_PORT_MAP)
    parser.add_argument("--target-disassembly", type=Path, default=DEFAULT_TARGET_DISASSEMBLY)
    parser.add_argument("--manual-symbols", type=Path, default=DEFAULT_MANUAL_SYMBOLS)
    parser.add_argument("--dump-dir", type=Path, default=DEFAULT_DUMP_DIR)
    parser.add_argument("--out-json", type=Path, default=DEFAULT_OUT_JSON)
    parser.add_argument("--out-csv", type=Path, default=DEFAULT_OUT_CSV)
    parser.add_argument("--out-md", type=Path, default=DEFAULT_OUT_MD)
    parser.add_argument("--tool-prefix", default="arm-none-eabi")
    parser.add_argument("--refresh-dumps", action="store_true")
    args = parser.parse_args()

    rows, summary = build_compare_rows(args)
    write_reports(args, rows, summary)
    print(
        "direct packet probe match: "
        f"{summary['compared_probes']} compared, {summary['exact_c']} exact, "
        f"{summary['codegen_near']} codegen-near, {summary['structural_near']} structural-near, "
        f"{summary['semantic_started']} semantic-started, {summary['semantic_gap']} semantic-gap"
    )
    print(rel(args.out_md))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
