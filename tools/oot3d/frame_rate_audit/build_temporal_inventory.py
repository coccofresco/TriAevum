#!/usr/bin/env python3
"""Build a reproducible OOT3D temporal-site inventory by owner graph."""

from __future__ import annotations

import argparse
import csv
import json
import re
import subprocess
from bisect import bisect_right
from collections import Counter, defaultdict, deque
from pathlib import Path
from typing import Any


SCRIPT_ROOT = Path(__file__).resolve().parent
DEFAULT_CONFIG = SCRIPT_ROOT / "temporal_inventory_config.json"

SITE_PATTERNS: tuple[tuple[str, re.Pattern[str], str], ...] = (
    (
        "native_rate",
        re.compile(r"(?:\+\s*0x110\b|NativeUpdateRate|update_?rate)", re.I),
        "continuous",
    ),
    (
        "integer_decrement",
        re.compile(
            r"(?:--|-\=\s*1\b|(?<![=!<>])=(?!=)[^;]*?"
            r"(?:\+\s*-1\b|-\s*1\b))"
        ),
        "logical_frame_candidate",
    ),
    (
        "integer_increment",
        re.compile(
            r"(?:\+\+|\+\=\s*1\b|(?<![=!<>])=(?!=)[^;]*?\+\s*1\b)"
        ),
        "logical_frame_candidate",
    ),
    (
        "equality_comparison",
        re.compile(r"(?:==|!=)\s*(?:0x[0-9a-f]+|\d+)\b", re.I),
        "unknown",
    ),
    (
        "rng_call",
        re.compile(r"\b(?:Rand_[A-Za-z0-9_]*|Math_Rand[A-Za-z0-9_]*)\s*\("),
        "event_candidate",
    ),
    (
        "audio_call",
        re.compile(r"\b(?:Audio_[A-Za-z0-9_]*|Sfx[A-Za-z0-9_]*)\s*\("),
        "event_candidate",
    ),
    (
        "animation_event",
        re.compile(
            r"\b(?:Animation_OnFrame[A-Za-z0-9_]*|SkelAnime_Update|"
            r"Oot3d_(?:Change|Play)Animation[A-Za-z0-9_]*)\s*\("
        ),
        "event_candidate",
    ),
    (
        "cutscene_event",
        re.compile(r"\b(?:Cutscene_[A-Za-z0-9_]*|OnePointCutscene_[A-Za-z0-9_]*)\s*\("),
        "event_candidate",
    ),
)


def normalize_entry(value: Any) -> str:
    text = str(value).strip().lower()
    return text.removeprefix("0x").zfill(8)


def load_json(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8"))


def git_revision(root: Path) -> str:
    result = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=root,
        capture_output=True,
        check=False,
        text=True,
    )
    return result.stdout.strip() if result.returncode == 0 else "unknown"


def load_model(decomp_root: Path) -> tuple[dict[str, dict[str, Any]], dict[str, list[str]]]:
    functions_path = decomp_root / "analysis" / "functions_enriched.json"
    callgraph_path = decomp_root / "analysis" / "callgraph.json"
    functions = load_json(functions_path)
    graph = load_json(callgraph_path)
    by_entry = {
        normalize_entry(row["entry"]): row
        for row in functions
    }
    outgoing: dict[str, list[str]] = defaultdict(list)
    for edge in graph.get("edges", []):
        outgoing[normalize_entry(edge["caller"])].append(
            normalize_entry(edge["callee"])
        )
    for callees in outgoing.values():
        callees.sort()
    return by_entry, outgoing


def owner_closure(
    owner: dict[str, Any],
    outgoing: dict[str, list[str]],
) -> dict[str, int]:
    maximum_depth = int(owner.get("maximum_call_depth", 0))
    stop_entries = {
        normalize_entry(entry) for entry in owner.get("stop_entries", [])
    }
    depths: dict[str, int] = {}
    queue: deque[tuple[str, int]] = deque(
        (normalize_entry(root), 0) for root in owner.get("roots", [])
    )
    while queue:
        entry, depth = queue.popleft()
        if entry in depths and depths[entry] <= depth:
            continue
        depths[entry] = depth
        if depth >= maximum_depth or entry in stop_entries:
            continue
        for callee in outgoing.get(entry, []):
            queue.append((callee, depth + 1))
    return depths


