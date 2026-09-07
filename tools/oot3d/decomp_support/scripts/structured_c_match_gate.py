#!/usr/bin/env python3
"""Build and classify N64-derived structured C match candidates in batch."""

from __future__ import annotations

import argparse
import csv
import json
import shlex
import subprocess
from collections import Counter
from pathlib import Path
from typing import Any

from audit_c_conversion_readiness import BASELINE_DEFINES, cflag_defines, source_style


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_UNITS = ROOT / "metadata" / "structured_port_units.csv"
DEFAULT_MAP = ROOT / "metadata" / "n64_port_map.csv"
DEFAULT_OUT_JSON = ROOT / "analysis" / "structured_c_match_gate.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "structured_c_match_gate.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "structured_c_match_gate.md"


def rel(path: Path) -> str:
    try:
        return str(path.relative_to(ROOT)).replace("\\", "/")
    except ValueError:
        return str(path).replace("\\", "/")


def read_csv(path: Path) -> list[dict[str, str]]:
    if not path.is_file():
        return []
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def read_json(path: Path, default: Any) -> Any:
    if not path.is_file():
        return default
    return json.loads(path.read_text(encoding="utf-8"))


def selected_units(units: list[dict[str, str]], names: list[str], port_files: list[str]) -> list[dict[str, str]]:
    selected_names = set(names)
    selected_ports = set(port_files)
    rows = units
    if selected_names:
        rows = [row for row in rows if row.get("unit") in selected_names]
    if selected_ports:
        rows = [row for row in rows if row.get("port_file") in selected_ports]
    return rows


def ps_quote(value: str | Path) -> str:
    text = str(value)
    return "'" + text.replace("'", "''") + "'"


def split_cflags(value: str) -> list[str]:
    if not value:
        return []
    return shlex.split(value)


def build_unit(unit: dict[str, str]) -> dict[str, Any]:
    command_text = (
        f"& {ps_quote(ROOT / 'scripts' / 'build-matched-objects.ps1')} "
        f"-OutDir {ps_quote(unit['out_dir'])} "
        f"-Source {ps_quote(unit['source'])}"
    )
    cflags = split_cflags(unit.get("extra_cflag", ""))
    if cflags:
        command_text += " -ExtraCFlag @(" + ", ".join(ps_quote(flag) for flag in cflags) + ")"

    command = [
        "powershell",
        "-NoProfile",
        "-ExecutionPolicy",
        "Bypass",
        "-Command",
        "$ErrorActionPreference = 'Stop'; " + command_text,
    ]

    completed = subprocess.run(command, cwd=ROOT, text=True, capture_output=True)
    return {
        "returncode": completed.returncode,
        "stdout_tail": tail(completed.stdout),
        "stderr_tail": tail(completed.stderr),
        "command": command,
    }


def tail(text: str, lines: int = 20) -> str:
    return "\n".join(text.splitlines()[-lines:])


def compare_rows(unit: dict[str, str]) -> dict[str, dict[str, Any]]:
    rows = read_json(ROOT / unit["out_dir"] / "compare_matched_objects.json", [])
    return {str(row.get("name", "")): row for row in rows}


def map_rows_by_port(rows: list[dict[str, str]]) -> dict[str, list[dict[str, str]]]:
    by_port: dict[str, list[dict[str, str]]] = {}
    for row in rows:
        by_port.setdefault(row.get("port_file", ""), []).append(row)
    return by_port


def classify_compare(row: dict[str, Any] | None) -> str:
    if not row:
        return "not-built"
    if row.get("exact_match"):
        return "exact-c"

    target_count = int(row.get("target_instruction_count", 0) or 0)
    compiled_count = int(row.get("compiled_instruction_count", 0) or 0)
    count_delta = abs(target_count - compiled_count)
    prefix = int(row.get("matching_prefix", 0) or 0)
    suffix = int(row.get("matching_suffix", 0) or 0)
    lcs_ratio = float(row.get("lcs_target_ratio", 0.0) or 0.0)
    longest_run = row.get("longest_common_run", {})
    run_length = int(longest_run.get("length", 0) or 0) if isinstance(longest_run, dict) else 0

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


