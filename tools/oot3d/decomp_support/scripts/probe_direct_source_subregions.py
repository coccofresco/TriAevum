#!/usr/bin/env python3
"""Compile and compare source subregion probes suggested by the direct plan."""

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

from compare_direct_packet_probe_matches import read_objdump_file
from compare_direct_target_window_probes import classify_window, first_difference_text, run_text
from compare_runtime_objects import compare_ops
from probe_direct_packet_compilability import (
    analyze_stderr,
    compile_command,
    extra_probe_declarations,
    find_tool,
    normalize_probe_source,
    probe_prelude,
)
from refine_direct_data_like_boundaries import read_indexed_ops


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_PLAN = ROOT / "analysis" / "direct_source_subregion_plan.json"
DEFAULT_SEMANTIC = ROOT / "analysis" / "direct_semantic_call_shape.json"
DEFAULT_BUILD = ROOT / "build" / "direct_source_subregion_probes"
DEFAULT_OUT_JSON = ROOT / "analysis" / "direct_source_subregion_probe.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "direct_source_subregion_probe.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "direct_source_subregion_probe.md"
SOURCE_LINE_RE = re.compile(r"^\s*(?P<line>\d+):\s(?P<text>.*)$")


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


def list_value(value: Any) -> list[Any]:
    return value if isinstance(value, list) else []


def repo_path(value: Any) -> Path:
    path = Path(str(value or ""))
    return path if path.is_absolute() else ROOT / path


def rel(path: Path | str) -> str:
    p = Path(path)
    try:
        return str(p.relative_to(ROOT)).replace("\\", "/")
    except ValueError:
        return str(p).replace("\\", "/")


def safe_name(value: str) -> str:
    return re.sub(r"[^A-Za-z0-9_]+", "_", value).strip("_")


def read_source_lines(path: Path) -> dict[int, str]:
    rows: dict[int, str] = {}
    if not path.is_file():
        return rows
    for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = SOURCE_LINE_RE.match(raw)
        if match:
            rows[int(match.group("line"))] = match.group("text")
    return rows


def brace_delta(line: str) -> int:
    text = re.sub(r"//.*$", "", line)
    return text.count("{") - text.count("}")


def sanitize_range_lines(lines: list[str]) -> list[str]:
    cleaned: list[str] = []
    depth = 0
    for raw in lines:
        text = raw.rstrip()
        stripped = text.strip()
        if not stripped:
            cleaned.append("")
            continue
        if depth == 0 and stripped.startswith("} else if"):
            text = text.replace("} else if", "if", 1)
            stripped = text.strip()
        elif depth == 0 and stripped.startswith("} else"):
            text = re.sub(r"^\s*}\s*else\s*{", "if (1) {", text, count=1)
            stripped = text.strip()
        elif depth == 0 and stripped.startswith("}"):
            continue
        cleaned.append(text)
        depth += brace_delta(text)
        if depth < 0:
            depth = 0
    cleaned.extend("}" for _ in range(depth))
    return cleaned


def semantic_index(data: dict[str, Any]) -> dict[tuple[str, str], dict[str, Any]]:
    result: dict[tuple[str, str], dict[str, Any]] = {}
    for row in list_value(data.get("rows", [])):
        if isinstance(row, dict):
            result[(str(row.get("entry", "")).lower(), str(row.get("candidate_symbol", "")))] = row
    return result


def target_slice_for(row: dict[str, Any]) -> list[str]:
    source_excerpt = repo_path(row.get("source_excerpt", ""))
    manifest = read_json(source_excerpt.with_name("probe_manifest.json"), {})
    target_rows = read_indexed_ops(repo_path(manifest.get("target_window_ops", "")))
    target_skip = int_value(row.get("target_skip"))
    count = int_value(row.get("slice_instruction_count"))
    return [str(item.get("op", "")) for item in target_rows[target_skip : target_skip + count]]


def source_for_probe(
    prelude: str,
    function_name: str,
    source_lines: dict[int, str],
    start_line: int,
    end_line: int,
) -> str:
    selected = [source_lines[line] for line in range(start_line, end_line + 1) if line in source_lines]
    body = "\n".join("    " + line for line in sanitize_range_lines(selected))
    function = f"void {function_name}(Player* this, PlayState* play) {{\n{body}\n}}\n"
    normalized = normalize_probe_source(function)
    return prelude + "\n" + extra_probe_declarations(normalized) + subregion_extra_declarations(normalized) + normalized