def resolve_source(decomp_root: Path, function: dict[str, Any]) -> Path | None:
    relative = function.get("file")
    if relative:
        candidate = decomp_root / str(relative)
        if candidate.is_file():
            return candidate
    entry = normalize_entry(function["entry"])
    matches = sorted(
        (decomp_root / "ghidra_export" / "decompiled").glob(f"*_{entry}_*.c")
    )
    return matches[0] if matches else None


def load_runtime_hits(
    trace_path: Path | None,
    functions: dict[str, dict[str, Any]],
) -> Counter[str]:
    hits: Counter[str] = Counter()
    if trace_path is None:
        return hits
    ranges = sorted(
        (
            int(normalize_entry(row.get("body_min", entry)), 16),
            int(normalize_entry(row.get("body_max", entry)), 16),
            entry,
        )
        for entry, row in functions.items()
    )
    starts = [start for start, _end, _entry in ranges]
    pc_pattern = re.compile(rb'"pc":(\d+)')
    with trace_path.open("rb") as trace:
        for line in trace:
            match = pc_pattern.search(line)
            if match is None:
                continue
            pc = int(match.group(1))
            range_index = bisect_right(starts, pc) - 1
            if range_index < 0:
                continue
            start, end, entry = ranges[range_index]
            if start <= pc <= end:
                hits[entry] += 1
    return hits


def build_overrides(config: dict[str, Any]) -> dict[tuple[str, str, int], str]:
    overrides: dict[tuple[str, str, int], str] = {}
    for row in config.get("site_overrides", []):
        key = (
            normalize_entry(row["entry"]),
            str(row["kind"]),
            int(row["line"]),
        )
        overrides[key] = str(row["domain"])
    return overrides


def scan_function(
    decomp_root: Path,
    entry: str,
    function: dict[str, Any],
    owners: list[dict[str, Any]],
    runtime_hits: int,
    overrides: dict[tuple[str, str, int], str],
) -> dict[str, Any]:
    source_path = resolve_source(decomp_root, function)
    sites: list[dict[str, Any]] = []
    if source_path is not None:
        for line_number, raw_line in enumerate(
            source_path.read_text(encoding="utf-8", errors="replace").splitlines(),
            start=1,
        ):
            line = raw_line.strip()
            if not line or line.startswith(("/*", "*", "//")):
                continue
            for kind, pattern, default_domain in SITE_PATTERNS:
                if pattern.search(line) is None:
                    continue
                if kind in {"integer_decrement", "integer_increment"}:
                    assignment = re.search(
                        r"(?<![=!<>])=(?!=)(?P<rhs>[^;]+)", line
                    )
                    if assignment is not None and re.search(
                        r"(?:==|!=|<=|>=|<|>)", assignment.group("rhs")
                    ):
                        continue
                domain = overrides.get(
                    (entry, kind, line_number), default_domain
                )
                sites.append(
                    {
                        "id": f"{entry}:{kind}:{line_number}",
                        "kind": kind,
                        "line": line_number,
                        "domain": domain,
                        "evidence": line[:300],
                    }
                )

    candidate_sites = [
        site for site in sites if site["kind"] != "equality_comparison"
    ]
    domains = {site["domain"] for site in candidate_sites}
    has_continuous = "continuous" in domains
    has_discrete = bool(
        domains
        & {"logical_frame_candidate", "event_candidate", "logical_frame"}
    )
    if has_continuous and has_discrete:
        classification = "mixed_unknown"
    elif has_continuous:
        classification = (
            "continuous_candidate_with_comparisons"
            if len(candidate_sites) != len(sites)
            else "continuous_candidate"
        )
    elif has_discrete:
        classification = "logical_or_event_candidate"
    elif sites:
        classification = "branch_comparison_only"
    else:
        classification = "no_temporal_site_detected"

    return {
        "entry": entry,
        "name": str(function.get("name", f"FUN_{entry}")),
        "source": (
            source_path.relative_to(decomp_root).as_posix()
            if source_path is not None
            else None
        ),
        "line_count": int(function.get("line_count", 0)),
        "call_count": int(function.get("call_count", 0)),
        "runtime_hits": runtime_hits,
        "owners": sorted(owners, key=lambda row: (row["id"], row["depth"])),
        "classification": classification,
        "site_count": len(sites),
        "candidate_site_count": len(candidate_sites),
        "site_kinds": dict(sorted(Counter(site["kind"] for site in sites).items())),
        "sites": sites,
    }