def build_gate_rows(
    units: list[dict[str, str]],
    port_map: list[dict[str, str]],
    build_results: dict[str, dict[str, Any]],
) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    map_by_port = map_rows_by_port(port_map)

    for unit in units:
        compares = compare_rows(unit)
        build_result = build_results.get(unit["unit"], {"returncode": 0})
        defines = set(BASELINE_DEFINES) | cflag_defines(unit.get("extra_cflag", ""))
        for mapped in map_by_port.get(unit["port_file"], []):
            name = mapped["oot3d_name"]
            compare = compares.get(name)
            category = "build-failed" if build_result["returncode"] != 0 else classify_compare(compare)
            rows.append(
                {
                    "unit": unit["unit"],
                    "port_file": unit["port_file"],
                    "source": unit["source"],
                    "extra_cflag": unit.get("extra_cflag", ""),
                    "out_dir": unit["out_dir"],
                    "oot3d_entry": mapped["oot3d_entry"],
                    "oot3d_name": name,
                    "n64_source": mapped["n64_source"],
                    "n64_name": mapped["n64_name"],
                    "map_status": mapped["status"],
                    "category": category,
                    "implementation_style": source_style(unit["source"], name, defines),
                    "target_instruction_count": int(compare.get("target_instruction_count", 0) or 0) if compare else 0,
                    "compiled_instruction_count": int(compare.get("compiled_instruction_count", 0) or 0) if compare else 0,
                    "matching_prefix": int(compare.get("matching_prefix", 0) or 0) if compare else 0,
                    "matching_suffix": int(compare.get("matching_suffix", 0) or 0) if compare else 0,
                    "lcs_instruction_count": int(compare.get("lcs_instruction_count", 0) or 0) if compare else 0,
                    "lcs_target_ratio": float(compare.get("lcs_target_ratio", 0.0) or 0.0) if compare else 0.0,
                    "longest_common_run": run_text(compare),
                    "first_difference": first_difference_text(compare),
                }
            )
    return sorted(rows, key=gate_sort_key)


def gate_sort_key(row: dict[str, Any]) -> tuple[int, str, str]:
    order = {
        "exact-c": 0,
        "codegen-near": 1,
        "structural-near": 2,
        "semantic-started": 3,
        "semantic-gap": 4,
        "not-built": 5,
        "build-failed": 6,
    }
    return (order.get(row["category"], 99), row["unit"], row["oot3d_entry"])


