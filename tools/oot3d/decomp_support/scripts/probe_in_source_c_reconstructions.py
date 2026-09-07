#!/usr/bin/env python3
"""Probe in-source C reconstruction alternatives for exact asm seeds."""

from __future__ import annotations

import argparse
import csv
import json
import re
import shutil
import subprocess
from collections import Counter
from pathlib import Path
from typing import Any

from audit_c_conversion_readiness import BASELINE_DEFINES, source_style


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SOURCE_ROOTS = [ROOT / "src"]
DEFAULT_BUILD_ROOT = ROOT / "build" / "in_source_c_reconstruction_probe"
DEFAULT_VARIANT_ROOT = ROOT / "analysis" / "in_source_c_reconstruction_sources"
DEFAULT_OUT_JSON = ROOT / "analysis" / "in_source_c_reconstruction_probe.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "in_source_c_reconstruction_probe.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "in_source_c_reconstruction_probe.md"

DEFINE_RE = re.compile(r"#\s*if\s+defined\s*\(\s*(OOT3D_[A-Za-z0-9_]+_C_RECONSTRUCTION)\s*\)")
FUNCTION_RE = re.compile(r"\b(?P<name>(?:oot3d|FUN)_[A-Za-z0-9_]+)\s*\(")


def rel(path: Path | str) -> str:
    path = Path(path)
    try:
        return str(path.relative_to(ROOT)).replace("\\", "/")
    except ValueError:
        return str(path).replace("\\", "/")


