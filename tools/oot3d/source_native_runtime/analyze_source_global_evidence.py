#!/usr/bin/env python3
"""Relate unresolved semantic globals to their promoted OOT3D evidence."""

from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
from collections import Counter, defaultdict
import csv
import hashlib
import json
from pathlib import Path
import re
import subprocess


TARGET_DATA_RE = re.compile(r"\b(?:DAT|LAB|PTR|UNK)_[0-9A-Fa-f]+\b|\biRam[0-9A-Fa-f]+\b")


def walk(node: dict):
    yield node
    for child in node.get("inner", ()):
        yield from walk(child)


def load_promotions(decomp_root: Path) -> dict[tuple[str, str], dict]:
    promotions = {}
    for path in sorted((decomp_root / "metadata").glob("*.csv")):
        with path.open(encoding="utf-8", newline="") as stream:
            try:
                rows = csv.DictReader(stream)
                for row in rows:
                    source = row.get("source_file", "")
                    function = row.get("definition_name") or row.get("semantic_name") or ""
                    evidence = row.get("target_evidence", "")
                    if source and function and evidence:
                        promotions[(source.replace("\\", "/"), function)] = {
                            "entry": row.get("entry", ""),
                            "evidence": evidence,
                            "manifest": path.relative_to(decomp_root).as_posix(),
                        }
            except (csv.Error, UnicodeDecodeError):
                continue
    return promotions


def inspect_source(clang: Path, include_root: Path, source_root: Path,
                   relative: str, unresolved: set[str]) -> tuple[str, list[dict], str | None]:
    result = subprocess.run([
        str(clang),
        "--target=x86_64-w64-windows-gnu",
        "-std=gnu11",
        "-w",
        "-Wno-error=incompatible-pointer-types",
        "-Wno-error=int-conversion",
        "-I", str(include_root),
        "-Xclang", "-ast-dump=json",
        "-fsyntax-only", str(source_root / relative),
    ], capture_output=True, text=True, errors="replace")
    if result.returncode != 0:
        return relative, [], result.stderr[-4000:]
    root = json.loads(result.stdout)
    functions = []
    for node in walk(root):
        if node.get("kind") != "FunctionDecl" or not any(
            child.get("kind") == "CompoundStmt" for child in node.get("inner", ())
        ):
            continue
        references = sorted({
            descendant.get("referencedDecl", {}).get("name")
            for descendant in walk(node)
            if descendant.get("kind") == "DeclRefExpr"
            and descendant.get("referencedDecl", {}).get("name") in unresolved
        })
        if references:
            functions.append({"name": node.get("name"), "globals": references})
    return relative, functions, None


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--clang", required=True, type=Path)
    parser.add_argument("--decomp-root", required=True, type=Path)
    parser.add_argument("--snapshot-root", required=True, type=Path)
    parser.add_argument("--snapshot-manifest", required=True, type=Path)
    parser.add_argument("--surface-contracts", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--jobs", type=int, default=8)
    args = parser.parse_args()

    snapshot = json.loads(args.snapshot_manifest.read_text(encoding="utf-8"))
    closure = json.loads(args.surface_contracts.read_text(encoding="utf-8"))
    unresolved_records = {
        record["name"]: record
        for record in closure["external_symbols"]
        if record["category"] == "source_global"
    }
    object_sources = {
        hashlib.sha256(record["path"].encode("utf-8")).hexdigest()[:16] + ".o":
            record["path"]
        for record in snapshot["canonical_sources"]
    }
    source_files = sorted({
        object_sources[owner]
        for record in unresolved_records.values()
        for owner in record.get("referenced_by", ())
        if owner in object_sources
    })
    promotions = load_promotions(args.decomp_root.resolve())

    failures = []
    function_records = []
    with ThreadPoolExecutor(max_workers=max(1, args.jobs)) as executor:
        futures = {
            executor.submit(
                inspect_source,
                args.clang.resolve(),
                (args.snapshot_root / "include").resolve(),
                args.snapshot_root.resolve(),
                relative,
                set(unresolved_records),
            ): relative
            for relative in source_files
        }
        for future in as_completed(futures):
            relative, functions, error = future.result()
            if error:
                failures.append({"source": relative, "error": error})
                continue
            for function in functions:
                promotion = promotions.get((relative, function["name"]))
                record = {"source": relative, **function}
                if promotion:
                    raw_evidence = promotion["evidence"].split("|", 1)[0]
                    raw_path = args.decomp_root / raw_evidence
                    candidates = []
                    if raw_path.is_file():
                        candidates = sorted(set(TARGET_DATA_RE.findall(
                            raw_path.read_text(encoding="utf-8", errors="replace")
                        )))
                    record.update({
                        "entry": promotion["entry"],
                        "evidence": raw_evidence,
                        "manifest": promotion["manifest"],
                        "target_data_candidates": candidates,
                    })
                function_records.append(record)

    evidence_by_global: dict[str, list[dict]] = defaultdict(list)
    for function in function_records:
        for name in function["globals"]:
            evidence_by_global[name].append(function)
    globals_report = []
    candidate_histogram = Counter()
    for name in sorted(unresolved_records):
        evidence = evidence_by_global.get(name, [])
        candidate_sets = [
            set(item["target_data_candidates"])
            for item in evidence
            if "target_data_candidates" in item
        ]
        candidates = sorted(set.intersection(*candidate_sets)) if candidate_sets else []
        candidate_histogram[len(candidates)] += 1
        globals_report.append({
            "name": name,
            "candidate_count": len(candidates),
            "candidates": candidates,
            "functions": [
                {
                    key: item[key]
                    for key in ("source", "name", "entry", "evidence")
                    if key in item
                }
                for item in evidence
            ],
        })

    report = {
        "schema_version": 1,
        "decomp_revision": snapshot["decomp_revision"],
        "unresolved_global_count": len(unresolved_records),
        "source_file_count": len(source_files),
        "function_with_global_count": len(function_records),
        "failed_source_count": len(failures),
        "candidate_count_histogram": {
            str(key): value for key, value in sorted(candidate_histogram.items())
        },
        "failures": failures,
        "globals": globals_report,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(report, indent=2) + "\n", encoding="utf-8", newline="\n"
    )
    print(json.dumps({
        key: report[key]
        for key in (
            "unresolved_global_count", "source_file_count",
            "function_with_global_count", "failed_source_count",
            "candidate_count_histogram",
        )
    }, indent=2))
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
