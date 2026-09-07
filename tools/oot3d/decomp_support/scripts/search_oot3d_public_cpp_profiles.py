#!/usr/bin/env python3
"""Search compiler profiles for oot3d_before_public C++ bodies.

This wraps probe_oot3d_public_cpp.py across multiple ARM GNU toolchains and
flag profiles, then builds a single index of the best observed codegen for each
mapped public-reference function.
"""

from __future__ import annotations

import argparse
import csv
import json
import os
import re
import subprocess
import sys
import time
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUT_ROOT = ROOT / "analysis" / "oot3d_public_cpp_profile_search"
DEFAULT_BUILD_ROOT = ROOT / "build" / "oot3d_public_cpp_profile_search"
GCC15_ROOT = Path("C:/Users/xander/Downloads/arm-gnu-toolchain-15.2.rel1-mingw-w64-x86_64-arm-none-eabi")


@dataclass(frozen=True)
class Profile:
    name: str
    optimization: str
    flags: tuple[str, ...] = ()


@dataclass(frozen=True)
class Toolchain:
    name: str
    root: Path


PROFILE_SETS: dict[str, list[Profile]] = {
    "quick": [
        Profile("o2", "-O2"),
        Profile("o1", "-O1"),
        Profile("os", "-Os"),
        Profile("o3", "-O3"),
        Profile("o2_no_schedule", "-O2", ("-fno-schedule-insns", "-fno-schedule-insns2")),
        Profile("o2_no_tree_sra", "-O2", ("-fno-tree-sra", "-fno-ipa-sra")),
    ],
    "wide": [
        Profile("o2", "-O2"),
        Profile("o1", "-O1"),
        Profile("os", "-Os"),
        Profile("o3", "-O3"),
        Profile("o2_no_schedule", "-O2", ("-fno-schedule-insns", "-fno-schedule-insns2")),
        Profile("o2_no_tree_sra", "-O2", ("-fno-tree-sra", "-fno-ipa-sra")),
        Profile("o2_no_reorder", "-O2", ("-fno-reorder-blocks", "-fno-reorder-functions")),
        Profile("o2_no_gcse", "-O2", ("-fno-gcse", "-fno-cse-follow-jumps")),
        Profile("o2_no_if_conversion", "-O2", ("-fno-if-conversion", "-fno-if-conversion2")),
        Profile("o2_no_sibling_calls", "-O2", ("-fno-optimize-sibling-calls",)),
        Profile("o2_no_crossjump", "-O2", ("-fno-crossjumping",)),
        Profile("o2_no_strict_alias", "-O2", ("-fno-strict-aliasing",)),
        Profile("o2_no_inline_small", "-O2", ("-fno-inline-small-functions",)),
        Profile("o2_no_dce", "-O2", ("-fno-dce", "-fno-dse")),
    ],
}

CATEGORY_SCORE = {
    "exact": 5,
    "codegen-near": 4,
    "structural-near": 3,
    "semantic-started": 2,
    "semantic-gap": 1,
    "compare-blocked": 0,
    "compile-fail": 0,
}


def rel(path: Path | str) -> str:
    path = Path(path)
    try:
        return str(path.relative_to(ROOT)).replace("\\", "/")
    except ValueError:
        return str(path).replace("\\", "/")


