#!/usr/bin/env python3
"""Materialize and compile-probe direct split/helper source candidates."""

from __future__ import annotations

import argparse
import csv
import json
import os
import re
import subprocess
from collections import Counter
from pathlib import Path
from typing import Any

from compare_direct_packet_probe_matches import (
    classify_compare,
    first_difference_text,
    read_objdump_file,
    rel,
    run_text,
)
from compare_runtime_objects import (
    apply_target_aliases,
    compare_ops,
    read_manual_symbol_aliases,
    read_target_functions,
)
from probe_direct_packet_compilability import (
    MELEE_OFFSET_SYMBOLS,
    balance_probe_braces,
    compile_command,
    extra_probe_declarations,
    find_tool,
    normalize_probe_source,
    probe_prelude,
)


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_WORKORDERS = ROOT / "analysis" / "direct_split_workorders.csv"
DEFAULT_TARGET_DISASSEMBLY = ROOT / "ghidra_export" / "disassembly.txt"
DEFAULT_MANUAL_SYMBOLS = ROOT / "symbols" / "manual_symbols.csv"
DEFAULT_SNIPPET_DIR = ROOT / "analysis" / "direct_split_candidate_sources"
DEFAULT_BUILD_DIR = ROOT / "build" / "direct_split_candidate_probes"
DEFAULT_OUT_JSON = ROOT / "analysis" / "direct_split_candidate_probe.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "direct_split_candidate_probe.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "direct_split_candidate_probe.md"


DIRECT_FIELD_MACRO = """\
#ifndef OOT3D_DIRECT_FIELD
#define OOT3D_DIRECT_FIELD(base, offset) /* direct-field: base + offset */ (base)
#endif
"""

D_TOKEN_RE = re.compile(r"\bD_[A-Za-z0-9_]+\b")
S_TOKEN_RE = re.compile(r"\bs[A-Za-z0-9_]+\b")
KNOWN_PROBE_DATA_TOKENS = {
    "D_80853D4C",
    "D_80854384",
    "D_80854380",
    "D_80854368",
    "D_80854360",
    "D_80854370",
    "D_80854378",
    "D_80854190",
    "D_80854528",
}