def write_csv_report(path: Path, rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fieldnames = [
        "category",
        "implementation_style",
        "unit",
        "oot3d_entry",
        "oot3d_name",
        "n64_source",
        "n64_name",
        "target_instruction_count",
        "compiled_instruction_count",
        "matching_prefix",
        "matching_suffix",
        "lcs_instruction_count",
        "lcs_target_ratio",
        "longest_common_run",
        "first_difference",
        "port_file",
        "source",
        "extra_cflag",
        "out_dir",
        "map_status",
    ]
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow({key: row.get(key, "") for key in fieldnames})


def write_markdown(path: Path, rows: list[dict[str, Any]], build_results: dict[str, dict[str, Any]]) -> None:
    counts = Counter(row["category"] for row in rows)
    exact_rows = [row for row in rows if row["category"] == "exact-c"]
    exact_styles = Counter(row["implementation_style"] for row in exact_rows)
    units = sorted({row["unit"] for row in rows})
    exact_instructions = sum(row["target_instruction_count"] for row in rows if row["category"] == "exact-c")

    lines = [
        "# Structured C Match Gate",
        "",
        "Generated from N64-derived structured C units compiled with the OOT3D ARM toolchain.",
        "",
        "## Summary",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Units | {len(units)} |",
        f"| Functions checked | {len(rows)} |",
        f"| Exact compiled functions | {counts['exact-c']} |",
        f"| Exact real-C functions | {exact_styles['plain-c']} |",
        f"| Exact inline-asm functions | {exact_styles['c-inline-asm']} |",
        f"| Exact naked-asm seed functions | {exact_styles['naked-asm']} |",
        f"| Exact compiled target instructions | {exact_instructions} |",
        f"| Codegen-near functions | {counts['codegen-near']} |",
        f"| Structural-near functions | {counts['structural-near']} |",
        f"| Semantic-started functions | {counts['semantic-started']} |",
        f"| Semantic-gap functions | {counts['semantic-gap']} |",
        f"| Not built | {counts['not-built'] + counts['build-failed']} |",
        "",
        "## Unit Builds",
        "",
        "| Unit | Status | Out dir | Extra C flag |",
        "| --- | --- | --- | --- |",
    ]
    for unit in units:
        build = build_results.get(unit, {"returncode": 0})
        status = "ok" if build.get("returncode", 0) == 0 else f"failed:{build.get('returncode')}"
        sample = next(row for row in rows if row["unit"] == unit)
        lines.append(f"| `{unit}` | {status} | `{sample['out_dir']}` | `{sample['extra_cflag']}` |")

    lines.extend(
        [
            "",
            "## Promotion Queue",
            "",
            "Rows in `exact-c` are eligible for promotion only when their style is not `naked-asm`.",
            "Rows in `codegen-near` should be attacked with source reshaping, type/prototype fixes, or compiler-profile work.",
            "",
            "| Category | Style | Unit | OOT3D | N64 function | Target | Compiled | Prefix | Suffix | LCS | Ratio | Run | First difference |",
            "| --- | --- | --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | --- | --- |",
        ]
    )

    for row in rows:
        lines.append(
            f"| `{row['category']}` | `{row['implementation_style']}` | `{row['unit']}` | "
            f"`{row['oot3d_entry']}` `{row['oot3d_name']}` | "
            f"`{row['n64_name']}` | {row['target_instruction_count']} | {row['compiled_instruction_count']} | "
            f"{row['matching_prefix']} | {row['matching_suffix']} | {row['lcs_instruction_count']} | "
            f"{row['lcs_target_ratio']:.4f} | {row['longest_common_run']} | `{row['first_difference']}` |"
        )

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_json_report(
    path: Path,
    rows: list[dict[str, Any]],
    build_results: dict[str, dict[str, Any]],
    units: list[dict[str, str]],
) -> None:
    counts = Counter(row["category"] for row in rows)
    exact_styles = Counter(row["implementation_style"] for row in rows if row["category"] == "exact-c")
    data = {
        "counts": dict(sorted(counts.items())),
        "exact_c_functions": counts["exact-c"],
        "exact_real_c_functions": exact_styles["plain-c"],
        "exact_inline_asm_functions": exact_styles["c-inline-asm"],
        "exact_naked_asm_seed_functions": exact_styles["naked-asm"],
        "exact_c_target_instructions": sum(
            row["target_instruction_count"] for row in rows if row["category"] == "exact-c"
        ),
        "units": units,
        "build_results": build_results,
        "rows": rows,
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--units", type=Path, default=DEFAULT_UNITS)
    parser.add_argument("--map", type=Path, default=DEFAULT_MAP)
    parser.add_argument("--unit", action="append", default=[], help="Structured unit to gate; repeatable.")
    parser.add_argument("--port-file", action="append", default=[], help="Structured port file to gate; repeatable.")
    parser.add_argument("--skip-build", action="store_true", help="Read existing compare outputs without rebuilding.")
    parser.add_argument("--out-json", type=Path, default=DEFAULT_OUT_JSON)
    parser.add_argument("--out-csv", type=Path, default=DEFAULT_OUT_CSV)
    parser.add_argument("--out-md", type=Path, default=DEFAULT_OUT_MD)
    args = parser.parse_args()

    units = selected_units(read_csv(args.units), args.unit, args.port_file)
    if not units:
        raise SystemExit("no structured units selected")

    build_results: dict[str, dict[str, Any]] = {}
    if not args.skip_build:
        for unit in units:
            build_results[unit["unit"]] = build_unit(unit)
    else:
        previous = read_json(args.out_json, {})
        previous_builds = previous.get("build_results", {}) if isinstance(previous, dict) else {}
        build_results = {
            unit["unit"]: previous_builds.get(
                unit["unit"],
                {"returncode": 0, "stdout_tail": "", "stderr_tail": "", "skipped_build": True},
            )
            for unit in units
        }

    rows = build_gate_rows(units, read_csv(args.map), build_results)
    write_json_report(args.out_json, rows, build_results, units)
    write_csv_report(args.out_csv, rows)
    write_markdown(args.out_md, rows, build_results)

    counts = Counter(row["category"] for row in rows)
    print(
        "structured C gate: "
        f"{counts['exact-c']} exact, {counts['codegen-near']} codegen-near, "
        f"{counts['structural-near']} structural-near, {counts['semantic-started']} semantic-started, "
        f"{counts['semantic-gap']} semantic-gap"
    )
    print(rel(args.out_md))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