def subregion_extra_declarations(text: str) -> str:
    lines: list[str] = []
    for token in ("D_80854A58", "D_80854A64", "D_80854A70"):
        if re.search(rf"\b{token}\b", text):
            lines.append(f"static LinkAnimationHeader* {token}[3];")
    if re.search(r"\bD_80854A7C\b", text):
        lines.append("static u8 D_80854A7C[3];")
    if re.search(r"\bD_80854A3C\b", text):
        lines.append("static AnimSfxEntry D_80854A3C[3];")
    if re.search(r"\btalkActor\b", text) and not re.search(r"\bActor\s*\*\s*talkActor\b", text):
        lines.append("static Actor* talkActor;")
    return "\n".join(lines) + ("\n" if lines else "")


def compile_probe(
    source_text: str,
    function_name: str,
    build_dir: Path,
    gcc: str,
    objdump: str,
    optimization: str,
) -> dict[str, Any]:
    source_dir = build_dir / "sources"
    object_dir = build_dir / "objects"
    dump_dir = build_dir / "dumps"
    log_dir = build_dir / "logs"
    for path in (source_dir, object_dir, dump_dir, log_dir):
        path.mkdir(parents=True, exist_ok=True)
    source_path = source_dir / f"{function_name}.probe.c"
    object_path = object_dir / f"{function_name}.o"
    dump_path = dump_dir / f"{function_name}.dump"
    stderr_path = log_dir / f"{function_name}.stderr.txt"
    stdout_path = log_dir / f"{function_name}.stdout.txt"
    objdump_stderr_path = log_dir / f"{function_name}.objdump.stderr.txt"
    for path in (object_path, dump_path):
        if path.is_file():
            path.unlink()
    source_path.write_text(source_text, encoding="utf-8", newline="\n")

    env = os.environ.copy()
    env["PATH"] = str(Path(gcc).parent) + os.pathsep + env.get("PATH", "")
    completed = subprocess.run(
        compile_command(gcc, source_path, object_path, optimization),
        cwd=ROOT,
        text=True,
        capture_output=True,
        env=env,
    )
    stderr_path.write_text(completed.stderr, encoding="utf-8")
    stdout_path.write_text(completed.stdout, encoding="utf-8")
    analysis = analyze_stderr(completed.stderr)
    compiled = completed.returncode == 0 and object_path.is_file()
    dumped = False
    objdump_returncode: int | None = None
    if compiled:
        dump_completed = subprocess.run([objdump, "-dr", str(object_path)], cwd=ROOT, text=True, capture_output=True, env=env)
        dump_path.write_text(dump_completed.stdout, encoding="utf-8")
        objdump_stderr_path.write_text(dump_completed.stderr, encoding="utf-8")
        objdump_returncode = dump_completed.returncode
        dumped = dump_completed.returncode == 0 and dump_path.is_file()
    else:
        objdump_stderr_path.write_text("", encoding="utf-8")
    return {
        "probe_source": rel(source_path),
        "object": rel(object_path),
        "dump": rel(dump_path),
        "stderr_log": rel(stderr_path),
        "stdout_log": rel(stdout_path),
        "objdump_stderr_log": rel(objdump_stderr_path),
        "returncode": completed.returncode,
        "compiled": compiled,
        "dumped": dumped,
        "objdump_returncode": objdump_returncode,
        **analysis,
    }