def read_csv(path: Path) -> list[dict[str, str]]:
    if not path.is_file():
        return []
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def write_json(path: Path, data: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


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


def safe_stem(*parts: str) -> str:
    text = "__".join(part for part in parts if part)
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", text).strip("_")


def repo_path(value: str | Path) -> Path:
    path = Path(value)
    if path.is_absolute():
        return path
    return ROOT / path


def selected_workorders(args: argparse.Namespace) -> list[dict[str, str]]:
    rows = read_csv(args.workorders)
    selected = []
    wanted_entries = {entry.lower().removeprefix("0x").zfill(8) for entry in args.entry}
    for row in rows:
        if args.complete_only and row.get("source_span_status") != "complete":
            continue
        if wanted_entries and row.get("entry", "").lower() not in wanted_entries:
            continue
        selected.append(row)
    if args.limit:
        selected = selected[: args.limit]
    return selected


def extract_snippet(row: dict[str, str]) -> str:
    packet = repo_path(row.get("packet", ""))
    start = int_value(row.get("source_start_line"))
    end = int_value(row.get("source_end_line"))
    if not packet.is_file() or start <= 0 or end < start:
        raise ValueError(f"invalid source span for {row.get('entry')} {row.get('candidate_symbol')}")
    lines = packet.read_text(encoding="utf-8", errors="replace").splitlines()
    snippet = "\n".join(lines[start - 1 : end]) + "\n"
    return snippet


def write_snippet(row: dict[str, str], snippet_dir: Path) -> Path:
    snippet_dir.mkdir(parents=True, exist_ok=True)
    stem = safe_stem(row.get("entry", ""), row.get("oot3d_name", ""), row.get("candidate_symbol", ""))
    path = snippet_dir / f"{stem}.c"
    header = [
        "/*",
        " * Generated split/helper C candidate.",
        f" * OOT3D: {row.get('entry', '')} {row.get('oot3d_name', '')}",
        f" * Candidate: {row.get('candidate_symbol', '')}",
        f" * Source: {row.get('source', '')}",
        " * This is analysis input, not maintained build source.",
        " */",
        "",
    ]
    path.write_text("\n".join(header) + extract_snippet(row), encoding="utf-8")
    return path


def write_probe_source(row: dict[str, str], snippet: str, source_dir: Path, prelude: str) -> Path:
    source_dir.mkdir(parents=True, exist_ok=True)
    stem = safe_stem(row.get("entry", ""), row.get("oot3d_name", ""), row.get("candidate_symbol", ""))
    path = source_dir / f"{stem}.probe.c"
    normalized = balance_probe_braces(normalize_probe_source(DIRECT_FIELD_MACRO + "\n" + snippet))
    path.write_text(
        prelude + "\n" + extra_split_candidate_declarations(normalized) + extra_probe_declarations(normalized) + normalized,
        encoding="utf-8",
    )
    return path


def token_is_defined(text: str, token: str) -> bool:
    if re.search(rf"\b(static|extern)\b[^;=]*\b{re.escape(token)}\b", text):
        return True
    if re.search(rf"\b{re.escape(token)}\s*\[", text) and re.search(
        rf"\b(static|extern)\b[^;=]*\b{re.escape(token)}\s*\[", text
    ):
        return True
    return False


def extra_split_candidate_declarations(text: str) -> str:
    lines: list[str] = []
    generated: set[str] = set()

    for token in sorted(set(D_TOKEN_RE.findall(text))):
        if token in KNOWN_PROBE_DATA_TOKENS:
            continue
        if token_is_defined(text, token):
            continue
        lines.append(f"static s32 {token}[16];")
        generated.add(token)

    for token in sorted(set(S_TOKEN_RE.findall(text))):
        if token in generated or token_is_defined(text, token):
            continue
        if token in MELEE_OFFSET_SYMBOLS:
            continue
        if token.endswith("AnimSfxList"):
            lines.append(f"static AnimSfxEntry {token}[8];")

    if not lines:
        return ""
    return "\n".join(lines) + "\n"


def compile_candidate(
    row: dict[str, str],
    snippet: str,
    args: argparse.Namespace,
    gcc: str,
    objdump: str,
    prelude: str,
    target_functions: dict[str, dict[str, object]],
) -> dict[str, Any]:
    stem = safe_stem(row.get("entry", ""), row.get("oot3d_name", ""), row.get("candidate_symbol", ""))
    source = write_probe_source(row, snippet, args.build_dir / "sources", prelude)
    obj = args.build_dir / "objects" / f"{stem}.o"
    dump = args.build_dir / "dumps" / f"{stem}.dump"
    stderr_log = args.build_dir / "logs" / f"{stem}.stderr.txt"
    stdout_log = args.build_dir / "logs" / f"{stem}.stdout.txt"
    for directory in (obj.parent, dump.parent, stderr_log.parent):
        directory.mkdir(parents=True, exist_ok=True)
    for stale in (obj, dump):
        if stale.is_file():
            stale.unlink()

    env = os.environ.copy()
    env["PATH"] = str(Path(gcc).parent) + os.pathsep + env.get("PATH", "")
    completed = subprocess.run(
        compile_command(gcc, source, obj, args.optimization),
        cwd=ROOT,
        text=True,
        capture_output=True,
        env=env,
    )
    stderr_log.write_text(completed.stderr, encoding="utf-8")
    stdout_log.write_text(completed.stdout, encoding="utf-8")
    compiled = completed.returncode == 0 and obj.is_file()
    dumped = False
    category = "compile-blocked"
    compare: dict[str, Any] = {}
    first_error = ""

    if compiled:
        dumped_result = subprocess.run([objdump, "-dr", str(obj)], cwd=ROOT, text=True, capture_output=True, env=env)
        dump.write_text(dumped_result.stdout, encoding="utf-8")
        dumped = dumped_result.returncode == 0 and dump.is_file()
    else:
        for line in completed.stderr.splitlines():
            if "error:" in line:
                first_error = line.strip()
                break

    if dumped:
        target = target_functions.get(row.get("oot3d_name", ""))
        compiled_functions = read_objdump_file(dump)
        compiled_function = compiled_functions.get(row.get("candidate_symbol", ""))
        if target and compiled_function:
            compare = compare_ops(list(target["ops"]), list(compiled_function["ops"]))
            category = classify_compare(compare)
        elif not target:
            category = "target-missing"
        else:
            category = "compiled-symbol-missing"

    return {
        "category": category,
        "entry": row.get("entry", ""),
        "oot3d_name": row.get("oot3d_name", ""),
        "mapped_n64_name": row.get("mapped_n64_name", ""),
        "candidate_symbol": row.get("candidate_symbol", ""),
        "reason": row.get("reason", ""),
        "priority": int_value(row.get("priority")),
        "source_span_status": row.get("source_span_status", ""),
        "source_nonblank_lines": int_value(row.get("source_nonblank_lines")),
        "source": row.get("source", ""),
        "snippet": rel(args.snippet_dir / f"{stem}.c"),
        "probe_source": rel(source),
        "object": rel(obj),
        "dump": rel(dump),
        "stderr_log": rel(stderr_log),
        "compiled": compiled,
        "dumped": dumped,
        "returncode": completed.returncode,
        "target_instruction_count": int_value(compare.get("target_instruction_count")),
        "compiled_instruction_count": int_value(compare.get("compiled_instruction_count")),
        "matching_prefix": int_value(compare.get("matching_prefix")),
        "matching_suffix": int_value(compare.get("matching_suffix")),
        "lcs_instruction_count": int_value(compare.get("lcs_instruction_count")),
        "lcs_target_ratio": float_value(compare.get("lcs_target_ratio")),
        "longest_common_run": run_text(compare) if compare else "",
        "first_difference": first_difference_text(compare) if compare else first_error,
    }


def write_csv_report(path: Path, rows: list[dict[str, Any]]) -> None:
    fieldnames = [
        "category",
        "entry",
        "oot3d_name",
        "candidate_symbol",
        "reason",
        "priority",
        "source_nonblank_lines",
        "compiled",
        "dumped",
        "target_instruction_count",
        "compiled_instruction_count",
        "matching_prefix",
        "matching_suffix",
        "lcs_instruction_count",
        "lcs_target_ratio",
        "longest_common_run",
        "first_difference",
        "source",
        "snippet",
        "probe_source",
        "object",
        "dump",
        "stderr_log",
    ]
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow({field: row.get(field, "") for field in fieldnames})


def summarize(rows: list[dict[str, Any]]) -> dict[str, Any]:
    categories = Counter(str(row.get("category", "")) for row in rows)
    return {
        "candidates": len(rows),
        "compiled": sum(1 for row in rows if row.get("compiled")),
        "dumped": sum(1 for row in rows if row.get("dumped")),
        "compile_blocked": categories["compile-blocked"],
        "exact_c": categories["exact-c"],
        "codegen_near": categories["codegen-near"],
        "structural_near": categories["structural-near"],
        "semantic_started": categories["semantic-started"],
        "semantic_gap": categories["semantic-gap"],
        "categories": dict(sorted(categories.items())),
        "entries": len({row.get("entry", "") for row in rows}),
        "candidate_symbols": len({row.get("candidate_symbol", "") for row in rows}),
    }


def write_md(path: Path, rows: list[dict[str, Any]], summary: dict[str, Any]) -> None:
    category_text = ", ".join(f"{key}: {value}" for key, value in summary["categories"].items()) or "none"
    lines = [
        "# Direct Split Candidate Probe",
        "",
        "This report materializes complete split/helper source spans from direct packet workorders, compiles them as standalone C probes, and compares each candidate symbol with its OOT3D target.",
        "",
        "## Summary",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Candidate spans | {summary['candidates']} |",
        f"| OOT3D entries covered | {summary['entries']} |",
        f"| Candidate symbols | {summary['candidate_symbols']} |",
        f"| Compiled candidates | {summary['compiled']} |",
        f"| Dumped candidates | {summary['dumped']} |",
        f"| Compile-blocked candidates | {summary['compile_blocked']} |",
        f"| Exact C candidates | {summary['exact_c']} |",
        f"| Codegen-near candidates | {summary['codegen_near']} |",
        f"| Structural-near candidates | {summary['structural_near']} |",
        f"| Semantic-started candidates | {summary['semantic_started']} |",
        f"| Semantic-gap candidates | {summary['semantic_gap']} |",
        "",
        f"- Categories: {category_text}",
        "",
        "## Candidate Queue",
        "",
        "| Category | OOT3D | Candidate | Reason | Insns | LCS | Run | Source | Snippet | First difference |",
        "| --- | --- | --- | --- | ---: | ---: | --- | --- | --- | --- |",
    ]
    for row in rows:
        lines.append(
            f"| `{row['category']}` | `{row['entry']}` `{row['oot3d_name']}` | `{row['candidate_symbol']}` | "
            f"`{row['reason']}` | {row['target_instruction_count']}/{row['compiled_instruction_count']} | "
            f"{float_value(row['lcs_target_ratio']):.4f} | {row['longest_common_run']} | "
            f"`{row['source']}` | `{row['snippet']}` | `{row['first_difference']}` |"
        )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workorders", type=Path, default=DEFAULT_WORKORDERS)
    parser.add_argument("--target-disassembly", type=Path, default=DEFAULT_TARGET_DISASSEMBLY)
    parser.add_argument("--manual-symbols", type=Path, default=DEFAULT_MANUAL_SYMBOLS)
    parser.add_argument("--snippet-dir", type=Path, default=DEFAULT_SNIPPET_DIR)
    parser.add_argument("--build-dir", type=Path, default=DEFAULT_BUILD_DIR)
    parser.add_argument("--out-json", type=Path, default=DEFAULT_OUT_JSON)
    parser.add_argument("--out-csv", type=Path, default=DEFAULT_OUT_CSV)
    parser.add_argument("--out-md", type=Path, default=DEFAULT_OUT_MD)
    parser.add_argument("--optimization", default="-O2")
    parser.add_argument("--tool-prefix", default="arm-none-eabi")
    parser.add_argument("--entry", action="append", default=[])
    parser.add_argument("--limit", type=int, default=0)
    parser.add_argument("--complete-only", action=argparse.BooleanOptionalAction, default=True)
    args = parser.parse_args()

    workorders = selected_workorders(args)
    if not workorders:
        raise SystemExit("no split candidate workorders selected")

    target_functions = apply_target_aliases(
        read_target_functions(args.target_disassembly),
        read_manual_symbol_aliases(args.manual_symbols),
    )
    gcc = find_tool("gcc", args.tool_prefix)
    objdump = find_tool("objdump", args.tool_prefix)
    prelude = probe_prelude()

    rows = []
    for row in workorders:
        snippet = extract_snippet(row)
        write_snippet(row, args.snippet_dir)
        rows.append(compile_candidate(row, snippet, args, gcc, objdump, prelude, target_functions))

    rows.sort(key=sort_key)
    summary = summarize(rows)
    write_json(args.out_json, {"summary": summary, "rows": rows})
    write_csv_report(args.out_csv, rows)
    write_md(args.out_md, rows, summary)
    print(
        "direct split candidate probe: "
        f"{summary['compiled']}/{summary['candidates']} compiled, "
        f"{summary['exact_c']} exact, {summary['codegen_near']} codegen-near, "
        f"{summary['structural_near']} structural-near"
    )
    print(rel(args.out_md))
    return 0


def sort_key(row: dict[str, Any]) -> tuple[int, int, str, str]:
    order = {
        "exact-c": 0,
        "codegen-near": 1,
        "structural-near": 2,
        "semantic-started": 3,
        "semantic-gap": 4,
        "compiled-symbol-missing": 5,
        "target-missing": 6,
        "compile-blocked": 7,
    }
    return (
        order.get(str(row.get("category", "")), 99),
        -int_value(row.get("priority")),
        str(row.get("entry", "")),
        str(row.get("candidate_symbol", "")),
    )


if __name__ == "__main__":
    raise SystemExit(main())
