#!/usr/bin/env python3
"""Build control-flow isolation workorders from qualified direct subregion seeds."""

from __future__ import annotations

import argparse
import csv
import json
import re
from pathlib import Path
from typing import Any

from compare_runtime_objects import normalize_op
from probe_direct_source_subregions import rel, safe_name
from qualify_direct_source_subregion_seeds import NEAR_CATEGORIES
from refine_direct_data_like_boundaries import read_indexed_ops
from sweep_direct_source_subregion_ranges import float_value, int_value, list_value, repo_path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_QUALIFICATION = ROOT / "analysis" / "direct_source_subregion_seed_qualification.json"
DEFAULT_SEMANTIC = ROOT / "analysis" / "direct_semantic_call_shape.json"
DEFAULT_OUT_DIR = ROOT / "analysis" / "direct_control_flow_isolation_workorders"
DEFAULT_OUT_JSON = ROOT / "analysis" / "direct_control_flow_isolation_workorders.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "direct_control_flow_isolation_workorders.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "direct_control_flow_isolation_workorders.md"
SOURCE_LINE_RE = re.compile(r"^\s*(?P<line>\d+):\s(?P<text>.*)$")
OBJDUMP_HEADER_RE = re.compile(r"^[0-9a-fA-F]+\s+<(?P<name>[^>]+)>:\s*$")
OBJDUMP_INSN_RE = re.compile(r"^\s*(?P<offset>[0-9a-fA-F]+):\s+(?:[0-9a-fA-F]{2,8}\s+)+\t(?P<op>.+?)\s*$")


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


def semantic_index(data: dict[str, Any]) -> dict[tuple[str, str], dict[str, Any]]:
    result: dict[tuple[str, str], dict[str, Any]] = {}
    for row in list_value(data.get("rows", [])):
        if isinstance(row, dict):
            result[(str(row.get("entry", "")).lower(), str(row.get("candidate_symbol", "")))] = row
    return result


def source_lines(path: Path, start_line: int, end_line: int) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    if not path.is_file():
        return rows
    for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = SOURCE_LINE_RE.match(raw)
        if not match:
            continue
        line = int_value(match.group("line"))
        if start_line <= line <= end_line:
            text = match.group("text").rstrip()
            rows.append(
                {
                    "line": line,
                    "text": text,
                    "control_role": classify_source_line(text),
                }
            )
    return rows


def classify_source_line(text: str) -> str:
    stripped = text.strip()
    if not stripped:
        return "blank"
    if stripped.startswith("if "):
        return "condition"
    if stripped.startswith("} else"):
        return "branch"
    if re.search(r"\w+\(.*\);$", stripped):
        return "call"
    if any(token in stripped for token in ("++", "--", "|=", "&=", "=")):
        return "state-write"
    return "statement"


def read_dump_rows(path: Path, function_name: str) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    if not path.is_file():
        return rows
    current_name: str | None = None
    for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
        header = OBJDUMP_HEADER_RE.match(raw)
        if header:
            current_name = header.group("name")
            continue
        if current_name != function_name:
            continue
        match = OBJDUMP_INSN_RE.match(raw)
        if match:
            op = match.group("op").strip()
            if not op.startswith(".word"):
                rows.append(
                    {
                        "ordinal": len(rows),
                        "offset": match.group("offset").lower(),
                        "op": normalize_op(op),
                    }
                )
    return rows


def function_name_from_seed(seed: dict[str, Any]) -> str:
    name = str(seed.get("function_name", ""))
    if name:
        return name
    probe_source = str(seed.get("probe_source", ""))
    if probe_source:
        return Path(probe_source).name.replace(".probe.c", "")
    return ""


def lcs_pairs(left: list[str], right: list[str]) -> list[dict[str, Any]]:
    table = [[0] * (len(right) + 1) for _ in range(len(left) + 1)]
    for i in range(len(left) - 1, -1, -1):
        for j in range(len(right) - 1, -1, -1):
            if left[i] == right[j]:
                table[i][j] = 1 + table[i + 1][j + 1]
            else:
                table[i][j] = max(table[i + 1][j], table[i][j + 1])
    pairs: list[dict[str, Any]] = []
    i = 0
    j = 0
    while i < len(left) and j < len(right):
        if left[i] == right[j]:
            pairs.append({"target_slice_index": i, "compiled_slice_index": j, "op": left[i]})
            i += 1
            j += 1
        elif table[i + 1][j] >= table[i][j + 1]:
            i += 1
        else:
            j += 1
    return pairs


def mismatch_rows(target_slice: list[dict[str, Any]], compiled_slice: list[dict[str, Any]]) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    count = max(len(target_slice), len(compiled_slice))
    for index in range(count):
        target = target_slice[index] if index < len(target_slice) else {}
        compiled = compiled_slice[index] if index < len(compiled_slice) else {}
        target_op = str(target.get("op", ""))
        compiled_op = str(compiled.get("op", ""))
        rows.append(
            {
                "slice_index": index,
                "target_index": target.get("index", ""),
                "target_addr": target.get("addr", ""),
                "target_op": target_op,
                "compiled_ordinal": compiled.get("ordinal", ""),
                "compiled_offset": compiled.get("offset", ""),
                "compiled_op": compiled_op,
                "exact_op_match": target_op == compiled_op,
            }
        )
    return rows


