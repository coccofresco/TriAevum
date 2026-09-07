#!/usr/bin/env python3
"""Compile Ghidra leaf pseudocode as plain-C match candidates.

This probes leaf candidates in batch and only promotes functions whose compiled
instructions exactly match the OOT3D target disassembly.  This is a separate
lane from N64-derived ports: it is useful for exact C coverage and compiler
calibration, not for claiming semantic N64 source recovery.
"""

from __future__ import annotations

import argparse
import csv
import json
import re
import shutil
import subprocess
from collections import Counter
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any

from compare_runtime_objects import compare_ops, read_objdump_functions, read_target_functions
from probe_plain_c_fallbacks import configure_compiler


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CANDIDATES = ROOT / "analysis" / "leaf_function_candidates.json"
DEFAULT_BUILD_DIR = ROOT / "build" / "leaf_pseudocode_c_probe"
DEFAULT_OUT_JSON = ROOT / "analysis" / "leaf_pseudocode_c_probe.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "leaf_pseudocode_c_probe.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "leaf_pseudocode_c_probe.md"
DEFAULT_PROMOTION_SOURCE = ROOT / "src" / "leaf_pseudocode_matches.c"
DEFAULT_MATCHED_SOURCES = ROOT / "metadata" / "matched_sources.txt"
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


@dataclass
class ProbeResult:
    entry: str
    name: str
    category: str
    source: str
    score: int
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


def load_candidates(path: Path, limit: int) -> list[dict[str, Any]]:
    rows = read_json(path)
    if not isinstance(rows, list):
        raise SystemExit(f"{rel(path)} must contain a JSON list")
    if limit > 0:
        return rows[:limit]
    return rows


def standalone_source(row: dict[str, Any], text: str) -> str:
    return (
        GHIDRA_PREAMBLE
        + "\n"
        + f"/* Candidate {row['entry']} {row['name']} from {row['file']}. */\n"
        + text.strip()
        + "\n"
    )


def classify_match(comparison: dict[str, Any]) -> str:
    if comparison["exact_match"]:
        return "exact"
    if (
        comparison["lcs_target_ratio"] >= 0.5
        and comparison["target_instruction_count"] == comparison["compiled_instruction_count"]
    ):
        return "codegen-near"
    if comparison["lcs_target_ratio"] >= 0.3:
        return "structural-near"
    if comparison["lcs_instruction_count"] > 0:
        return "semantic-started"
    return "semantic-gap"


def format_run(run: dict[str, Any]) -> str:
    return f"{run.get('length', 0)}@{run.get('target_index', 0)}/{run.get('compiled_index', 0)}"


def format_first_difference(diff: dict[str, Any] | None) -> str:
    if not diff:
        return ""
    return f"{diff.get('index')}: {diff.get('target')} vs {diff.get('compiled')}"


