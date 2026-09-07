#!/usr/bin/env python3
"""Search compiler profiles for structured N64-derived port units."""

from __future__ import annotations

import argparse
import csv
import json
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
UNITS = ROOT / "metadata" / "structured_port_units.csv"
DEFAULT_OUT_ROOT = ROOT / "analysis" / "compile_profile_search"
DEFAULT_ARMCC_OUT_ROOT = ROOT / "analysis" / "compile_profile_search_armcc"
DEFAULT_BUILD_ROOT = "build/compile_profile_search"
DEFAULT_ARMCC_BUILD_ROOT = "build/compile_profile_search_armcc"
OUT_ROOT = DEFAULT_OUT_ROOT
BUILD_ROOT = DEFAULT_BUILD_ROOT


@dataclass(frozen=True)
class Profile:
    name: str
    optimization: str
    cflags: tuple[str, ...] = ()


PROFILE_SETS: dict[str, list[Profile]] = {
    "quick": [
        Profile("o2", "-O2"),
        Profile("o1", "-O1"),
        Profile("os", "-Os"),
        Profile("o3", "-O3"),
        Profile("o2_no_schedule", "-O2", ("-fno-schedule-insns", "-fno-schedule-insns2")),
        Profile("o2_no_tree_sra", "-O2", ("-fno-tree-sra", "-fno-ipa-sra")),
    ],
    "extended": [
        Profile("o2", "-O2"),
        Profile("o1", "-O1"),
        Profile("os", "-Os"),
        Profile("o3", "-O3"),
        Profile("o2_no_schedule", "-O2", ("-fno-schedule-insns", "-fno-schedule-insns2")),
        Profile("o2_no_tree_sra", "-O2", ("-fno-tree-sra", "-fno-ipa-sra")),
        Profile(
            "o2_no_inline",
            "-O2",
            ("-fno-inline-functions", "-fno-inline-small-functions", "-fno-inline-functions-called-once"),
        ),
        Profile("o2_no_reorder", "-O2", ("-fno-reorder-blocks", "-fno-reorder-functions")),
        Profile("o2_no_gcse", "-O2", ("-fno-gcse", "-fno-cse-follow-jumps")),
    ],
    "armcc": [
        Profile("otime", "-Otime"),
        Profile("o3_otime", "-O3 -Otime"),
        Profile("ospace", "-Ospace"),
        Profile("o2_otime", "-O2 -Otime"),
    ],
}


def ps_quote(value: str | Path) -> str:
    text = str(value)
    return "'" + text.replace("'", "''") + "'"


