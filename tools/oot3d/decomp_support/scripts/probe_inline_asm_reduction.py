#!/usr/bin/env python3
"""Probe exact inline-asm sources for lower-asm replacement variants."""

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
DEFAULT_CATALOG = ROOT / "analysis" / "symbol_rank_catalog.csv"
DEFAULT_BUILD_ROOT = ROOT / "build" / "inline_asm_reduction_probe"
DEFAULT_SOURCE_ROOT = ROOT / "analysis" / "inline_asm_reduction_sources"
DEFAULT_OUT_JSON = ROOT / "analysis" / "inline_asm_reduction_probe.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "inline_asm_reduction_probe.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "inline_asm_reduction_probe.md"

EMPTY_ASM_RE = re.compile(
    r"^[ \t]*__asm__\s+volatile\(\s*\"\"\s*(?::[^;]*)?\);\s*\r?\n",
    re.MULTILINE,
)
NOP_ASM_RE = re.compile(
    r"^[ \t]*__asm__\s+volatile\(\s*\"nop\"\s*:\s*\"\+r\"\([^)]+\)\s*\);\s*\r?\n",
    re.MULTILINE,
)
GOTO_ZERO_RE = re.compile(
    r"(?P<indent>^[ \t]*)__asm__\s+goto\(\s*"
    r"\"cmp %0, #0\\n\\tbeq %l\[(?P<label>[A-Za-z_][A-Za-z0-9_]*)\]\"\s*"
    r":\s*:\s*\"r\"\((?P<var>[A-Za-z_][A-Za-z0-9_]*)\)\s*"
    r":\s*\"cc\"\s*:\s*(?P=label)\s*\);\s*",
    re.MULTILINE,
)


def rel(path: Path | str) -> str:
    path = Path(path)
    try:
        return str(path.relative_to(ROOT)).replace("\\", "/")
    except ValueError:
        return str(path).replace("\\", "/")