def compare_dump(dump_path: Path, function_name: str, target_ops: list[str]) -> dict[str, Any]:
    if not dump_path.is_file():
        return {}
    symbols = read_objdump_file(dump_path)
    compiled = symbols.get(function_name)
    if not isinstance(compiled, dict):
        return {}
    compiled_ops = list(compiled.get("ops", []))
    count = min(len(target_ops), len(compiled_ops))
    compare = compare_ops(target_ops[:count], compiled_ops[:count]) if count else compare_ops([], compiled_ops)
    return {
        "compiled_instruction_count": len(compiled_ops),
        "compare_instruction_count": count,
        "category": classify_window(compare),
        "matching_prefix": int_value(compare.get("matching_prefix")),
        "matching_suffix": int_value(compare.get("matching_suffix")),
        "lcs_instruction_count": int_value(compare.get("lcs_instruction_count")),
        "lcs_window_ratio": float_value(compare.get("lcs_target_ratio")),
        "longest_common_run": run_text(compare),
        "first_difference": first_difference_text(compare),
        "exact_match": bool(compare.get("exact_match")),
    }


def build_rows(args: argparse.Namespace) -> dict[str, Any]:
    plan = read_json(args.plan, {})
    semantic = semantic_index(read_json(args.semantic, {}))
    prelude = probe_prelude()
    gcc = find_tool("gcc", args.tool_prefix)
    objdump = find_tool("objdump", args.tool_prefix)
    rows: list[dict[str, Any]] = []

    for plan_row in list_value(plan.get("rows", [])):
        if not isinstance(plan_row, dict):
            continue
        key = (str(plan_row.get("entry", "")).lower(), str(plan_row.get("candidate_symbol", "")))
        semantic_row = semantic.get(key, {})
        target_ops = target_slice_for(semantic_row)
        source_lines = read_source_lines(repo_path(plan_row.get("source_excerpt", "")))
        for suggested in list_value(plan_row.get("suggested_ranges", [])):
            if not isinstance(suggested, dict):
                continue
            start_line = int_value(suggested.get("source_start_line"))
            end_line = int_value(suggested.get("source_end_line"))
            function_name = safe_name(
                f"oot3d_subregion_{plan_row.get('entry', '')}_{plan_row.get('candidate_symbol', '')}_{start_line}_{end_line}"
            )
            source_text = source_for_probe(prelude, function_name, source_lines, start_line, end_line)
            compile_row = compile_probe(source_text, function_name, args.build_dir, gcc, objdump, args.optimization)
            compare_row = compare_dump(repo_path(compile_row.get("dump", "")), function_name, target_ops) if compile_row.get("dumped") else {}
            rows.append(
                {
                    "entry": plan_row.get("entry", ""),
                    "oot3d_name": plan_row.get("oot3d_name", ""),
                    "candidate_symbol": plan_row.get("candidate_symbol", ""),
                    "function_name": function_name,
                    "source_start_line": start_line,
                    "source_end_line": end_line,
                    "source_line_count": max(0, end_line - start_line + 1),
                    "target_window": plan_row.get("target_window", ""),
                    "anchor_source_line": plan_row.get("anchor_source_line", 0),
                    "anchor_source_text": plan_row.get("anchor_source_text", ""),
                    "compiled": bool(compile_row.get("compiled")),
                    "dumped": bool(compile_row.get("dumped")),
                    "primary_blocker": compile_row.get("primary_blocker", ""),
                    "error_count": int_value(compile_row.get("error_count")),
                    "warning_count": int_value(compile_row.get("warning_count")),
                    **compile_row,
                    **compare_row,
                }
            )

    rows.sort(
        key=lambda row: (
            not bool(row.get("compiled")),
            -float_value(row.get("lcs_window_ratio")),
            int_value(row.get("source_start_line")),
            int_value(row.get("source_end_line")),
        )
    )
    near_categories = {"exact-window", "codegen-near-window", "structural-near-window"}
    counts = Counter(str(row.get("category", "")) for row in rows if row.get("category"))
    summary = {
        "subregion_probes": len(rows),
        "compiled": sum(1 for row in rows if row.get("compiled")),
        "dumped": sum(1 for row in rows if row.get("dumped")),
        "failed": sum(1 for row in rows if not row.get("compiled")),
        "near_or_exact": sum(1 for row in rows if str(row.get("category", "")) in near_categories),
        "categories": dict(sorted(counts.items())),
        "primary_blockers": dict(Counter(str(row.get("primary_blocker", "")) for row in rows if row.get("primary_blocker"))),
        "best_entry": rows[0].get("entry", "") if rows else "",
        "best_candidate_symbol": rows[0].get("candidate_symbol", "") if rows else "",
        "best_source_range": (
            f"{rows[0].get('source_start_line', '')}-{rows[0].get('source_end_line', '')}" if rows else ""
        ),
        "best_category": rows[0].get("category", "") if rows else "",
        "best_lcs_window_ratio": rows[0].get("lcs_window_ratio", 0.0) if rows else 0.0,
        "gcc": gcc,
        "objdump": objdump,
        "optimization": args.optimization,
        "build_dir": rel(args.build_dir),
        "next_gate": "Use near/exact subregion probes as split seeds; otherwise widen or relocate the source subregion.",
    }
    return {
        "format": "oot3d_direct_source_subregion_probe_v1",
        "summary": summary,
        "rows": rows,
    }