def write_outputs(output_dir: Path, document: dict[str, Any]) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    json_path = output_dir / "temporal_inventory.json"
    csv_path = output_dir / "temporal_inventory.csv"
    markdown_path = output_dir / "temporal_inventory.md"
    json_path.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")

    with csv_path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(
            stream,
            fieldnames=[
                "entry",
                "name",
                "owners",
                "classification",
                "runtime_hits",
                "line_count",
                "call_count",
                "site_count",
                "candidate_site_count",
                "site_kinds",
                "source",
            ],
        )
        writer.writeheader()
        for row in document["functions"]:
            writer.writerow(
                {
                    **{key: row[key] for key in writer.fieldnames if key in row},
                    "owners": ";".join(owner["id"] for owner in row["owners"]),
                    "site_kinds": json.dumps(row["site_kinds"], sort_keys=True),
                }
            )

    summary = document["summary"]
    lines = [
        "# OOT3D temporal inventory",
        "",
        f"- Decomp revision: `{document['source']['revision']}`",
        f"- Owners: {', '.join(document['selected_owners'])}",
        f"- Functions scanned: {summary['functions_scanned']}",
        f"- Sites: {summary['sites']}",
        f"- Temporal candidates: {summary['candidate_sites']}",
        f"- Mixed/unknown functions: {summary['mixed_unknown_functions']}",
        "",
        "## Highest-priority functions",
        "",
        "| Entry | Function | Owner | Class | Hits | Candidates | Lines |",
        "| --- | --- | --- | --- | ---: | ---: | ---: |",
    ]
    ranked = sorted(
        document["functions"],
        key=lambda row: (
            -int(row["runtime_hits"]),
            row["classification"] != "mixed_unknown",
            -int(row["candidate_site_count"]),
            -int(row["line_count"]),
            row["entry"],
        ),
    )
    for row in ranked[:50]:
        owners = ", ".join(owner["id"] for owner in row["owners"])
        lines.append(
            f"| `{row['entry']}` | {row['name']} | {owners} | "
            f"{row['classification']} | {row['runtime_hits']} | "
            f"{row['candidate_site_count']} | {row['line_count']} |"
        )
    markdown_path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--decomp-root", type=Path, required=True)
    parser.add_argument("--config", type=Path, default=DEFAULT_CONFIG)
    parser.add_argument("--owner", action="append", default=[])
    parser.add_argument("--runtime-trace", type=Path)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()

    decomp_root = args.decomp_root.resolve()
    config = load_json(args.config.resolve())
    if config.get("schema") != "oot3d_temporal_inventory_config_v1":
        raise RuntimeError("unsupported temporal inventory config")
    selected = set(args.owner)
    owners = [
        owner
        for owner in config.get("owners", [])
        if not selected or owner["id"] in selected
    ]
    if not owners:
        raise RuntimeError("no temporal inventory owner selected")

    functions, outgoing = load_model(decomp_root)
    ownership: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for owner in owners:
        for entry, depth in owner_closure(owner, outgoing).items():
            ownership[entry].append({"id": owner["id"], "depth": depth})
    runtime_hits = load_runtime_hits(args.runtime_trace, functions)
    overrides = build_overrides(config)
    rows = [
        scan_function(
            decomp_root,
            entry,
            functions.get(entry, {"entry": entry, "name": f"FUN_{entry}"}),
            owner_rows,
            runtime_hits[entry],
            overrides,
        )
        for entry, owner_rows in sorted(ownership.items())
    ]
    document = {
        "schema": "oot3d_temporal_inventory_v1",
        "source": {
            "decomp_root": str(decomp_root),
            "revision": git_revision(decomp_root),
            "functions_enriched": "analysis/functions_enriched.json",
            "callgraph": "analysis/callgraph.json",
        },
        "selected_owners": [owner["id"] for owner in owners],
        "summary": {
            "functions_scanned": len(rows),
            "sites": sum(row["site_count"] for row in rows),
            "candidate_sites": sum(
                row["candidate_site_count"] for row in rows
            ),
            "mixed_unknown_functions": sum(
                row["classification"] == "mixed_unknown" for row in rows
            ),
            "functions_with_runtime_hits": sum(
                row["runtime_hits"] != 0 for row in rows
            ),
        },
        "functions": rows,
    }
    write_outputs(args.output_dir.resolve(), document)
    print(
        f"temporal inventory: {len(rows)} functions, "
        f"{document['summary']['sites']} sites"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