def slug(value: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", value).strip("_")


def read_csv(path: Path) -> list[dict[str, str]]:
    if not path.is_file():
        return []
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def read_json(path: Path, default: Any) -> Any:
    if not path.is_file():
        return default
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, data: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def inline_exact_candidates(path: Path, limit: int) -> list[dict[str, str]]:
    rows = [
        row
        for row in read_csv(path)
        if row.get("rank") == "O"
        and row.get("source_style") == "c-inline-asm"
        and row.get("compiled_source")
        and row.get("name")
    ]
    rows.sort(key=lambda row: row.get("entry", ""))
    return rows[:limit] if limit else rows


def transform_drop_empty(text: str) -> str:
    return EMPTY_ASM_RE.sub("", text)


def transform_drop_empty_nop(text: str) -> str:
    return NOP_ASM_RE.sub("", transform_drop_empty(text))


def transform_c_goto_zero(text: str) -> str:
    def replace(match: re.Match[str]) -> str:
        indent = match.group("indent")
        var = match.group("var")
        label = match.group("label")
        return f"{indent}if ({var} == 0) {{\n{indent}    goto {label};\n{indent}}}\n"

    return GOTO_ZERO_RE.sub(replace, transform_drop_empty_nop(text))


VARIANTS = {
    "drop_empty_asm": transform_drop_empty,
    "drop_empty_and_nop": transform_drop_empty_nop,
    "c_goto_zero": transform_c_goto_zero,
}


def write_variant_source(candidate: dict[str, str], variant_name: str, text: str, source_root: Path) -> Path:
    source_root.mkdir(parents=True, exist_ok=True)
    out = source_root / f"{candidate['entry']}_{slug(candidate['name'])}_{variant_name}.c"
    out.write_text(text, encoding="utf-8")
    return out


def run_build(source: Path, out_dir: Path) -> dict[str, Any]:
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
        rel(source),
        "-OutDir",
        rel(out_dir),
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


def probe(args: argparse.Namespace) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    if args.source_root.exists():
        shutil.rmtree(args.source_root)
    if args.build_root.exists():
        shutil.rmtree(args.build_root)
    candidates = inline_exact_candidates(args.catalog_csv, args.limit)
    for candidate in candidates:
        original_path = ROOT / candidate["compiled_source"]
        original_text = original_path.read_text(encoding="utf-8", errors="replace")
        for variant_name, transform in VARIANTS.items():
            variant_text = transform(original_text)
            if variant_text == original_text:
                continue
            variant_source = write_variant_source(candidate, variant_name, variant_text, args.source_root)
            out_dir = args.build_root / slug(candidate["name"]) / variant_name
            result = run_build(variant_source, out_dir)
            compare = compare_for_name(result["compare"], candidate["name"])
            style = source_style(str(variant_source), candidate["name"], set(BASELINE_DEFINES))
            compiled = result["returncode"] == 0
            row = {
                "category": classify(compare, compiled),
                "entry": candidate["entry"],
                "function": candidate["name"],
                "source": candidate["compiled_source"],
                "variant": variant_name,
                "variant_source": rel(variant_source),
                "variant_style": style,
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


def row_sort_key(row: dict[str, Any]) -> tuple[int, str, str]:
    order = {
        "exact": 0,
        "codegen-near": 1,
        "structural-near": 2,
        "semantic-started": 3,
        "semantic-gap": 4,
        "compare-blocked": 5,
        "compile-blocked": 6,
    }
    return (order.get(str(row["category"]), 99), row["function"], row["variant"])


def write_csv(path: Path, rows: list[dict[str, Any]]) -> None:
    fieldnames = [
        "category",
        "entry",
        "function",
        "source",
        "variant",
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


def write_markdown(path: Path, rows: list[dict[str, Any]], summary: dict[str, Any]) -> None:
    lines = [
        "# Inline ASM Reduction Probe",
        "",
        "Generated by trying mechanical lower-asm variants for exact `c-inline-asm` functions.",
        "",
        "## Summary",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Candidates | {summary['candidates']} |",
        f"| Variants built | {summary['variants']} |",
        f"| Exact variants | {summary['exact']} |",
        f"| Exact plain-style variants | {summary['exact_plain_style']} |",
        f"| Codegen-near variants | {summary['codegen_near']} |",
        f"| Compile-blocked variants | {summary['compile_blocked']} |",
        "",
        "## Variants",
        "",
        "| Category | Style | Entry | Function | Variant | Target | Compiled | Prefix | Suffix | LCS | Ratio | Run | First difference |",
        "| --- | --- | --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | --- | --- |",
    ]
    for row in rows:
        lines.append(
            f"| `{row['category']}` | `{row['variant_style']}` | `{row['entry']}` | `{row['function']}` | "
            f"`{row['variant']}` | {row['target_instruction_count']} | {row['compiled_instruction_count']} | "
            f"{row['matching_prefix']} | {row['matching_suffix']} | {row['lcs_instruction_count']} | "
            f"{float(row['lcs_target_ratio']):.4f} | `{row['longest_common_run']}` | `{row['first_difference']}` |"
        )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def summarize(rows: list[dict[str, Any]]) -> dict[str, Any]:
    counts = Counter(row["category"] for row in rows)
    candidates = {(row["entry"], row["function"]) for row in rows}
    return {
        "candidates": len(candidates),
        "variants": len(rows),
        "exact": counts["exact"],
        "exact_plain_style": sum(
            1 for row in rows if row["category"] == "exact" and row["variant_style"] == "plain-c"
        ),
        "codegen_near": counts["codegen-near"],
        "structural_near": counts["structural-near"],
        "semantic_started": counts["semantic-started"],
        "semantic_gap": counts["semantic-gap"],
        "compare_blocked": counts["compare-blocked"],
        "compile_blocked": counts["compile-blocked"],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--catalog-csv", type=Path, default=DEFAULT_CATALOG)
    parser.add_argument("--build-root", type=Path, default=DEFAULT_BUILD_ROOT)
    parser.add_argument("--source-root", type=Path, default=DEFAULT_SOURCE_ROOT)
    parser.add_argument("--out-json", type=Path, default=DEFAULT_OUT_JSON)
    parser.add_argument("--out-csv", type=Path, default=DEFAULT_OUT_CSV)
    parser.add_argument("--out-md", type=Path, default=DEFAULT_OUT_MD)
    parser.add_argument("--limit", type=int, default=0)
    args = parser.parse_args()

    rows = probe(args)
    summary = summarize(rows)
    write_json(args.out_json, {"summary": summary, "rows": rows})
    write_csv(args.out_csv, rows)
    write_markdown(args.out_md, rows, summary)

    print(
        "inline asm reduction probe: "
        f"{summary['exact_plain_style']}/{summary['variants']} exact plain-style variants, "
        f"{summary['codegen_near']} codegen-near, {summary['compile_blocked']} compile-blocked"
    )
    print(rel(args.out_md))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