def sanitize(value: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", value)


def parse_toolchain(value: str) -> Toolchain:
    if "=" not in value:
        raise argparse.ArgumentTypeError("--toolchain must be NAME=PATH")
    name, raw_path = value.split("=", 1)
    name = sanitize(name.strip())
    if not name:
        raise argparse.ArgumentTypeError("toolchain name cannot be empty")
    path = Path(raw_path.strip()).resolve()
    if not (path / "bin" / "arm-none-eabi-g++.exe").is_file():
        raise argparse.ArgumentTypeError(f"toolchain is missing bin/arm-none-eabi-g++.exe: {path}")
    return Toolchain(name, path)


def detect_toolchains() -> list[Toolchain]:
    rows: list[Toolchain] = []
    seen: set[Path] = set()

    candidates = []
    if os.environ.get("DEVKITARM"):
        candidates.append(("devkitarm_env", Path(os.environ["DEVKITARM"])))
    candidates.append(("devkitarm", Path("C:/devkitPro/devkitARM")))
    candidates.append(("arm_gcc15", GCC15_ROOT))

    for name, root in candidates:
        root = root.resolve()
        if root in seen:
            continue
        if (root / "bin" / "arm-none-eabi-g++.exe").is_file():
            rows.append(Toolchain(name, root))
            seen.add(root)
    return rows


def result_counts(results: list[dict[str, Any]]) -> Counter[str]:
    return Counter(str(row.get("category", "")) for row in results)


def score_row(row: dict[str, Any]) -> tuple[Any, ...]:
    target = int(row.get("target_instruction_count") or 0)
    compiled = int(row.get("compiled_instruction_count") or 0)
    return (
        CATEGORY_SCORE.get(str(row.get("category", "")), -1),
        int(row.get("lcs_instruction_count") or 0),
        float(row.get("lcs_target_ratio") or 0.0),
        int(row.get("matching_prefix") or 0),
        -abs(target - compiled),
        target,
    )


def read_matched_baseline(path: Path) -> dict[str, str]:
    if not path.is_file():
        return {}

    rows = json.loads(path.read_text(encoding="utf-8"))
    matched: dict[str, str] = {}
    for row in rows:
        if row.get("exact_match"):
            entry = str(row.get("target_entry", "")).lower()
            name = str(row.get("name", ""))
            if entry and name:
                matched[entry] = name
    return matched


def profile_command(
    toolchain: Toolchain,
    profile: Profile,
    args: argparse.Namespace,
    stem: str,
) -> list[str]:
    command = [
        sys.executable,
        str(ROOT / "scripts" / "probe_oot3d_public_cpp.py"),
        "--reference",
        str(args.reference),
        "--target-disassembly",
        str(args.target_disassembly),
        "--tool-root",
        str(toolchain.root),
        f"--optimization={profile.optimization}",
        "--build-dir",
        str(args.build_root / stem),
        "--out-json",
        str(args.out_root / "profiles" / f"{stem}.json"),
        "--out-csv",
        str(args.out_root / "profiles" / f"{stem}.csv"),
        "--out-md",
        str(args.out_root / "profiles" / f"{stem}.md"),
        "--progress",
        str(args.progress),
    ]
    if args.limit_files:
        command += ["--limit-files", str(args.limit_files)]
    if args.limit_functions:
        command += ["--limit-functions", str(args.limit_functions)]
    for flag in profile.flags:
        command += [f"--extra-cxx-flag={flag}"]
    return command


def run_profile(toolchain: Toolchain, profile: Profile, args: argparse.Namespace) -> dict[str, Any]:
    stem = sanitize(f"{toolchain.name}_{profile.name}")
    json_path = args.out_root / "profiles" / f"{stem}.json"
    command = profile_command(toolchain, profile, args, stem)
    started = time.monotonic()

    if args.skip_existing and json_path.is_file():
        completed = None
        status = "cached"
    else:
        print(f"running {toolchain.name}/{profile.name}")
        completed = subprocess.run(command, cwd=ROOT, text=True, capture_output=True)
        status = "ok" if completed.returncode == 0 else "failed"

    elapsed = round(time.monotonic() - started, 3)
    row: dict[str, Any] = {
        "toolchain": toolchain.name,
        "tool_root": str(toolchain.root),
        "profile": profile.name,
        "optimization": profile.optimization,
        "flags": list(profile.flags),
        "stem": stem,
        "status": status,
        "duration_seconds": elapsed,
        "command": command,
        "stdout": completed.stdout if completed else "",
        "stderr": completed.stderr if completed else "",
        "json": rel(json_path),
        "markdown": rel(args.out_root / "profiles" / f"{stem}.md"),
        "results": [],
        "summary": {},
    }

    if status in {"ok", "cached"} and json_path.is_file():
        payload = json.loads(json_path.read_text(encoding="utf-8"))
        results = payload.get("results", [])
        summary = payload.get("summary", {})
        counts = result_counts(results)
        row["results"] = results
        row["summary"] = summary
        row["mapped_function_bodies"] = summary.get("mapped_function_bodies", 0)
        row["compiled_source_files"] = summary.get("compiled_source_files", 0)
        row["mapped_source_files"] = summary.get("mapped_source_files", 0)
        row["compiled_symbols"] = summary.get("compiled_symbols", 0)
        for category in CATEGORY_SCORE:
            row[f"{category.replace('-', '_')}_functions"] = counts[category]
        comparable = [result for result in results if int(result.get("target_instruction_count") or 0)]
        best = max(comparable, key=score_row) if comparable else {}
        row["best_result"] = best
    elif completed:
        row["returncode"] = completed.returncode
    return row


def best_function_rows(profile_rows: list[dict[str, Any]], matched_by_entry: dict[str, str]) -> list[dict[str, Any]]:
    best: dict[tuple[str, str], dict[str, Any]] = {}
    for profile_row in profile_rows:
        if profile_row.get("status") not in {"ok", "cached"}:
            continue
        for result in profile_row.get("results", []):
            key = (str(result.get("entry", "")), str(result.get("object_name", "")))
            enriched = dict(result)
            matched_name = matched_by_entry.get(str(result.get("entry", "")).lower(), "")
            enriched.update(
                {
                    "toolchain": profile_row["toolchain"],
                    "profile": profile_row["profile"],
                    "optimization": profile_row["optimization"],
                    "flags": " ".join(profile_row["flags"]),
                    "matched_baseline_exact": bool(matched_name),
                    "matched_baseline_name": matched_name,
                }
            )
            if key not in best or score_row(enriched) > score_row(best[key]):
                best[key] = enriched
    return sorted(best.values(), key=score_row, reverse=True)


def write_json(path: Path, profile_rows: list[dict[str, Any]], function_rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    compact_profiles = []
    for row in profile_rows:
        compact = dict(row)
        compact.pop("results", None)
        compact_profiles.append(compact)
    payload = {
        "profiles": compact_profiles,
        "function_best": function_rows,
    }
    path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def write_profile_csv(path: Path, profile_rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fields = [
        "toolchain",
        "tool_root",
        "profile",
        "optimization",
        "flags",
        "status",
        "duration_seconds",
        "mapped_function_bodies",
        "compiled_source_files",
        "mapped_source_files",
        "compiled_symbols",
        "exact_functions",
        "codegen_near_functions",
        "structural_near_functions",
        "semantic_started_functions",
        "semantic_gap_functions",
        "compare_blocked_functions",
        "compile_fail_functions",
        "best_function",
        "best_category",
        "best_lcs",
        "json",
        "markdown",
    ]
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        for row in profile_rows:
            best = row.get("best_result") or {}
            writer.writerow(
                {
                    **{field: row.get(field, "") for field in fields},
                    "flags": " ".join(row.get("flags", [])),
                    "best_function": best.get("object_name", ""),
                    "best_category": best.get("category", ""),
                    "best_lcs": best.get("lcs_instruction_count", ""),
                }
            )


def write_function_csv(path: Path, rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fields = [
        "category",
        "entry",
        "object_name",
        "toolchain",
        "profile",
        "optimization",
        "flags",
        "target_instruction_count",
        "compiled_instruction_count",
        "matching_prefix",
        "matching_suffix",
        "lcs_instruction_count",
        "lcs_target_ratio",
        "longest_common_run",
        "source",
        "line",
        "first_difference",
        "matched_baseline_exact",
        "matched_baseline_name",
    ]
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        for row in rows:
            writer.writerow({field: row.get(field, "") for field in fields})


def candidate_rows(rows: list[dict[str, Any]]) -> list[dict[str, Any]]:
    return [
        row
        for row in rows
        if not row.get("matched_baseline_exact")
        and row.get("category") in {"codegen-near", "structural-near"}
    ]


def suggest_action(row: dict[str, Any]) -> str:
    target = int(row.get("target_instruction_count") or 0)
    compiled = int(row.get("compiled_instruction_count") or 0)
    diff = str(row.get("first_difference", ""))
    if target == compiled and diff.startswith("0: stmdb"):
        return "Tune register pressure/prologue shape; instruction count already matches."
    if target == compiled:
        return "Try local source-shape rewrite; instruction count already matches."
    if "cmp r1,#32 vs cmp r1,#31" in diff:
        return "Rewrite boundary condition around flag < 0x20."
    if abs(target - compiled) <= 2:
        return "Fix small scheduling/register-order gap before broader rewrites."
    return "Lower structs/calls toward target offsets, then rerun profile search."


def write_candidates_csv(path: Path, rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fields = [
        "category",
        "entry",
        "object_name",
        "toolchain",
        "profile",
        "optimization",
        "flags",
        "target_instruction_count",
        "compiled_instruction_count",
        "lcs_instruction_count",
        "lcs_target_ratio",
        "source",
        "line",
        "first_difference",
        "suggested_action",
    ]
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        for row in rows:
            output = {field: row.get(field, "") for field in fields}
            output["suggested_action"] = suggest_action(row)
            writer.writerow(output)


def write_candidates_markdown(path: Path, rows: list[dict[str, Any]]) -> None:
    lines = [
        "# oot3d_before_public New Candidates",
        "",
        "Filtered worklist from the public C++ profile search. Rows already exact in the maintained baseline are excluded.",
        "",
        "| Category | Entry | Name | Profile | Target | Compiled | LCS | Source | Suggested action |",
        "| --- | --- | --- | --- | ---: | ---: | ---: | --- | --- |",
    ]
    for row in rows:
        lines.append(
            f"| `{row.get('category', '')}` | `{row.get('entry', '')}` | `{row.get('object_name', '')}` | "
            f"`{row.get('toolchain', '')}/{row.get('profile', '')}` | "
            f"{row.get('target_instruction_count', 0)} | {row.get('compiled_instruction_count', 0)} | "
            f"{row.get('lcs_instruction_count', 0)} | `{row.get('source', '')}:{row.get('line', '')}` | "
            f"{suggest_action(row)} |"
        )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_markdown(path: Path, profile_rows: list[dict[str, Any]], function_rows: list[dict[str, Any]]) -> None:
    lines = [
        "# oot3d_before_public C++ Profile Search",
        "",
        "Batch search over ARM GNU compiler versions and source-level flag profiles for mapped public-reference C++ bodies.",
        "",
        "## Profiles",
        "",
        "| Toolchain | Profile | Opt | Flags | Status | Exact | Codegen-near | Structural-near | Semantic-started | Files | Best |",
        "| --- | --- | --- | --- | --- | ---: | ---: | ---: | ---: | ---: | --- |",
    ]
    for row in sorted(
        profile_rows,
        key=lambda item: (
            -int(item.get("exact_functions") or 0),
            -int(item.get("codegen_near_functions") or 0),
            -int(item.get("structural_near_functions") or 0),
            item.get("toolchain", ""),
            item.get("profile", ""),
        ),
    ):
        best = row.get("best_result") or {}
        files = f"{row.get('compiled_source_files', 0)}/{row.get('mapped_source_files', 0)}"
        lines.append(
            f"| `{row['toolchain']}` | `{row['profile']}` | `{row['optimization']}` | "
            f"`{' '.join(row.get('flags', []))}` | {row['status']} | "
            f"{row.get('exact_functions', 0)} | {row.get('codegen_near_functions', 0)} | "
            f"{row.get('structural_near_functions', 0)} | {row.get('semantic_started_functions', 0)} | "
            f"{files} | `{best.get('object_name', '')}:{best.get('category', '')}` |"
        )

    counts = Counter(row.get("category", "") for row in function_rows)
    baseline_exact = sum(1 for row in function_rows if row.get("matched_baseline_exact"))
    new_best = [row for row in function_rows if not row.get("matched_baseline_exact")]
    lines.extend(
        [
            "",
            "## Best Per Function",
            "",
            f"Baseline-exact rows are already covered by maintained sources under the shown baseline name. New work should prioritize rows where this column is empty. Baseline-exact best rows: {baseline_exact}. New best rows: {len(new_best)}.",
            "",
            "| Category | Entry | Name | Baseline exact | Toolchain | Profile | Target | Compiled | LCS | Ratio | Source | First difference |",
            "| --- | --- | --- | --- | --- | --- | ---: | ---: | ---: | ---: | --- | --- |",
        ]
    )
    for row in function_rows[:100]:
        lines.append(
            f"| `{row.get('category', '')}` | `{row.get('entry', '')}` | `{row.get('object_name', '')}` | "
            f"`{row.get('matched_baseline_name', '')}` | "
            f"`{row.get('toolchain', '')}` | `{row.get('profile', '')}` | "
            f"{row.get('target_instruction_count', 0)} | {row.get('compiled_instruction_count', 0)} | "
            f"{row.get('lcs_instruction_count', 0)} | {row.get('lcs_target_ratio', 0)} | "
            f"`{row.get('source', '')}:{row.get('line', '')}` | `{row.get('first_difference', '')}` |"
        )

    lines.extend(
        [
            "",
            "## Output Files",
            "",
            "- `function_best.csv`: best observed profile per mapped public function.",
            "- `new_candidates.csv` / `new_candidates.md`: filtered non-baseline near matches for the next source-shaping batch.",
            "",
            "## Function Summary",
            "",
            "| Category | Best-function count |",
            "| --- | ---: |",
        ]
    )
    for category in ("exact", "codegen-near", "structural-near", "semantic-started", "semantic-gap", "compare-blocked", "compile-fail"):
        lines.append(f"| `{category}` | {counts[category]} |")
    lines.append(f"| `baseline-exact` | {baseline_exact} |")
    lines.append(f"| `new-or-unmatched` | {len(new_best)} |")

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", type=Path, default=ROOT.parent / "external" / "oot3d_before_public")
    parser.add_argument("--target-disassembly", type=Path, default=ROOT / "ghidra_export" / "disassembly.txt")
    parser.add_argument("--out-root", type=Path, default=DEFAULT_OUT_ROOT)
    parser.add_argument("--build-root", type=Path, default=DEFAULT_BUILD_ROOT)
    parser.add_argument("--matched-compare", type=Path, default=ROOT / "build" / "matched" / "compare_matched_objects.json")
    parser.add_argument("--profile-set", choices=sorted(PROFILE_SETS), default="quick")
    parser.add_argument("--toolchain", action="append", type=parse_toolchain, default=[])
    parser.add_argument("--only-toolchain", action="append", default=[])
    parser.add_argument("--limit-profiles", type=int, default=0)
    parser.add_argument("--limit-files", type=int, default=0)
    parser.add_argument("--limit-functions", type=int, default=0)
    parser.add_argument("--progress", type=int, default=0)
    parser.add_argument("--skip-existing", action="store_true")
    args = parser.parse_args()

    args.reference = args.reference.resolve()
    args.target_disassembly = args.target_disassembly.resolve()
    args.out_root = args.out_root.resolve()
    args.build_root = args.build_root.resolve()
    args.matched_compare = args.matched_compare.resolve()
    args.out_root.mkdir(parents=True, exist_ok=True)
    (args.out_root / "profiles").mkdir(parents=True, exist_ok=True)
    args.build_root.mkdir(parents=True, exist_ok=True)

    toolchains = args.toolchain or detect_toolchains()
    if args.only_toolchain:
        wanted = {sanitize(name) for name in args.only_toolchain}
        toolchains = [toolchain for toolchain in toolchains if toolchain.name in wanted]
    if not toolchains:
        raise SystemExit("no ARM GNU toolchains found")

    profiles = PROFILE_SETS[args.profile_set]
    if args.limit_profiles > 0:
        profiles = profiles[: args.limit_profiles]

    profile_rows = [run_profile(toolchain, profile, args) for toolchain in toolchains for profile in profiles]
    matched_by_entry = read_matched_baseline(args.matched_compare)
    function_rows = best_function_rows(profile_rows, matched_by_entry)

    write_json(args.out_root / "index.json", profile_rows, function_rows)
    write_profile_csv(args.out_root / "index.csv", profile_rows)
    write_function_csv(args.out_root / "function_best.csv", function_rows)
    candidates = candidate_rows(function_rows)
    write_candidates_csv(args.out_root / "new_candidates.csv", candidates)
    write_candidates_markdown(args.out_root / "new_candidates.md", candidates)
    write_markdown(args.out_root / "index.md", profile_rows, function_rows)

    counts = Counter(row.get("category", "") for row in function_rows)
    print(
        "oot3d public C++ profile search: "
        f"{len(profile_rows)} profiles, "
        f"{counts['exact']} best exact, {counts['codegen-near']} best codegen-near, "
        f"{counts['structural-near']} best structural-near"
    )
    print(rel(args.out_root / "index.md"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