def build_workorder(
    seed: dict[str, Any],
    semantic_rows: dict[tuple[str, str], dict[str, Any]],
    out_dir: Path,
) -> dict[str, Any]:
    key = (str(seed.get("entry", "")).lower(), str(seed.get("candidate_symbol", "")))
    semantic = semantic_rows.get(key, {})
    source_excerpt = repo_path(semantic.get("source_excerpt", ""))
    manifest = read_json(source_excerpt.with_name("probe_manifest.json"), {})
    target_rows = read_indexed_ops(repo_path(manifest.get("target_window_ops", "")))
    target_window_start_offset = find_target_offset(target_rows, str(seed.get("target_start_addr", "")))
    compare_count = int_value(seed.get("compare_instruction_count"))
    target_slice = target_rows[target_window_start_offset : target_window_start_offset + compare_count]
    compiled_rows = read_dump_rows(repo_path(seed.get("dump", "")), function_name_from_seed(seed))
    compiled_skip = int_value(seed.get("compiled_skip"))
    compiled_slice = compiled_rows[compiled_skip : compiled_skip + compare_count]
    source_start = int_value(seed.get("source_start_line"))
    source_end = int_value(seed.get("source_end_line"))
    workorder_name = safe_name(
        f"{seed.get('entry', '')}_{seed.get('candidate_symbol', '')}_{source_start}_{source_end}_{seed.get('target_start_addr', '')}"
    )
    workorder_path = out_dir / f"{workorder_name}.json"
    source = source_lines(source_excerpt, source_start, source_end)
    target_ops = [str(row.get("op", "")) for row in target_slice]
    compiled_ops = [str(row.get("op", "")) for row in compiled_slice]
    pairs = lcs_pairs(target_ops, compiled_ops)
    workorder = {
        "format": "oot3d_direct_control_flow_isolation_workorder_v1",
        "workorder": workorder_name,
        "entry": seed.get("entry", ""),
        "oot3d_name": seed.get("oot3d_name", ""),
        "candidate_symbol": seed.get("candidate_symbol", ""),
        "seed_class": seed.get("seed_class", ""),
        "seed_reason": seed.get("seed_reason", ""),
        "source_range": seed.get("source_range", ""),
        "target_range": f"{seed.get('target_start_addr', '')}-{seed.get('target_end_addr', '')}",
        "compiled_skip": seed.get("compiled_skip", 0),
        "compare_instruction_count": compare_count,
        "lcs_instruction_count": seed.get("lcs_instruction_count", 0),
        "lcs_window_ratio": seed.get("lcs_window_ratio", 0.0),
        "longest_common_run": seed.get("longest_common_run", ""),
        "generic_op_ratio": seed.get("generic_op_ratio", 0.0),
        "shared_specific_opcodes": seed.get("shared_specific_opcodes", ""),
        "promotion_state": "control-flow-isolation-only",
        "next_gate": (
            "Isolate the matching N64 branch/control-flow block against the indexed target slice; "
            "only promote after a rerun produces exact/codegen-near evidence over a non-generic slice."
        ),
        "inputs": {
            "qualification": rel(DEFAULT_QUALIFICATION),
            "semantic_manifest": rel(source_excerpt.with_name("probe_manifest.json")),
            "source_excerpt": rel(source_excerpt),
            "target_window_ops": rel(repo_path(manifest.get("target_window_ops", ""))),
            "compiled_dump": seed.get("dump", ""),
            "probe_source": seed.get("probe_source", ""),
        },
        "source_lines": source,
        "source_control_points": [row for row in source if row.get("control_role") in {"condition", "branch", "call", "state-write"}],
        "target_slice": target_slice,
        "compiled_slice": compiled_slice,
        "lcs_pairs": pairs,
        "mismatch_rows": mismatch_rows(target_slice, compiled_slice),
        "risk_notes": [
            "Target uses normalized branch/call labels, so call identity still needs symbol recovery.",
            "This seed starts inside the larger function body and must not be treated as a complete function boundary.",
            "The earlier 755-759 / 00474f00 hit is short-generic risk and is not the preferred isolation anchor.",
        ],
    }
    write_json(workorder_path, workorder)
    return {
        "workorder": workorder_name,
        "workorder_path": rel(workorder_path),
        "entry": workorder["entry"],
        "oot3d_name": workorder["oot3d_name"],
        "candidate_symbol": workorder["candidate_symbol"],
        "seed_class": workorder["seed_class"],
        "source_range": workorder["source_range"],
        "target_range": workorder["target_range"],
        "compiled_skip": workorder["compiled_skip"],
        "compare_instruction_count": workorder["compare_instruction_count"],
        "lcs_instruction_count": workorder["lcs_instruction_count"],
        "lcs_window_ratio": workorder["lcs_window_ratio"],
        "longest_common_run": workorder["longest_common_run"],
        "generic_op_ratio": workorder["generic_op_ratio"],
        "source_control_point_count": len(workorder["source_control_points"]),
        "lcs_pair_count": len(pairs),
        "exact_positional_match_count": sum(1 for row in workorder["mismatch_rows"] if row["exact_op_match"]),
        "promotion_state": workorder["promotion_state"],
    }