def write_markdown(path: Path, data: dict[str, Any]) -> None:
    summary = data["summary"]
    lines = [
        "# Direct Source Subregion Probe",
        "",
        "This report compiles source-range probes and compares their object code to the corrected OOT3D target window.",
        "",
        "## Summary",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Subregion probes | {summary['subregion_probes']} |",
        f"| Compiled | {summary['compiled']} |",
        f"| Dumped | {summary['dumped']} |",
        f"| Near/exact | {summary['near_or_exact']} |",
        f"| Best LCS | {float_value(summary['best_lcs_window_ratio']):.4f} |",
        "",
        "## Probes",
        "",
        "| Status | Category | OOT3D | Candidate | Source range | LCS | Prefix/Suffix | Blocker | Probe | Dump |",
        "| --- | --- | --- | --- | ---: | ---: | ---: | --- | --- | --- |",
    ]
    for row in data["rows"]:
        status = "compiled" if row.get("compiled") else "failed"
        lines.append(
            f"| `{status}` | `{row.get('category', '')}` | `{row.get('entry', '')}` `{row.get('oot3d_name', '')}` | "
            f"`{row.get('candidate_symbol', '')}` | {row.get('source_start_line', '')}-{row.get('source_end_line', '')} | "
            f"{float_value(row.get('lcs_window_ratio')):.4f} | "
            f"{row.get('matching_prefix', '')}/{row.get('matching_suffix', '')} | "
            f"`{row.get('primary_blocker', '')}` | `{row.get('probe_source', '')}` | `{row.get('dump', '')}` |"
        )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plan", type=Path, default=DEFAULT_PLAN)
    parser.add_argument("--semantic", type=Path, default=DEFAULT_SEMANTIC)
    parser.add_argument("--build-dir", type=Path, default=DEFAULT_BUILD)
    parser.add_argument("--out-json", type=Path, default=DEFAULT_OUT_JSON)
    parser.add_argument("--out-csv", type=Path, default=DEFAULT_OUT_CSV)
    parser.add_argument("--out-md", type=Path, default=DEFAULT_OUT_MD)
    parser.add_argument("--optimization", default="-O2")
    parser.add_argument("--tool-prefix", default="arm-none-eabi")
    args = parser.parse_args()

    data = build_rows(args)
    fields = [
        "compiled",
        "dumped",
        "category",
        "entry",
        "oot3d_name",
        "candidate_symbol",
        "function_name",
        "source_start_line",
        "source_end_line",
        "source_line_count",
        "target_window",
        "anchor_source_line",
        "compiled_instruction_count",
        "compare_instruction_count",
        "lcs_instruction_count",
        "lcs_window_ratio",
        "matching_prefix",
        "matching_suffix",
        "longest_common_run",
        "first_difference",
        "primary_blocker",
        "error_count",
        "warning_count",
        "probe_source",
        "object",
        "dump",
        "stderr_log",
    ]
    write_json(args.out_json, data)
    write_csv(args.out_csv, data["rows"], fields)
    write_markdown(args.out_md, data)
    summary = data["summary"]
    print(
        "direct source subregion probe: "
        f"{summary['compiled']}/{summary['subregion_probes']} compiled, "
        f"{summary['near_or_exact']} near/exact, "
        f"best {summary['best_entry']} {summary['best_candidate_symbol']} "
        f"{summary['best_source_range']} {summary['best_lcs_window_ratio']}"
    )
    print(str(args.out_md.relative_to(ROOT)).replace("\\", "/"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
