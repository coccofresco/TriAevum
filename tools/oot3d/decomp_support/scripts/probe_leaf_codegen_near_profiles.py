#!/usr/bin/env python3
"""Search local GCC optimize profiles for leaf codegen-near pseudocode rows.

This is a diagnostic lane for `analysis/leaf_pseudocode_c_probe.json` rows that
are already semantically close but not exact under the default standalone probe.
It does not promote source automatically; exact rows still need a reviewed
manual entry in `src/leaf_codegen_near_matches.c`.
"""

from __future__ import annotations

import argparse
import csv
import json
import re
import shutil
import subprocess
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any

from compare_runtime_objects import compare_ops, read_objdump_functions, read_target_functions
from probe_plain_c_fallbacks import configure_compiler


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_PROBE = ROOT / "analysis" / "leaf_pseudocode_c_probe.json"
DEFAULT_MATCHED_COMPARE = ROOT / "build" / "matched" / "compare_matched_objects.json"
DEFAULT_BUILD_DIR = ROOT / "build" / "leaf_codegen_near_profile_probe"
DEFAULT_OUT_JSON = ROOT / "analysis" / "leaf_codegen_near_profile_probe.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "leaf_codegen_near_profile_probe.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "leaf_codegen_near_profile_probe.md"
TARGET_DISASSEMBLY = ROOT / "ghidra_export" / "disassembly.txt"

GHIDRA_PREAMBLE = """#include "oot3d/types.h"
#include <stdbool.h>

typedef u8 undefined1;
typedef u16 undefined2;
typedef u32 undefined4;
typedef u64 undefined8;
typedef u8 byte;
typedef u16 ushort;
typedef u32 uint;
typedef void undefined;
"""

PROFILE_SETS: dict[str, list[str]] = {
    "quick": ["O2", "O1", "Os", "O3", "Og"],
    "extended": [
        "O2",
        "O1",
        "Os",
        "O3",
        "Og",
        "O2,no-tree-reassoc",
        "O2,no-tree-bit-ccp",
        "O2,no-tree-ter",
        "O2,no-if-conversion",
        "O2,no-if-conversion2",
        "O2,no-schedule-insns",
        "O2,no-schedule-insns2",
    ],
}


@dataclass
class ProfileResult:
    entry: str
    name: str
    source: str
    status: str
    best_profile: str = ""
    exact_profile: str = ""
    target_instruction_count: int = 0
    compiled_instruction_count: int = 0
    matching_prefix: int = 0
    matching_suffix: int = 0
    lcs_instruction_count: int = 0
    lcs_target_ratio: float = 0.0
    longest_common_run: str = ""
    first_difference: str = ""
    probe_source: str = ""
    object: str = ""
    dump: str = ""
    stderr_log: str = ""


def rel(path: Path) -> str:
    try:
        return str(path.relative_to(ROOT)).replace("\\", "/")
    except ValueError:
        return str(path).replace("\\", "/")