def find_target_offset(target_rows: list[dict[str, Any]], addr: str) -> int:
    wanted = addr.lower()
    for index, row in enumerate(target_rows):
        if str(row.get("addr", "")).lower() == wanted:
            return index
    return 0


def selected_seeds(data: dict[str, Any], limit: int) -> list[dict[str, Any]]:
    rows = [
        row
        for row in list_value(data.get("rows", []))
        if isinstance(row, dict)
        and str(row.get("seed_class", "")) == "candidate-control-flow-seed"
        and str(row.get("category", "")) in NEAR_CATEGORIES
    ]
    return rows[:limit]


def build_report(args: argparse.Namespace) -> dict[str, Any]:
    qualification = read_json(args.qualification, {})
    semantic = read_json(args.semantic, {})
    semantic_rows = semantic_index(semantic)
    args.out_dir.mkdir(parents=True, exist_ok=True)
    rows = [build_workorder(seed, semantic_rows, args.out_dir) for seed in selected_seeds(qualification, args.limit)]
    best = rows[0] if rows else {}
    summary = {
        "workorders": len(rows),
        "candidate_control_flow_seed_workorders": len(rows),
        "best_workorder": best.get("workorder", ""),
        "best_workorder_path": best.get("workorder_path", ""),
        "best_entry": best.get("entry", ""),
        "best_candidate_symbol": best.get("candidate_symbol", ""),
        "best_source_range": best.get("source_range", ""),
        "best_target_range": best.get("target_range", ""),
        "best_seed_class": best.get("seed_class", ""),
        "best_lcs_window_ratio": best.get("lcs_window_ratio", 0.0),
        "next_gate": "Use the best workorder to isolate and re-probe the matching control-flow block.",
    }
    return {
        "format": "oot3d_direct_control_flow_isolation_workorders_v1",
        "inputs": {
            "qualification": rel(args.qualification),
            "semantic": rel(args.semantic),
        },
        "summary": summary,
        "rows": rows,
    }


def write_markdown(path: Path, data: dict[str, Any]) -> None:
    summary = data["summary"]
    lines = [
        "# Direct Control-Flow Isolation Workorders",
        "",
        "These workorders turn qualified subregion seeds into indexed target/source/compiled slices for the next decompilation pass.",
        "",
        "## Summary",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Workorders | {summary['workorders']} |",
        f"| Candidate control-flow seed workorders | {summary['candidate_control_flow_seed_workorders']} |",
        f"| Best LCS | {float_value(summary['best_lcs_window_ratio']):.4f} |",
        "",
        "## Workorders",
        "",
        "| Workorder | OOT3D | Candidate | Source | Target | Skip | LCS | Run | State |",
        "| --- | --- | --- | ---: | --- | ---: | ---: | --- | --- |",
    ]
    for row in data["rows"]:
        lines.append(
            f"| `{row.get('workorder', '')}` | `{row.get('entry', '')}` `{row.get('oot3d_name', '')}` | "
            f"`{row.get('candidate_symbol', '')}` | {row.get('source_range', '')} | "
            f"`{row.get('target_range', '')}` | {row.get('compiled_skip', '')} | "
            f"{float_value(row.get('lcs_window_ratio')):.4f} | {row.get('longest_common_run', '')} | "
            f"`{row.get('promotion_state', '')}` |"
        )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qualification", type=Path, default=DEFAULT_QUALIFICATION)
    parser.add_argument("--semantic", type=Path, default=DEFAULT_SEMANTIC)
    parser.add_argument("--out-dir", type=Path, default=DEFAULT_OUT_DIR)
    parser.add_argument("--out-json", type=Path, default=DEFAULT_OUT_JSON)
    parser.add_argument("--out-csv", type=Path, default=DEFAULT_OUT_CSV)
    parser.add_argument("--out-md", type=Path, default=DEFAULT_OUT_MD)
    parser.add_argument("--limit", type=int, default=4)
    args = parser.parse_args()

    data = build_report(args)
    fields = [
        "workorder",
        "workorder_path",
        "entry",
        "oot3d_name",
        "candidate_symbol",
        "seed_class",
        "source_range",
        "target_range",
        "compiled_skip",
        "compare_instruction_count",
        "lcs_instruction_count",
        "lcs_window_ratio",
        "longest_common_run",
        "generic_op_ratio",
        "source_control_point_count",
        "lcs_pair_count",
        "exact_positional_match_count",
        "promotion_state",
    ]
    write_json(args.out_json, data)
    write_csv(args.out_csv, data["rows"], fields)
    write_markdown(args.out_md, data)
    summary = data["summary"]
    print(
        "direct control-flow isolation workorders: "
        f"{summary['workorders']} workorders, best {summary['best_entry']} "
        f"{summary['best_candidate_symbol']} {summary['best_source_range']} "
        f"{summary['best_target_range']}"
    )
    print(str(args.out_md.relative_to(ROOT)).replace("\\", "/"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