def run_probe(args: argparse.Namespace) -> list[ProbeResult]:
    compiler = configure_compiler(args)
    candidates = load_candidates(args.candidates, args.limit)
    target_functions = read_target_functions(TARGET_DISASSEMBLY)

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

    results: list[ProbeResult] = []
    for index, row in enumerate(candidates, start=1):
        entry = str(row["entry"]).lower()
        name = str(row["name"])
        source = str(row["file"])
        score = int(row.get("score", 0) or 0)
        stem = sanitize_file_stem(f"{entry}_{name}")
        source_path = source_dir / f"{stem}.c"
        object_path = object_dir / f"{stem}.o"
        dump_path = dump_dir / f"{stem}.dump"
        stderr_path = log_dir / f"{stem}.stderr.txt"

        source_file = ROOT / source
        if not source_file.is_file():
            stderr_path.write_text(f"missing source file: {source}\n", encoding="utf-8")
            results.append(
                ProbeResult(
                    entry=entry,
                    name=name,
                    category="missing-source",
                    source=source,
                    score=score,
                    first_difference="source file missing; refresh leaf candidates after Ghidra export changes",
                    stderr_log=rel(stderr_path),
                )
            )
            continue

        text = source_file.read_text(encoding="utf-8", errors="replace")
        if re.search(r"\bin_[A-Za-z0-9_]+\b", text):
            stderr_path.write_text("skipped: Ghidra register pseudo-inputs need signature recovery\n", encoding="utf-8")
            results.append(
                ProbeResult(
                    entry=entry,
                    name=name,
                    category="skip-inreg",
                    source=source,
                    score=score,
                    stderr_log=rel(stderr_path),
                )
            )
            continue

        source_path.write_text(standalone_source(row, text), encoding="utf-8")
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
        stderr_path.write_text((completed.stdout or "") + (completed.stderr or ""), encoding="utf-8")
        if completed.returncode != 0:
            first_error = next(
                (line.strip() for line in stderr_path.read_text(encoding="utf-8", errors="replace").splitlines() if line.strip()),
                "compile failed",
            )
            results.append(
                ProbeResult(
                    entry=entry,
                    name=name,
                    category="compile-fail",
                    source=source,
                    score=score,
                    first_difference=first_error,
                    probe_source=rel(source_path),
                    stderr_log=rel(stderr_path),
                )
            )
            continue

        dumped = subprocess.run([compiler.objdump, "-dr", str(object_path)], cwd=ROOT, text=True, capture_output=True)
        dump_path.write_text(dumped.stdout + dumped.stderr, encoding="utf-8")
        if dumped.returncode != 0:
            results.append(
                ProbeResult(
                    entry=entry,
                    name=name,
                    category="dump-fail",
                    source=source,
                    score=score,
                    first_difference="objdump failed",
                    probe_source=rel(source_path),
                    object=rel(object_path),
                    dump=rel(dump_path),
                    stderr_log=rel(stderr_path),
                )
            )
            continue

        compiled = read_objdump_functions(dump_dir).get(name)
        target = target_functions.get(name)
        if compiled is None or target is None:
            results.append(
                ProbeResult(
                    entry=entry,
                    name=name,
                    category="compare-blocked",
                    source=source,
                    score=score,
                    first_difference="missing compiled function or target function",
                    probe_source=rel(source_path),
                    object=rel(object_path),
                    dump=rel(dump_path),
                    stderr_log=rel(stderr_path),
                )
            )
            continue

        comparison = compare_ops(target["ops"], compiled["ops"])
        results.append(
            ProbeResult(
                entry=entry,
                name=name,
                category=classify_match(comparison),
                source=source,
                score=score,
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
        )

        if args.progress and index % args.progress == 0:
            print(f"probed {index}/{len(candidates)} candidates")

    return results


def write_csv(path: Path, results: list[ProbeResult]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fields = list(asdict(results[0]).keys()) if results else list(ProbeResult.__dataclass_fields__.keys())
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        for result in results:
            writer.writerow(asdict(result))


def write_markdown(path: Path, results: list[ProbeResult], promoted_source: Path | None) -> None:
    counts = Counter(result.category for result in results)
    exact = [result for result in results if result.category == "exact"]
    near = [result for result in results if result.category == "codegen-near"]

    lines = [
        "# Leaf Pseudocode C Probe",
        "",
        "Generated by compiling leaf Ghidra pseudocode as standalone plain-C candidates and comparing against the OOT3D target disassembly.",
        "",
        "This lane is tracked separately from N64-derived ports; it is for exact C coverage and compiler calibration.",
        "",
        "## Summary",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Candidates probed | {len(results)} |",
        f"| Exact plain-C candidates | {counts['exact']} |",
        f"| Codegen-near candidates | {counts['codegen-near']} |",
        f"| Structural-near candidates | {counts['structural-near']} |",
        f"| Semantic-started candidates | {counts['semantic-started']} |",
        f"| Semantic-gap candidates | {counts['semantic-gap']} |",
        f"| Compile-failed candidates | {counts['compile-fail']} |",
        f"| Register-input skipped candidates | {counts['skip-inreg']} |",
        f"| Missing-source candidates | {counts['missing-source']} |",
    ]
    if promoted_source is not None:
        lines.append(f"| Promoted source | `{rel(promoted_source)}` |")

    lines.extend(
        [
            "",
            "## Exact Candidates",
            "",
            "| Entry | Name | Target | Source |",
            "| --- | --- | ---: | --- |",
        ]
    )
    for result in exact:
        lines.append(f"| `{result.entry}` | `{result.name}` | {result.target_instruction_count} | `{result.source}` |")

    lines.extend(
        [
            "",
            "## Codegen-Near Queue",
            "",
            "| Entry | Name | Target | Compiled | LCS | Run | First difference |",
            "| --- | --- | ---: | ---: | ---: | --- | --- |",
        ]
    )
    for result in near[:80]:
        lines.append(
            f"| `{result.entry}` | `{result.name}` | {result.target_instruction_count} | "
            f"{result.compiled_instruction_count} | {result.lcs_instruction_count} | "
            f"`{result.longest_common_run}` | `{result.first_difference}` |"
        )

    lines.append("")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def promotion_source(results: list[ProbeResult]) -> str:
    exact = sorted((result for result in results if result.category == "exact"), key=lambda item: int(item.entry, 16))
    chunks = [
        "/* Auto-promoted plain-C leaf matches from scripts/probe_leaf_pseudocode_c.py.",
        " * Generated from Ghidra leaf pseudocode, not from the N64 decompilation lane.",
        " * Keep this file mechanically exact and separate from semantic N64-derived ports.",
        " */",
        GHIDRA_PREAMBLE.rstrip(),
        "",
    ]
    for result in exact:
        text = (ROOT / result.source).read_text(encoding="utf-8", errors="replace").strip()
        text = "\n".join(line.rstrip() for line in text.splitlines())
        chunks.append(f"/* Ghidra leaf {result.name} @ 0x{result.entry}; source: {result.source}. */")
        chunks.append(text)
        chunks.append("")
    return "\n".join(chunks).rstrip() + "\n"


def update_matched_sources(path: Path, source: Path) -> None:
    source_rel = rel(source)
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    present = {line.strip().replace("\\", "/") for line in lines if line.strip() and not line.strip().startswith("#")}
    if source_rel in present:
        return
    lines.append(source_rel)
    path.write_text("\n".join(lines).rstrip() + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--candidates", type=Path, default=DEFAULT_CANDIDATES)
    parser.add_argument("--build-dir", type=Path, default=DEFAULT_BUILD_DIR)
    parser.add_argument("--out-json", type=Path, default=DEFAULT_OUT_JSON)
    parser.add_argument("--out-csv", type=Path, default=DEFAULT_OUT_CSV)
    parser.add_argument("--out-md", type=Path, default=DEFAULT_OUT_MD)
    parser.add_argument("--limit", type=int, default=0, help="Probe only the first N candidates; 0 means all")
    parser.add_argument("--progress", type=int, default=100, help="Print progress every N candidates; 0 disables")
    parser.add_argument("--compiler", choices=["gcc", "armcc", "auto"], default="gcc")
    parser.add_argument("--tool-prefix", default="arm-none-eabi")
    parser.add_argument("--optimization", default="-O2")
    parser.add_argument("--armcc-path", default="")
    parser.add_argument("--armcc-optimization", default="-Otime")
    parser.add_argument("--promote-exact", action="store_true", help="Write exact candidates to a maintained source file")
    parser.add_argument("--promotion-source", type=Path, default=DEFAULT_PROMOTION_SOURCE)
    parser.add_argument("--matched-sources", type=Path, default=DEFAULT_MATCHED_SOURCES)
    args = parser.parse_args()

    args.candidates = args.candidates.resolve()
    args.build_dir = args.build_dir.resolve()
    args.out_json = args.out_json.resolve()
    args.out_csv = args.out_csv.resolve()
    args.out_md = args.out_md.resolve()
    args.promotion_source = args.promotion_source.resolve()
    args.matched_sources = args.matched_sources.resolve()

    results = run_probe(args)
    counts = Counter(result.category for result in results)
    promoted_source = None
    if args.promote_exact:
        if counts["exact"] > 0:
            args.promotion_source.parent.mkdir(parents=True, exist_ok=True)
            args.promotion_source.write_text(promotion_source(results), encoding="utf-8")
            update_matched_sources(args.matched_sources, args.promotion_source)
            promoted_source = args.promotion_source
        else:
            print("no exact leaf candidates; leaving promoted source unchanged")

    write_json(args.out_json, {"summary": dict(counts), "results": [asdict(result) for result in results]})
    write_csv(args.out_csv, results)
    write_markdown(args.out_md, results, promoted_source)

    print(
        "leaf pseudocode C probe: "
        f"{counts['exact']} exact, {counts['codegen-near']} codegen-near, "
        f"{counts['structural-near']} structural-near, {counts['compile-fail']} compile-failed, "
        f"{counts['missing-source']} missing-source"
    )
    if promoted_source is not None:
        print(f"promoted exact leaf source: {rel(promoted_source)}")
    print(rel(args.out_md))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