def slug(value: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", value).strip("_")


def read_json(path: Path, default: Any) -> Any:
    if not path.is_file():
        return default
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, data: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def discover_candidates(roots: list[Path]) -> list[dict[str, str]]:
    candidates: list[dict[str, str]] = []
    for root in roots:
        for path in sorted(root.rglob("*.c")):
            text = path.read_text(encoding="utf-8", errors="replace")
            for match in DEFINE_RE.finditer(text):
                function_match = FUNCTION_RE.search(text, match.end())
                if not function_match:
                    continue
                candidates.append(
                    {
                        "source": rel(path),
                        "define": match.group(1),
                        "function": function_match.group("name"),
                    }
                )
    return sorted(candidates, key=lambda row: (row["source"], row["define"], row["function"]))


def run_build(candidate: dict[str, str], out_dir: Path) -> dict[str, Any]:
    if out_dir.exists():
        shutil.rmtree(out_dir)
    command = [
        "powershell",
        "-NoProfile",
        "-ExecutionPolicy",
        "Bypass",
        "-File",
        str(ROOT / "scripts" / "build-matched-objects.ps1"),
        "-Compiler",
        "gcc",
        "-Source",
        candidate["source"],
        "-OutDir",
        rel(out_dir),
        "-ExtraCFlag",
        f"-D{candidate['define']}",
    ]
    completed = subprocess.run(command, cwd=ROOT, text=True, capture_output=True)
    return {
        "returncode": completed.returncode,
        "stdout_tail": tail(completed.stdout),
        "stderr_tail": tail(completed.stderr),
        "compare": read_json(out_dir / "compare_matched_objects.json", []) if completed.returncode == 0 else [],
    }


def tail(text: str, lines: int = 20) -> str:
    return "\n".join(text.splitlines()[-lines:])


def compare_for_name(rows: list[dict[str, Any]], name: str) -> dict[str, Any] | None:
    for row in rows:
        if row.get("name") == name:
            return row
    return None


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


def classify(row: dict[str, Any] | None, compiled: bool) -> str:
    if not compiled:
        return "compile-blocked"
    if not row:
        return "compare-blocked"
    if row.get("exact_match"):
        return "exact"
    target_count = int(row.get("target_instruction_count", 0) or 0)
    compiled_count = int(row.get("compiled_instruction_count", 0) or 0)
    lcs_ratio = float(row.get("lcs_target_ratio", 0.0) or 0.0)
    prefix = int(row.get("matching_prefix", 0) or 0)
    suffix = int(row.get("matching_suffix", 0) or 0)
    run = row.get("longest_common_run")
    run_length = int(run.get("length", 0) or 0) if isinstance(run, dict) else 0
    if abs(target_count - compiled_count) <= 3 and (lcs_ratio >= 0.50 or prefix >= 5 or suffix >= 5):
        return "codegen-near"
    if lcs_ratio >= 0.35 or run_length >= 5:
        return "structural-near"
    if lcs_ratio >= 0.15 or run_length >= 3:
        return "semantic-started"
    return "semantic-gap"


def write_variant_source(candidate: dict[str, str], out_dir: Path) -> Path:
    source_path = ROOT / candidate["source"]
    text = source_path.read_text(encoding="utf-8", errors="replace")
    out_dir.mkdir(parents=True, exist_ok=True)
    out = out_dir / f"{candidate['function']}_{candidate['define']}.c"
    out.write_text(text, encoding="utf-8")
    return out


def probe(args: argparse.Namespace) -> list[dict[str, Any]]:
    if args.build_root.exists():
        shutil.rmtree(args.build_root)
    if args.variant_root.exists():
        shutil.rmtree(args.variant_root)

    candidates = discover_candidates(args.source_root)
    rows: list[dict[str, Any]] = []
    for candidate in candidates:
        out_dir = args.build_root / slug(candidate["function"])
        result = run_build(candidate, out_dir)
        compare = compare_for_name(result["compare"], candidate["function"])
        compiled = result["returncode"] == 0
        variant_source = write_variant_source(candidate, args.variant_root)
        row = {
            "category": classify(compare, compiled),
            "function": candidate["function"],
            "source": candidate["source"],
            "define": candidate["define"],
            "variant_source": rel(variant_source),
            "variant_style": source_style(str(variant_source), candidate["function"], set(BASELINE_DEFINES) | {candidate["define"]}),
            "build_dir": rel(out_dir),
            "returncode": result["returncode"],
            "target_instruction_count": 0,
            "compiled_instruction_count": 0,
            "matching_prefix": 0,
            "matching_suffix": 0,
            "lcs_instruction_count": 0,
            "lcs_target_ratio": 0.0,
            "longest_common_run": "",
            "first_difference": "",
            "stderr_tail": result["stderr_tail"],
        }
        if compare:
            for key in (
                "target_instruction_count",
                "compiled_instruction_count",
                "matching_prefix",
                "matching_suffix",
                "lcs_instruction_count",
                "lcs_target_ratio",
            ):
                row[key] = compare.get(key, row[key])
            row["longest_common_run"] = run_text(compare)
            row["first_difference"] = first_difference_text(compare)
        elif not compiled:
            row["first_difference"] = (result["stderr_tail"].splitlines() or ["compile failed"])[-1][:180]
        rows.append(row)
    return sorted(rows, key=row_sort_key)


def row_sort_key(row: dict[str, Any]) -> tuple[int, str]:
    order = {
        "exact": 0,
        "codegen-near": 1,
        "structural-near": 2,
        "semantic-started": 3,
        "semantic-gap": 4,
        "compare-blocked": 5,
        "compile-blocked": 6,
    }
    return (order.get(str(row["category"]), 99), str(row["function"]))


def summarize(rows: list[dict[str, Any]]) -> dict[str, Any]:
    counts = Counter(row["category"] for row in rows)
    return {
        "candidates": len(rows),
        "exact": counts["exact"],
        "codegen_near": counts["codegen-near"],
        "structural_near": counts["structural-near"],
        "semantic_started": counts["semantic-started"],
        "semantic_gap": counts["semantic-gap"],
        "compare_blocked": counts["compare-blocked"],
        "compile_blocked": counts["compile-blocked"],
    }


def write_csv_report(path: Path, rows: list[dict[str, Any]]) -> None:
    fieldnames = [
        "category",
        "function",
        "source",
        "define",
        "variant_style",
        "target_instruction_count",
        "compiled_instruction_count",
        "matching_prefix",
        "matching_suffix",
        "lcs_instruction_count",
        "lcs_target_ratio",
        "longest_common_run",
        "first_difference",
        "variant_source",
        "build_dir",
        "returncode",
    ]
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow({key: row.get(key, "") for key in fieldnames})


def write_md(path: Path, rows: list[dict[str, Any]], summary: dict[str, Any]) -> None:
    lines = [
        "# In-Source C Reconstruction Probe",
        "",
        "This report compiles C reconstruction alternatives already present behind `*_C_RECONSTRUCTION` defines and compares them against the exact target functions.",
        "",
        "## Summary",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Candidates | {summary['candidates']} |",
        f"| Exact | {summary['exact']} |",
        f"| Codegen-near | {summary['codegen_near']} |",
        f"| Structural-near | {summary['structural_near']} |",
        f"| Semantic-started | {summary['semantic_started']} |",
        f"| Semantic-gap | {summary['semantic_gap']} |",
        f"| Compile-blocked | {summary['compile_blocked']} |",
        "",
        "## Candidates",
        "",
        "| Category | Function | Define | Style | Target | Compiled | Prefix | Suffix | LCS | Ratio | Run | First difference |",
        "| --- | --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | --- | --- |",
    ]
    for row in rows:
        lines.append(
            f"| `{row['category']}` | `{row['function']}` | `{row['define']}` | `{row['variant_style']}` | "
            f"{row['target_instruction_count']} | {row['compiled_instruction_count']} | "
            f"{row['matching_prefix']} | {row['matching_suffix']} | {row['lcs_instruction_count']} | "
            f"{float(row['lcs_target_ratio']):.4f} | `{row['longest_common_run']}` | `{row['first_difference']}` |"
        )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, nargs="*", default=DEFAULT_SOURCE_ROOTS)
    parser.add_argument("--build-root", type=Path, default=DEFAULT_BUILD_ROOT)
    parser.add_argument("--variant-root", type=Path, default=DEFAULT_VARIANT_ROOT)
    parser.add_argument("--out-json", type=Path, default=DEFAULT_OUT_JSON)
    parser.add_argument("--out-csv", type=Path, default=DEFAULT_OUT_CSV)
    parser.add_argument("--out-md", type=Path, default=DEFAULT_OUT_MD)
    args = parser.parse_args()

    rows = probe(args)
    summary = summarize(rows)
    write_json(args.out_json, {"summary": summary, "rows": rows})
    write_csv_report(args.out_csv, rows)
    write_md(args.out_md, rows, summary)

    print(
        "in-source C reconstruction probe: "
        f"{summary['exact']}/{summary['candidates']} exact, "
        f"{summary['codegen_near']} codegen-near, "
        f"{summary['compile_blocked']} compile-blocked"
    )
    print(rel(args.out_md))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