def read_units(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def profile_command(
    unit: dict[str, str],
    profile: Profile,
    out_dir: str,
    source_list: str,
    args: argparse.Namespace,
) -> list[str]:
    cflags = [flag for flag in [unit.get("extra_cflag", "")] if flag] + list(profile.cflags)
    command = (
        f"& {ps_quote(ROOT / 'scripts' / 'build-matched-objects.ps1')} "
        f"-SourceList {ps_quote(source_list)} "
        f"-OutDir {ps_quote(out_dir)} "
        f"-Compiler {ps_quote(args.compiler)} "
    )
    if args.compiler == "armcc":
        command += f"-ArmccOptimization {ps_quote(profile.optimization)} "
    else:
        command += f"-Optimization {ps_quote(profile.optimization)} "
    if args.armcc_path:
        command += f"-ArmccPath {ps_quote(args.armcc_path)} "
    if cflags:
        flags = ", ".join(ps_quote(flag) for flag in cflags)
        command += f" -ExtraCFlag @({flags})"

    return [
        "powershell",
        "-NoProfile",
        "-ExecutionPolicy",
        "Bypass",
        "-Command",
        "$ErrorActionPreference = 'Stop'; " + command,
    ]


def read_compare(path: Path) -> list[dict[str, Any]]:
    if not path.is_file():
        return []
    return json.loads(path.read_text(encoding="utf-8"))


def run_profile(unit: dict[str, str], profile: Profile, args: argparse.Namespace) -> dict[str, Any]:
    unit_name = unit["unit"]
    out_dir = f"{BUILD_ROOT}/{unit_name}/{profile.name}"
    out_path = ROOT / out_dir
    out_path.mkdir(parents=True, exist_ok=True)
    source_list = f"{out_dir}/sources.txt"
    (ROOT / source_list).write_text(unit["source"] + "\n", encoding="utf-8")
    compare_path = ROOT / out_dir / "compare_matched_objects.json"
    completed = subprocess.run(
        profile_command(unit, profile, out_dir, source_list, args),
        cwd=ROOT,
        text=True,
        capture_output=True,
    )

    rows = read_compare(compare_path) if completed.returncode == 0 else []
    exact = sum(1 for row in rows if row.get("exact_match"))
    best = best_row(rows)
    return {
        "profile": profile.name,
        "compiler": args.compiler,
        "optimization": profile.optimization,
        "cflags": list(profile.cflags),
        "out_dir": out_dir,
        "returncode": completed.returncode,
        "stdout_tail": tail(completed.stdout),
        "stderr_tail": tail(completed.stderr),
        "compared_functions": len(rows),
        "exact_functions": exact,
        "best_row": best,
        "rows": rows,
    }


def best_row(rows: list[dict[str, Any]]) -> dict[str, Any] | None:
    if not rows:
        return None
    return max(rows, key=row_key)


def row_key(row: dict[str, Any]) -> tuple[int, float, int, int, int, int]:
    run = row.get("longest_common_run")
    run_length = int(run.get("length", 0)) if isinstance(run, dict) else 0
    return (
        1 if row.get("exact_match") else 0,
        float(row.get("lcs_target_ratio", 0.0) or 0.0),
        int(row.get("lcs_instruction_count", 0) or 0),
        int(row.get("matching_prefix", 0) or 0),
        int(row.get("matching_suffix", 0) or 0),
        run_length,
    )


def first_difference_text(row: dict[str, Any]) -> str:
    diff = row.get("first_difference")
    if not isinstance(diff, dict):
        return ""
    return f"{diff.get('index')}: {diff.get('target')} vs {diff.get('compiled')}"


def run_text(row: dict[str, Any]) -> str:
    run = row.get("longest_common_run")
    if not isinstance(run, dict):
        return ""
    return f"{run.get('length', '')}@{run.get('target_index', '')}/{run.get('compiled_index', '')}"


def classify_row(row: dict[str, Any]) -> str:
    if not row:
        return "not-built"
    if row.get("exact_match"):
        return "exact"

    target_count = int(row.get("target_instruction_count", 0) or 0)
    compiled_count = int(row.get("compiled_instruction_count", 0) or 0)
    count_delta = abs(target_count - compiled_count)
    prefix = int(row.get("matching_prefix", 0) or 0)
    suffix = int(row.get("matching_suffix", 0) or 0)
    lcs_ratio = float(row.get("lcs_target_ratio", 0.0) or 0.0)
    run = row.get("longest_common_run")
    run_length = int(run.get("length", 0) or 0) if isinstance(run, dict) else 0

    if count_delta <= 3 and (lcs_ratio >= 0.50 or prefix >= 5 or suffix >= 5 or run_length >= 8):
        return "codegen-near"
    if lcs_ratio >= 0.35 or run_length >= 5:
        return "structural-near"
    if lcs_ratio >= 0.15 or run_length >= 3:
        return "semantic-started"
    return "semantic-gap"


def function_bests(report: dict[str, Any]) -> list[dict[str, Any]]:
    bests: dict[str, dict[str, Any]] = {}
    for result in report.get("profiles", []):
        if result.get("returncode") != 0:
            continue
        profile = str(result.get("profile", ""))
        for row in result.get("rows", []):
            name = str(row.get("name", ""))
            if not name:
                continue
            current = bests.get(name)
            if current is None or row_key(row) > row_key(current["row"]):
                bests[name] = {
                    "unit": report["unit"],
                    "compiler": report.get("compiler", "gcc"),
                    "source": report["source"],
                    "profile": profile,
                    "optimization": result.get("optimization", ""),
                    "cflags": result.get("cflags", []),
                    "row": row,
                }

    rows = []
    for name, best in bests.items():
        row = best["row"]
        rows.append(
            {
                "unit": best["unit"],
                "compiler": best.get("compiler", "gcc"),
                "source": best["source"],
                "profile": best["profile"],
                "optimization": best["optimization"],
                "cflags": " ".join(best.get("cflags", [])),
                "function": name,
                "category": classify_row(row),
                "exact_match": bool(row.get("exact_match")),
                "target_instruction_count": int(row.get("target_instruction_count", 0) or 0),
                "compiled_instruction_count": int(row.get("compiled_instruction_count", 0) or 0),
                "matching_prefix": int(row.get("matching_prefix", 0) or 0),
                "matching_suffix": int(row.get("matching_suffix", 0) or 0),
                "lcs_instruction_count": int(row.get("lcs_instruction_count", 0) or 0),
                "lcs_target_ratio": float(row.get("lcs_target_ratio", 0.0) or 0.0),
                "longest_common_run": run_text(row),
                "first_difference": first_difference_text(row),
            }
        )
    return sorted(rows, key=function_sort_key)


def function_sort_key(row: dict[str, Any]) -> tuple[int, float, int, str, str]:
    order = {
        "exact": 0,
        "codegen-near": 1,
        "structural-near": 2,
        "semantic-started": 3,
        "semantic-gap": 4,
        "not-built": 5,
    }
    return (
        order.get(str(row["category"]), 99),
        -float(row["lcs_target_ratio"]),
        -int(row["lcs_instruction_count"]),
        str(row["unit"]),
        str(row["function"]),
    )


def tail(text: str, lines: int = 20) -> str:
    return "\n".join(text.splitlines()[-lines:])


def first_nonempty_line(*texts: str) -> str:
    for text in texts:
        for line in text.splitlines():
            stripped = line.strip()
            if stripped:
                return stripped[:180]
    return ""


def run_unit(unit: dict[str, str], profiles: list[Profile], args: argparse.Namespace) -> dict[str, Any]:
    results = [run_profile(unit, profile, args) for profile in profiles]
    successful = [result for result in results if result["returncode"] == 0 and result["best_row"]]
    best = max(successful, key=lambda result: row_key(result["best_row"])) if successful else None
    report = {
        "unit": unit["unit"],
        "compiler": args.compiler,
        "source": unit["source"],
        "port_file": unit["port_file"],
        "profiles": results,
        "best_profile": best["profile"] if best else "",
        "best_row": best["best_row"] if best else None,
    }
    report["function_bests"] = function_bests(report)
    write_unit_report(report)
    return report


def write_unit_report(report: dict[str, Any]) -> None:
    OUT_ROOT.mkdir(parents=True, exist_ok=True)
    unit = report["unit"]
    (OUT_ROOT / f"{unit}.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    lines = [
        f"# Compile Profile Search: {unit}",
        "",
        f"- Compiler: `{report.get('compiler', 'gcc')}`",
        f"- Source: `{report['source']}`",
        f"- Best profile: `{report['best_profile'] or 'none'}`",
        "",
        "| Profile | Status | Compared | Exact | Best function | Target | Compiled | Prefix | Suffix | LCS | LCS ratio | Longest run | First difference |",
        "| --- | --- | ---: | ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: | --- | --- |",
    ]
    for result in report["profiles"]:
        row = result.get("best_row") or {}
        diff = row.get("first_difference")
        run = row.get("longest_common_run")
        run_text = ""
        if isinstance(run, dict):
            run_text = f"{run.get('length', '')} @ {run.get('target_index', '')}/{run.get('compiled_index', '')}"
        diff_text = ""
        if isinstance(diff, dict):
            diff_text = f"{diff.get('index')}: `{diff.get('target')}` vs `{diff.get('compiled')}`"
        status = "ok" if result["returncode"] == 0 else f"failed:{result['returncode']}"
        if result["returncode"] != 0 and not diff_text:
            diff_text = first_nonempty_line(result.get("stderr_tail", ""), result.get("stdout_tail", ""))
        lines.append(
            f"| `{result['profile']}` | {status} | {result['compared_functions']} | {result['exact_functions']} | "
            f"`{row.get('name', '')}` | {row.get('target_instruction_count', '')} | "
            f"{row.get('compiled_instruction_count', '')} | {row.get('matching_prefix', '')} | "
            f"{row.get('matching_suffix', '')} | {row.get('lcs_instruction_count', '')} | "
            f"{row.get('lcs_target_ratio', '')} | {run_text} | {diff_text} |"
        )

    lines.extend(
        [
            "",
            "## Per-Function Bests",
            "",
            "| Category | Function | Best profile | Opt | Flags | Target | Compiled | Prefix | Suffix | LCS | Ratio | Run | First difference |",
            "| --- | --- | --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | --- | --- |",
        ]
    )
    for row in report.get("function_bests", []):
        lines.append(
            f"| `{row['category']}` | `{row['function']}` | `{row['profile']}` | `{row['optimization']}` | "
            f"`{row['cflags']}` | {row['target_instruction_count']} | {row['compiled_instruction_count']} | "
            f"{row['matching_prefix']} | {row['matching_suffix']} | {row['lcs_instruction_count']} | "
            f"{float(row['lcs_target_ratio']):.4f} | `{row['longest_common_run']}` | `{row['first_difference']}` |"
        )

    (OUT_ROOT / f"{unit}.md").write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_index(reports: list[dict[str, Any]]) -> None:
    OUT_ROOT.mkdir(parents=True, exist_ok=True)
    (OUT_ROOT / "index.json").write_text(json.dumps({"units": reports}, indent=2) + "\n", encoding="utf-8")

    fieldnames = [
        "unit",
        "compiler",
        "source",
        "best_profile",
        "function",
        "target_instruction_count",
        "compiled_instruction_count",
        "matching_prefix",
        "matching_suffix",
        "lcs_instruction_count",
        "lcs_target_ratio",
        "longest_common_run",
        "exact_match",
    ]
    with (OUT_ROOT / "index.csv").open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for report in reports:
            row = report.get("best_row") or {}
            run = row.get("longest_common_run")
            run_text = ""
            if isinstance(run, dict):
                run_text = f"{run.get('length', '')}@{run.get('target_index', '')}/{run.get('compiled_index', '')}"
            writer.writerow(
                {
                    "unit": report["unit"],
                    "compiler": report.get("compiler", "gcc"),
                    "source": report["source"],
                    "best_profile": report.get("best_profile", ""),
                    "function": row.get("name", ""),
                    "target_instruction_count": row.get("target_instruction_count", ""),
                    "compiled_instruction_count": row.get("compiled_instruction_count", ""),
                    "matching_prefix": row.get("matching_prefix", ""),
                    "matching_suffix": row.get("matching_suffix", ""),
                    "lcs_instruction_count": row.get("lcs_instruction_count", ""),
                    "lcs_target_ratio": row.get("lcs_target_ratio", ""),
                    "longest_common_run": run_text,
                    "exact_match": row.get("exact_match", ""),
                }
            )

    lines = [
        "# Structured Compile Profile Search",
        "",
        "| Unit | Compiler | Best profile | Function | Target | Compiled | Prefix | LCS | LCS ratio | Longest run | Exact |",
        "| --- | --- | --- | --- | ---: | ---: | ---: | ---: | ---: | --- | --- |",
    ]
    for report in reports:
        row = report.get("best_row") or {}
        run = row.get("longest_common_run")
        run_text = ""
        if isinstance(run, dict):
            run_text = f"{run.get('length', '')} @ {run.get('target_index', '')}/{run.get('compiled_index', '')}"
        lines.append(
            f"| `{report['unit']}` | `{report.get('compiler', 'gcc')}` | `{report.get('best_profile', '')}` | `{row.get('name', '')}` | "
            f"{row.get('target_instruction_count', '')} | {row.get('compiled_instruction_count', '')} | "
            f"{row.get('matching_prefix', '')} | {row.get('lcs_instruction_count', '')} | "
            f"{row.get('lcs_target_ratio', '')} | {run_text} | {str(row.get('exact_match', '')).lower()} |"
        )
    (OUT_ROOT / "index.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    write_function_index(reports)
    print((OUT_ROOT / "index.md").relative_to(ROOT))


def write_function_index(reports: list[dict[str, Any]]) -> None:
    rows: list[dict[str, Any]] = []
    for report in reports:
        if "function_bests" not in report:
            report["function_bests"] = function_bests(report)
        rows.extend(report.get("function_bests", []))
    rows.sort(key=function_sort_key)

    (OUT_ROOT / "function_index.json").write_text(json.dumps({"rows": rows}, indent=2) + "\n", encoding="utf-8")

    fieldnames = [
        "unit",
        "compiler",
        "function",
        "category",
        "profile",
        "optimization",
        "cflags",
        "target_instruction_count",
        "compiled_instruction_count",
        "matching_prefix",
        "matching_suffix",
        "lcs_instruction_count",
        "lcs_target_ratio",
        "longest_common_run",
        "first_difference",
        "exact_match",
        "source",
    ]
    with (OUT_ROOT / "function_index.csv").open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow({key: row.get(key, "") for key in fieldnames})

    lines = [
        "# Structured Compile Profile Function Index",
        "",
        "| Category | Unit | Compiler | Function | Best profile | Target | Compiled | Prefix | Suffix | LCS | Ratio | Run | Exact |",
        "| --- | --- | --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | --- | --- |",
    ]
    for row in rows:
        lines.append(
            f"| `{row['category']}` | `{row['unit']}` | `{row.get('compiler', 'gcc')}` | `{row['function']}` | `{row['profile']}` | "
            f"{row['target_instruction_count']} | {row['compiled_instruction_count']} | "
            f"{row['matching_prefix']} | {row['matching_suffix']} | {row['lcs_instruction_count']} | "
            f"{float(row['lcs_target_ratio']):.4f} | `{row['longest_common_run']}` | "
            f"{str(row['exact_match']).lower()} |"
        )
    (OUT_ROOT / "function_index.md").write_text("\n".join(lines) + "\n", encoding="utf-8")


def selected_units(args: argparse.Namespace, units: list[dict[str, str]]) -> list[dict[str, str]]:
    if args.all_units:
        return units
    selected = set(args.unit)
    return [unit for unit in units if unit["unit"] in selected]


def read_existing_report(unit_name: str) -> dict[str, Any] | None:
    path = OUT_ROOT / f"{unit_name}.json"
    if not path.is_file():
        return None
    return json.loads(path.read_text(encoding="utf-8"))


def merge_existing_reports(units: list[dict[str, str]], selected_reports: list[dict[str, Any]]) -> list[dict[str, Any]]:
    by_unit = {report["unit"]: report for report in selected_reports}
    for unit in units:
        unit_name = unit["unit"]
        if unit_name in by_unit:
            continue
        existing = read_existing_report(unit_name)
        if existing:
            by_unit[unit_name] = existing
    return [by_unit[unit["unit"]] for unit in units if unit["unit"] in by_unit]


def main() -> int:
    global BUILD_ROOT, OUT_ROOT

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--units", type=Path, default=UNITS)
    parser.add_argument("--unit", action="append", default=[], help="Structured unit to test; repeatable.")
    parser.add_argument("--all-units", action="store_true", help="Search all structured units.")
    parser.add_argument("--profile-set", choices=sorted(PROFILE_SETS), default="quick")
    parser.add_argument("--compiler", choices=["gcc", "armcc", "auto"], default="gcc")
    parser.add_argument("--armcc-path", default="")
    parser.add_argument("--out-root", type=Path)
    parser.add_argument("--build-root", default="")
    parser.add_argument("--index-only", action="store_true", help="Rebuild the index from existing unit reports.")
    args = parser.parse_args()

    if args.out_root is None:
        OUT_ROOT = DEFAULT_ARMCC_OUT_ROOT if args.compiler == "armcc" else DEFAULT_OUT_ROOT
    else:
        OUT_ROOT = args.out_root if args.out_root.is_absolute() else ROOT / args.out_root
    BUILD_ROOT = args.build_root or (DEFAULT_ARMCC_BUILD_ROOT if args.compiler == "armcc" else DEFAULT_BUILD_ROOT)

    units = read_units(args.units)
    if args.index_only:
        reports = merge_existing_reports(units, [])
        if not reports:
            raise SystemExit("no existing reports found")
        write_index(reports)
        return 0

    selected = selected_units(args, units)
    if not selected:
        raise SystemExit("no structured units selected")

    selected_reports = [run_unit(unit, PROFILE_SETS[args.profile_set], args) for unit in selected]
    write_index(merge_existing_reports(units, selected_reports))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