def read_json(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, data: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def sanitize_file_stem(value: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", value)


def load_codegen_near_rows(path: Path) -> list[dict[str, Any]]:
    data = read_json(path)
    rows = data.get("results", []) if isinstance(data, dict) else data
    if not isinstance(rows, list):
        raise SystemExit(f"{rel(path)} must contain a JSON list or a results list")
    return [row for row in rows if isinstance(row, dict) and row.get("category") == "codegen-near"]


def load_baseline_exact(path: Path) -> set[str]:
    if not path.is_file():
        return set()
    rows = read_json(path)
    if not isinstance(rows, list):
        return set()
    return {str(row.get("name")) for row in rows if row.get("exact_match")}


def standalone_source(row: dict[str, Any], text: str, profile: str) -> str:
    attribute = "" if profile == "O2" else f'__attribute__((optimize("{profile}")))\n'
    return (
        GHIDRA_PREAMBLE
        + "\n"
        + f"/* Profile probe {row['entry']} {row['name']} from {row['source']}. */\n"
        + attribute
        + text.strip()
        + "\n"
    )


def format_run(run: dict[str, Any]) -> str:
    return f"{run.get('length', 0)}@{run.get('target_index', 0)}/{run.get('compiled_index', 0)}"


def format_first_difference(diff: dict[str, Any] | None) -> str:
    if not diff:
        return ""
    return f"{diff.get('index')}: {diff.get('target')} vs {diff.get('compiled')}"


def comparison_key(comparison: dict[str, Any]) -> tuple[int, float, int, int, int, int]:
    run = comparison.get("longest_common_run", {})
    run_length = int(run.get("length", 0) or 0) if isinstance(run, dict) else 0
    return (
        1 if comparison.get("exact_match") else 0,
        float(comparison.get("lcs_target_ratio", 0.0) or 0.0),
        int(comparison.get("lcs_instruction_count", 0) or 0),
        int(comparison.get("matching_prefix", 0) or 0),
        int(comparison.get("matching_suffix", 0) or 0),
        run_length,
    )


def classify_best(comparison: dict[str, Any]) -> str:
    if comparison.get("exact_match"):
        return "exact-profile"
    if (
        comparison.get("lcs_target_ratio", 0.0) >= 0.5
        and comparison.get("target_instruction_count") == comparison.get("compiled_instruction_count")
    ):
        return "profile-codegen-near"
    if comparison.get("lcs_target_ratio", 0.0) >= 0.3:
        return "profile-structural-near"
    if comparison.get("lcs_instruction_count", 0) > 0:
        return "profile-semantic-started"
    return "profile-semantic-gap"


def result_from_comparison(
    row: dict[str, Any],
    status: str,
    best_profile: str,
    exact_profile: str,
    comparison: dict[str, Any],
    source_path: Path,
    object_path: Path,
    dump_path: Path,
    stderr_path: Path,
) -> ProfileResult:
    return ProfileResult(
        entry=str(row["entry"]).lower(),
        name=str(row["name"]),
        source=str(row["source"]),
        status=status,
        best_profile=best_profile,
        exact_profile=exact_profile,
        target_instruction_count=int(comparison["target_instruction_count"]),
        compiled_instruction_count=int(comparison["compiled_instruction_count"]),
        matching_prefix=int(comparison["matching_prefix"]),
        matching_suffix=int(comparison["matching_suffix"]),
        lcs_instruction_count=int(comparison["lcs_instruction_count"]),
        lcs_target_ratio=float(comparison["lcs_target_ratio"]),
        longest_common_run=format_run(comparison["longest_common_run"]),
        first_difference=format_first_difference(comparison["first_difference"]),
        probe_source=rel(source_path),
        object=rel(object_path),
        dump=rel(dump_path),
        stderr_log=rel(stderr_path),
    )


def run_probe(args: argparse.Namespace) -> list[ProfileResult]:
    compiler = configure_compiler(args)
    rows = load_codegen_near_rows(args.probe)
    baseline_exact = load_baseline_exact(args.matched_compare)
    if not args.include_baseline_exact:
        rows = [row for row in rows if str(row.get("name")) not in baseline_exact]
    if args.limit > 0:
        rows = rows[: args.limit]

    target_functions = read_target_functions(TARGET_DISASSEMBLY)
    profiles = PROFILE_SETS[args.profile_set]

    if args.build_dir.exists():
        shutil.rmtree(args.build_dir)
    source_dir = args.build_dir / "sources"
    object_dir = args.build_dir / "objects"
    dump_dir = args.build_dir / "dumps"
    log_dir = args.build_dir / "logs"
    source_dir.mkdir(parents=True)
    object_dir.mkdir()
    dump_dir.mkdir()
    log_dir.mkdir()

    results: list[ProfileResult] = []
    for index, row in enumerate(rows, start=1):
        entry = str(row["entry"]).lower()
        name = str(row["name"])
        source = str(row["source"])
        source_file = ROOT / source
        stem = sanitize_file_stem(f"{entry}_{name}")
        stderr_path = log_dir / f"{stem}.stderr.txt"

        if not source_file.is_file():
            stderr_path.write_text(f"missing source file: {source}\n", encoding="utf-8")
            results.append(ProfileResult(entry, name, source, "missing-source", stderr_log=rel(stderr_path)))
            continue

        text = source_file.read_text(encoding="utf-8", errors="replace")
        if re.search(r"\bin_[A-Za-z0-9_]+\b", text):
            stderr_path.write_text("skipped: Ghidra register pseudo-inputs need signature recovery\n", encoding="utf-8")
            results.append(ProfileResult(entry, name, source, "skip-inreg", stderr_log=rel(stderr_path)))
            continue

        target = target_functions.get(name)
        if target is None:
            stderr_path.write_text("target function missing from disassembly\n", encoding="utf-8")
            results.append(ProfileResult(entry, name, source, "compare-blocked", stderr_log=rel(stderr_path)))
            continue

        best: tuple[str, dict[str, Any], Path, Path, Path] | None = None
        compile_failures: list[str] = []
        for profile in profiles:
            profile_stem = sanitize_file_stem(f"{stem}_{profile}")
            source_path = source_dir / f"{profile_stem}.c"
            object_path = object_dir / f"{profile_stem}.o"
            profile_dump_dir = dump_dir / profile_stem
            profile_dump_dir.mkdir()
            dump_path = profile_dump_dir / f"{profile_stem}.dump"
            profile_stderr_path = log_dir / f"{profile_stem}.stderr.txt"
            source_path.write_text(standalone_source(row, text, profile), encoding="utf-8")

            completed = subprocess.run(
                [
                    compiler.cc,
                    *compiler.flags,
                    "-Wno-int-to-pointer-cast",
                    "-Wno-pointer-to-int-cast",
                    "-Wno-incompatible-pointer-types",
                    "-c",
                    str(source_path),
                    "-o",
                    str(object_path),
                ],
                cwd=ROOT,
                text=True,
                capture_output=True,
            )
            profile_stderr_path.write_text((completed.stdout or "") + (completed.stderr or ""), encoding="utf-8")
            if completed.returncode != 0:
                compile_failures.append(f"{profile}: compile failed")
                continue

            dumped = subprocess.run([compiler.objdump, "-dr", str(object_path)], cwd=ROOT, text=True, capture_output=True)
            dump_path.write_text(dumped.stdout + dumped.stderr, encoding="utf-8")
            if dumped.returncode != 0:
                compile_failures.append(f"{profile}: objdump failed")
                continue

            compiled = read_objdump_functions(profile_dump_dir).get(name)
            if compiled is None:
                compile_failures.append(f"{profile}: compiled symbol missing")
                continue

            comparison = compare_ops(target["ops"], compiled["ops"])
            if best is None or comparison_key(comparison) > comparison_key(best[1]):
                best = (profile, comparison, source_path, object_path, dump_path)
            if comparison["exact_match"] and not args.keep_searching:
                break

        if best is None:
            stderr_path.write_text("\n".join(compile_failures).rstrip() + "\n", encoding="utf-8")
            results.append(ProfileResult(entry, name, source, "compile-fail", stderr_log=rel(stderr_path)))
            continue

        best_profile, comparison, source_path, object_path, dump_path = best
        status = classify_best(comparison)
        exact_profile = best_profile if comparison["exact_match"] else ""
        results.append(
            result_from_comparison(
                row,
                status,
                best_profile,
                exact_profile,
                comparison,
                source_path,
                object_path,
                dump_path,
                log_dir / f"{sanitize_file_stem(f'{stem}_{best_profile}')}.stderr.txt",
            )
        )

        if args.progress and index % args.progress == 0:
            print(f"profile-probed {index}/{len(rows)} codegen-near rows")

    return results


def write_csv(path: Path, results: list[ProfileResult]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fields = list(ProfileResult.__dataclass_fields__.keys())
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        for result in results:
            writer.writerow(asdict(result))


def write_markdown(path: Path, results: list[ProfileResult], args: argparse.Namespace) -> None:
    counts: dict[str, int] = {}
    for result in results:
        counts[result.status] = counts.get(result.status, 0) + 1
    exact = [result for result in results if result.status == "exact-profile"]
    near = [result for result in results if result.status == "profile-codegen-near"]

    lines = [
        "# Leaf Codegen-Near Profile Probe",
        "",
        "Generated by recompiling leaf `codegen-near` pseudocode with local GCC `optimize(...)` profiles.",
        "",
        "This is a diagnostic lane. Exact rows are promotion candidates only after a reviewed source-shape entry is added to `src/leaf_codegen_near_matches.c` and the full matched build remains exact.",
        "",
        "## Summary",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Profile set | `{args.profile_set}` |",
        f"| Baseline exact excluded | {'no' if args.include_baseline_exact else 'yes'} |",
        f"| Rows probed | {len(results)} |",
        f"| Exact profile candidates | {counts.get('exact-profile', 0)} |",
        f"| Profile codegen-near rows | {counts.get('profile-codegen-near', 0)} |",
        f"| Profile structural-near rows | {counts.get('profile-structural-near', 0)} |",
        f"| Profile semantic-started rows | {counts.get('profile-semantic-started', 0)} |",
        f"| Compile-failed rows | {counts.get('compile-fail', 0)} |",
        "",
        "## Exact Profile Candidates",
        "",
        "| Entry | Name | Profile | Target | Source |",
        "| --- | --- | --- | ---: | --- |",
    ]
    for result in exact:
        lines.append(
            f"| `{result.entry}` | `{result.name}` | `{result.exact_profile}` | "
            f"{result.target_instruction_count} | `{result.source}` |"
        )

    lines.extend(
        [
            "",
            "## Best Remaining Codegen-Near Rows",
            "",
            "| Entry | Name | Best profile | Target | Compiled | LCS | Run | First difference |",
            "| --- | --- | --- | ---: | ---: | ---: | --- | --- |",
        ]
    )
    for result in near[:80]:
        lines.append(
            f"| `{result.entry}` | `{result.name}` | `{result.best_profile}` | "
            f"{result.target_instruction_count} | {result.compiled_instruction_count} | "
            f"{result.lcs_instruction_count} | `{result.longest_common_run}` | `{result.first_difference}` |"
        )

    lines.append("")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe", type=Path, default=DEFAULT_PROBE)
    parser.add_argument("--matched-compare", type=Path, default=DEFAULT_MATCHED_COMPARE)
    parser.add_argument("--build-dir", type=Path, default=DEFAULT_BUILD_DIR)
    parser.add_argument("--out-json", type=Path, default=DEFAULT_OUT_JSON)
    parser.add_argument("--out-csv", type=Path, default=DEFAULT_OUT_CSV)
    parser.add_argument("--out-md", type=Path, default=DEFAULT_OUT_MD)
    parser.add_argument("--profile-set", choices=sorted(PROFILE_SETS), default="extended")
    parser.add_argument("--include-baseline-exact", action="store_true")
    parser.add_argument("--keep-searching", action="store_true", help="Try all profiles even after finding an exact one")
    parser.add_argument("--limit", type=int, default=0)
    parser.add_argument("--progress", type=int, default=25)
    parser.add_argument("--compiler", choices=["gcc", "armcc", "auto"], default="gcc")
    parser.add_argument("--tool-prefix", default="arm-none-eabi")
    parser.add_argument("--optimization", default="-O2")
    parser.add_argument("--armcc-path", default="")
    parser.add_argument("--armcc-optimization", default="-Otime")
    args = parser.parse_args()

    args.probe = args.probe.resolve()
    args.matched_compare = args.matched_compare.resolve()
    args.build_dir = args.build_dir.resolve()
    args.out_json = args.out_json.resolve()
    args.out_csv = args.out_csv.resolve()
    args.out_md = args.out_md.resolve()

    results = run_probe(args)
    write_json(args.out_json, {"results": [asdict(result) for result in results]})
    write_csv(args.out_csv, results)
    write_markdown(args.out_md, results, args)

    exact_count = sum(1 for result in results if result.status == "exact-profile")
    near_count = sum(1 for result in results if result.status == "profile-codegen-near")
    print(
        "leaf codegen-near profile probe: "
        f"{len(results)} rows, {exact_count} exact-profile, {near_count} profile-codegen-near"
    )
    print(rel(args.out_md))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
