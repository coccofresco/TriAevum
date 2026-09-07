#!/usr/bin/env python3
"""Plan file-level N64-to-OOT3D porting batches.

The function ranker is intentionally conservative and works one OOT3D function
at a time. This script aggregates those function-level matches by N64 source
file so the next porting pass can move by subsystem/file, promote names in
batches, and choose small functions that are realistic matching seeds.
"""

from __future__ import annotations

import argparse
import csv
import json
import re
import shutil
from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path
from statistics import mean

from symbol_overlay import identifier_overlay, load_manual_symbols, overlay_identifier_text


ROOT = Path(__file__).resolve().parents[1]
ANALYSIS = ROOT / "analysis"
DEFAULT_RANK_JSONS = (
    ANALYSIS / "n64_port_candidate_rank_pause_item.json",
    ANALYSIS / "n64_port_candidate_rank.json",
)
MANUAL_SYMBOLS = ROOT / "symbols" / "manual_symbols.csv"
REVIEWED_PROMOTIONS_DIR = ANALYSIS / "reviewed_promotions"
FUNCTION_POINTER_REFS = ANALYSIS / "function_pointer_refs.json"
TRUTHY = {"1", "true", "yes", "y", "approved", "promote"}


@dataclass
class CandidateHit:
    rank_json: str
    oot3d_entry: str
    oot3d_name: str
    oot3d_path: str
    oot3d_lines: int
    candidate_rank: int
    score: float
    score_gap: float
    n64_name: str
    n64_path: str
    n64_lines: int
    confidence: str
    evidence: dict[str, list[str]]
    reviewed_defer: bool = False
    review_note: str = ""
    pointer_targets: list[dict[str, str]] = field(default_factory=list)
    pointer_sources: list[dict[str, str]] = field(default_factory=list)

    @property
    def is_top(self) -> bool:
        return self.candidate_rank == 1

    @property
    def promotion_ready(self) -> bool:
        return (
            not self.reviewed_defer
            and self.is_top
            and self.confidence in {"high", "medium"}
            and self.score >= 180
        )

    @property
    def matching_seed(self) -> bool:
        return (
            not self.reviewed_defer
            and self.is_top
            and self.score >= 170
            and self.oot3d_lines <= 140
            and self.n64_lines <= 220
        )

    @property
    def weighted_score(self) -> float:
        rank_weights = {1: 1.0, 2: 0.68, 3: 0.46, 4: 0.32, 5: 0.24, 6: 0.18}
        return self.score * rank_weights.get(self.candidate_rank, 0.12)


@dataclass
class FileLane:
    n64_path: str
    hits_by_entry: dict[str, CandidateHit] = field(default_factory=dict)

    def add(self, hit: CandidateHit) -> None:
        current = self.hits_by_entry.get(hit.oot3d_entry)
        if current is None or hit.weighted_score > current.weighted_score:
            self.hits_by_entry[hit.oot3d_entry] = hit

    @property
    def hits(self) -> list[CandidateHit]:
        return sorted(
            self.hits_by_entry.values(),
            key=lambda hit: (not hit.is_top, -hit.score, hit.oot3d_entry),
        )

    @property
    def top_hits(self) -> list[CandidateHit]:
        return [hit for hit in self.hits if hit.is_top]

    @property
    def top_n64_counts(self) -> dict[str, int]:
        counts: dict[str, int] = defaultdict(int)
        for hit in self.top_hits:
            counts[hit.n64_name] += 1
        return dict(counts)

    @property
    def fanout_n64_names(self) -> set[str]:
        return {name for name, count in self.top_n64_counts.items() if count >= 4}

    @property
    def fanout_hits(self) -> list[CandidateHit]:
        fanout_names = self.fanout_n64_names
        return [hit for hit in self.hits if hit.is_top and hit.n64_name in fanout_names]

    @property
    def promotion_ready_hits(self) -> list[CandidateHit]:
        conflict_names = self.name_conflict_suggested_names
        return [
            hit
            for hit in self.base_promotion_ready_hits
            if suggested_oot3d_name(hit.n64_name) not in conflict_names
        ]

    @property
    def base_promotion_ready_hits(self) -> list[CandidateHit]:
        fanout_names = self.fanout_n64_names
        return [hit for hit in self.hits if hit.promotion_ready and hit.n64_name not in fanout_names]

    @property
    def name_conflict_suggested_names(self) -> set[str]:
        counts: dict[str, int] = defaultdict(int)
        for hit in self.base_promotion_ready_hits:
            counts[suggested_oot3d_name(hit.n64_name)] += 1
        return {name for name, count in counts.items() if count > 1}

    @property
    def name_conflict_hits(self) -> list[CandidateHit]:
        conflict_names = self.name_conflict_suggested_names
        return [
            hit
            for hit in self.base_promotion_ready_hits
            if suggested_oot3d_name(hit.n64_name) in conflict_names
        ]

    @property
    def matching_seed_hits(self) -> list[CandidateHit]:
        fanout_names = self.fanout_n64_names
        return [hit for hit in self.hits if hit.matching_seed and hit.n64_name not in fanout_names]

    @property
    def reviewed_defer_hits(self) -> list[CandidateHit]:
        return [hit for hit in self.hits if hit.reviewed_defer]

    @property
    def lane_score(self) -> float:
        hits = self.hits
        fanout_penalty = 75 * sum(max(0, count - 3) for count in self.top_n64_counts.values())
        top_bonus = 45 * len(self.top_n64_counts)
        promotion_bonus = 28 * len(self.promotion_ready_hits)
        seed_bonus = 36 * len(self.matching_seed_hits)
        concentration_bonus = 18 * max(0, len(hits) - 1)
        return sum(hit.weighted_score for hit in hits) + top_bonus + promotion_bonus + seed_bonus + concentration_bonus - fanout_penalty

    @property
    def action(self) -> str:
        if len(self.promotion_ready_hits) >= 4:
            return "batch-promote-and-port"
        if len(self.matching_seed_hits) >= 3:
            return "matching-seed-batch"
        if len(self.top_hits) >= 2 or self.fanout_n64_names:
            return "subsystem-review-batch"
        return "single-lane-review"


def rel(path: Path, root: Path = ROOT) -> str:
    try:
        return str(path.relative_to(root)).replace("\\", "/")
    except ValueError:
        try:
            return str(path.relative_to(ROOT.parent)).replace("\\", "/")
        except ValueError:
            return str(path).replace("\\", "/")


def confidence(score: float, gap: float, candidate_rank: int) -> str:
    if candidate_rank == 1 and score >= 240 and gap >= 18:
        return "high"
    if candidate_rank == 1 and score >= 180 and gap >= 7:
        return "medium"
    if candidate_rank <= 2 and score >= 200 and gap >= -3:
        return "context"
    return "weak"


def load_manual_entries(path: Path) -> set[str]:
    if not path.is_file():
        return set()
    with path.open(newline="", encoding="utf-8") as handle:
        return {row["entry"].lower() for row in csv.DictReader(handle)}


def normalize_entry(entry: str) -> str:
    return entry.strip().lower().removeprefix("0x")


def is_truthy(value: str | None) -> bool:
    return (value or "").strip().lower() in TRUTHY


def load_reviewed_defer_entries(path: Path) -> dict[str, str]:
    if not path.is_dir():
        return {}
    reviewed: dict[str, str] = {}
    for csv_path in sorted(path.glob("*.csv")):
        with csv_path.open(newline="", encoding="utf-8-sig") as handle:
            reader = csv.DictReader(handle)
            if "entry" not in (reader.fieldnames or []) or "approved" not in (reader.fieldnames or []):
                continue
            for row in reader:
                approved = row.get("approved")
                if approved is None or approved.strip() == "" or is_truthy(approved):
                    continue
                entry = normalize_entry(row.get("entry", ""))
                if not entry:
                    continue
                note = (row.get("notes") or "").strip()
                reviewed[entry] = note or f"Deferred by {rel(csv_path)}."
    return reviewed


def load_function_pointer_refs(path: Path) -> dict[str, dict[str, list[dict[str, str]]]]:
    refs: dict[str, dict[str, list[dict[str, str]]]] = defaultdict(lambda: {"targets": [], "sources": []})
    if not path.is_file():
        return refs

    data = json.loads(path.read_text(encoding="utf-8"))
    for row in data.get("rows", []):
        source_entry = normalize_entry(str(row.get("source_entry", "")))
        target_entry = normalize_entry(str(row.get("target_entry", "")))
        if not source_entry or not target_entry:
            continue
        refs[source_entry]["targets"].append(
            {
                "entry": target_entry,
                "name": str(row.get("target_name", "")),
                "ref_name": str(row.get("ref_name", "")),
                "literal_address": str(row.get("literal_address", "")),
                "line": str(row.get("line", "")),
            }
        )
        refs[target_entry]["sources"].append(
            {
                "entry": source_entry,
                "name": str(row.get("source_name", "")),
                "ref_name": str(row.get("ref_name", "")),
                "literal_address": str(row.get("literal_address", "")),
                "line": str(row.get("line", "")),
            }
        )
    return refs


def unique_pointer_refs(rows: list[dict[str, str]], limit: int = 6) -> list[dict[str, str]]:
    seen: set[tuple[str, str]] = set()
    output: list[dict[str, str]] = []
    for row in rows:
        key = (row.get("entry", ""), row.get("literal_address", ""))
        if key in seen:
            continue
        seen.add(key)
        output.append(row)
        if len(output) >= limit:
            break
    return output


def load_rank_hits(
    path: Path,
    manual_entries: set[str],
    reviewed_defer_entries: dict[str, str],
    function_pointer_refs: dict[str, dict[str, list[dict[str, str]]]],
    include_named: bool,
) -> list[CandidateHit]:
    data = json.loads(path.read_text(encoding="utf-8"))
    hits: list[CandidateHit] = []
    for match in data.get("matches", []):
        entry = str(match["oot3d_entry"]).lower()
        if not include_named and entry in manual_entries:
            continue
        candidates = match.get("candidates", [])
        if not candidates:
            continue
        scores = [float(candidate["score"]) for candidate in candidates]
        for index, candidate in enumerate(candidates, start=1):
            score = float(candidate["score"])
            if index == 1:
                gap = score - (scores[1] if len(scores) > 1 else 0.0)
            else:
                gap = score - scores[0]
            pointer_refs = function_pointer_refs.get(entry, {})
            hits.append(
                CandidateHit(
                    rank_json=rel(path),
                    oot3d_entry=entry,
                    oot3d_name=match["oot3d_name"],
                    oot3d_path=match["oot3d_path"],
                    oot3d_lines=int(match["oot3d_lines"]),
                    candidate_rank=index,
                    score=score,
                    score_gap=gap,
                    n64_name=candidate["n64_name"],
                    n64_path=candidate["n64_path"],
                    n64_lines=int(candidate["n64_lines"]),
                    confidence=confidence(score, gap, index),
                    evidence=candidate.get("evidence", {}),
                    reviewed_defer=entry in reviewed_defer_entries,
                    review_note=reviewed_defer_entries.get(entry, ""),
                    pointer_targets=unique_pointer_refs(pointer_refs.get("targets", [])),
                    pointer_sources=unique_pointer_refs(pointer_refs.get("sources", [])),
                )
            )
    return hits


def merge_hits_by_entry_source(hits: list[CandidateHit]) -> list[CandidateHit]:
    best: dict[tuple[str, str], CandidateHit] = {}
    for hit in hits:
        key = (hit.oot3d_entry, hit.n64_path)
        current = best.get(key)
        if current is None or hit.weighted_score > current.weighted_score:
            best[key] = hit
    return list(best.values())


def build_lanes(hits: list[CandidateHit]) -> list[FileLane]:
    lanes_by_path: dict[str, FileLane] = defaultdict(lambda: FileLane(""))
    for hit in hits:
        lane = lanes_by_path.get(hit.n64_path)
        if lane is None or not lane.n64_path:
            lane = FileLane(hit.n64_path)
            lanes_by_path[hit.n64_path] = lane
        lane.add(hit)
    return sorted(lanes_by_path.values(), key=lambda lane: (-lane.lane_score, lane.n64_path))


def split_words(name: str) -> list[str]:
    name = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", name)
    return [part.lower() for part in re.split(r"[^A-Za-z0-9]+", name) if part]


def suggested_oot3d_name(n64_name: str) -> str:
    words = split_words(n64_name)
    replacements = {
        "kaleido": "pause",
        "scope": "",
        "interface": "interface",
    }
    mapped = [replacements.get(word, word) for word in words]
    mapped = [word for word in mapped if word]
    if not mapped or mapped[0] != "oot3d":
        mapped.insert(0, "oot3d")
    return "_".join(mapped)


def evidence_summary(hit: CandidateHit) -> str:
    evidence = hit.evidence or {}
    parts = []
    for key in ("constants", "tokens", "call_tokens", "strings"):
        values = evidence.get(key) or []
        if values:
            parts.append(f"{key}:{','.join(values[:5])}")
    if hit.pointer_targets:
        targets = [f"{row['entry']}/{row['name']}" for row in hit.pointer_targets[:4]]
        parts.append(f"ptr_out:{','.join(targets)}")
    if hit.pointer_sources:
        sources = [f"{row['entry']}/{row['name']}" for row in hit.pointer_sources[:4]]
        parts.append(f"ptr_in:{','.join(sources)}")
    return "; ".join(parts)


def write_csv(path: Path, lanes: list[FileLane]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        fieldnames = [
            "rank",
            "n64_path",
            "action",
            "lane_score",
            "oot3d_functions",
            "top_hits",
            "unique_top_n64_functions",
            "promotion_ready",
            "name_conflicts",
            "matching_seeds",
            "fanout_review",
            "reviewed_defer",
            "avg_score",
            "best_entries",
        ]
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for index, lane in enumerate(lanes, start=1):
            scores = [hit.score for hit in lane.hits]
            writer.writerow(
                {
                    "rank": index,
                    "n64_path": lane.n64_path,
                    "action": lane.action,
                    "lane_score": f"{lane.lane_score:.2f}",
                    "oot3d_functions": len(lane.hits),
                    "top_hits": len(lane.top_hits),
                    "unique_top_n64_functions": len(lane.top_n64_counts),
                    "promotion_ready": len(lane.promotion_ready_hits),
                    "name_conflicts": len(lane.name_conflict_hits),
                    "matching_seeds": len(lane.matching_seed_hits),
                    "fanout_review": len(lane.fanout_hits),
                    "reviewed_defer": len(lane.reviewed_defer_hits),
                    "avg_score": f"{mean(scores):.2f}" if scores else "0.00",
                    "best_entries": ", ".join(hit.oot3d_entry for hit in lane.hits[:8]),
                }
            )


def lane_to_json(lane: FileLane, rank: int) -> dict[str, object]:
    return {
        "rank": rank,
        "n64_path": lane.n64_path,
        "action": lane.action,
        "lane_score": round(lane.lane_score, 2),
        "oot3d_functions": len(lane.hits),
        "top_hits": len(lane.top_hits),
        "unique_top_n64_functions": len(lane.top_n64_counts),
        "promotion_ready": len(lane.promotion_ready_hits),
        "name_conflicts": len(lane.name_conflict_hits),
        "matching_seeds": len(lane.matching_seed_hits),
        "fanout_review": len(lane.fanout_hits),
        "reviewed_defer": len(lane.reviewed_defer_hits),
        "fanout_n64_names": [
            {"n64_name": name, "top_hit_count": count}
            for name, count in sorted(lane.top_n64_counts.items(), key=lambda item: (-item[1], item[0]))
            if name in lane.fanout_n64_names
        ],
        "hits": [
            {
                "oot3d_entry": hit.oot3d_entry,
                "oot3d_name": hit.oot3d_name,
                "oot3d_path": hit.oot3d_path,
                "oot3d_lines": hit.oot3d_lines,
                "candidate_rank": hit.candidate_rank,
                "score": hit.score,
                "score_gap": round(hit.score_gap, 2),
                "confidence": hit.confidence,
                "n64_name": hit.n64_name,
                "suggested_name": suggested_oot3d_name(hit.n64_name),
                "n64_lines": hit.n64_lines,
                "promotion_ready": hit in lane.promotion_ready_hits,
                "name_conflict": hit in lane.name_conflict_hits,
                "matching_seed": hit in lane.matching_seed_hits,
                "fanout_review": hit in lane.fanout_hits,
                "reviewed_defer": hit.reviewed_defer,
                "review_note": hit.review_note,
                "evidence": hit.evidence,
                "pointer_targets": hit.pointer_targets,
                "pointer_sources": hit.pointer_sources,
            }
            for hit in lane.hits
        ],
    }


def write_json(path: Path, lanes: list[FileLane], rank_jsons: list[Path]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    data = {
        "rank_jsons": [rel(path) for path in rank_jsons],
        "lane_count": len(lanes),
        "lanes": [lane_to_json(lane, index) for index, lane in enumerate(lanes, start=1)],
    }
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def write_markdown(path: Path, lanes: list[FileLane], rank_jsons: list[Path], top_lanes: int) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "# N64 Batch Porting Plan",
        "",
        "Generated from `scripts/plan_n64_batch_porting.py`.",
        "",
        "This report aggregates function-level N64/OOT3D ranker output by N64 source file. "
        "Use it to stop reviewing isolated functions and instead port coherent file/subsystem lanes.",
        "",
        "## Inputs",
        "",
    ]
    for path_item in rank_jsons:
        lines.append(f"- `{rel(path_item)}`")
    if FUNCTION_POINTER_REFS.is_file():
        lines.append(f"- `{rel(FUNCTION_POINTER_REFS)}`")
    lines.extend(
        [
            "",
            "## Faster Workflow",
            "",
            "1. Run the normal function rankers, then this batch planner.",
            "2. Pick the first lane with `batch-promote-and-port` or `matching-seed-batch` action. Use `subsystem-review-batch` lanes for shared structs/macros and semantic clustering before direct rename work.",
            "3. Review all top-hit functions in that lane together against the N64 source file; use `ptr_out` and `ptr_in` evidence for action/state-machine chains that static callers miss.",
            "4. Batch-promote only names with `high` or `medium` confidence that are not marked `fanout-review`, `name-conflict-review`, or `reviewed-defer`, then run `scripts\\refresh-n64-porting-plan.ps1`.",
            "5. Create one maintained source file for the lane, port shared structs/macros first, and add only the smallest `matching_seed` functions to the build.",
            "6. Use selective Ghidra export only at checkpoints that need refreshed pseudocode/call neighborhoods.",
            "7. Use `build-runtime-objects.ps1` after each small batch; keep exact matches, quarantine near misses.",
            "",
            "## Top File Lanes",
            "",
            "| Rank | N64 source | Action | Lane score | OOT3D funcs | Top hits | Unique top funcs | Promotion-ready | Name conflicts | Matching seeds | Fan-out review | Reviewed defer | Best entries |",
            "| ---: | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |",
        ]
    )
    for index, lane in enumerate(lanes[:top_lanes], start=1):
        lines.append(
            f"| {index} | `{lane.n64_path}` | `{lane.action}` | {lane.lane_score:.2f} | "
            f"{len(lane.hits)} | {len(lane.top_hits)} | {len(lane.top_n64_counts)} | "
            f"{len(lane.promotion_ready_hits)} | {len(lane.name_conflict_hits)} | "
            f"{len(lane.matching_seed_hits)} | {len(lane.fanout_hits)} | {len(lane.reviewed_defer_hits)} | "
            f"{', '.join('`' + hit.oot3d_entry + '`' for hit in lane.hits[:5])} |"
        )

    lines.extend(["", "## Lane Details", ""])
    for index, lane in enumerate(lanes[:top_lanes], start=1):
        lines.extend(
            [
                f"### {index}. `{lane.n64_path}`",
                "",
                f"- Action: `{lane.action}`",
                f"- Lane score: `{lane.lane_score:.2f}`",
                f"- OOT3D functions: `{len(lane.hits)}`",
                f"- Unique top N64 functions: `{len(lane.top_n64_counts)}`",
                f"- Promotion-ready: `{len(lane.promotion_ready_hits)}`",
                f"- Name-conflict review: `{len(lane.name_conflict_hits)}`",
                f"- Matching seeds: `{len(lane.matching_seed_hits)}`",
                f"- Fan-out review: `{len(lane.fanout_hits)}`",
                f"- Reviewed defer: `{len(lane.reviewed_defer_hits)}`",
                "",
                "| OOT3D | Lines | N64 function | Rank | Score | Gap | Confidence | Suggested name | Flags | Evidence |",
                "| --- | ---: | --- | ---: | ---: | ---: | --- | --- | --- | --- |",
            ]
        )
        fanout_names = lane.fanout_n64_names
        for hit in lane.hits[:12]:
            flags = []
            if hit.is_top and hit.n64_name in fanout_names:
                flags.append("fanout-review")
            if hit in lane.promotion_ready_hits:
                flags.append("promote")
            if hit in lane.name_conflict_hits:
                flags.append("name-conflict-review")
            if hit in lane.matching_seed_hits:
                flags.append("match-seed")
            if hit.reviewed_defer:
                flags.append("reviewed-defer")
            lines.append(
                f"| `{hit.oot3d_entry}` `{hit.oot3d_name}` | {hit.oot3d_lines} | "
                f"`{hit.n64_name}` ({hit.n64_lines}) | {hit.candidate_rank} | {hit.score:.2f} | "
                f"{hit.score_gap:.2f} | `{hit.confidence}` | `{suggested_oot3d_name(hit.n64_name)}` | "
                f"{', '.join(flags) if flags else '-'} | {evidence_summary(hit)} |"
            )
        lines.append("")

    promotion_hits = [hit for lane in lanes for hit in lane.promotion_ready_hits]
    seed_hits = [hit for lane in lanes for hit in lane.matching_seed_hits]
    lines.extend(
        [
            "## Batch Promotion Queue",
            "",
            "| OOT3D | N64 source | N64 function | Confidence | Suggested name |",
            "| --- | --- | --- | --- | --- |",
        ]
    )
    for hit in sorted(promotion_hits, key=lambda item: (-item.score, item.oot3d_entry))[:40]:
        lines.append(
            f"| `{hit.oot3d_entry}` `{hit.oot3d_name}` | `{hit.n64_path}` | `{hit.n64_name}` | "
            f"`{hit.confidence}` | `{suggested_oot3d_name(hit.n64_name)}` |"
        )

    lines.extend(
        [
            "",
            "## Matching Seed Queue",
            "",
            "| OOT3D | OOT3D lines | N64 source | N64 function | N64 lines | Score |",
            "| --- | ---: | --- | --- | ---: | ---: |",
        ]
    )
    for hit in sorted(seed_hits, key=lambda item: (item.oot3d_lines, -item.score, item.oot3d_entry))[:40]:
        lines.append(
            f"| `{hit.oot3d_entry}` `{hit.oot3d_name}` | {hit.oot3d_lines} | `{hit.n64_path}` | "
            f"`{hit.n64_name}` | {hit.n64_lines} | {hit.score:.2f} |"
        )

    fanout_hits = [hit for lane in lanes for hit in lane.fanout_hits]
    name_conflict_hits = [hit for lane in lanes for hit in lane.name_conflict_hits]
    reviewed_defer_by_entry: dict[str, CandidateHit] = {}
    for hit in (hit for lane in lanes for hit in lane.reviewed_defer_hits):
        current = reviewed_defer_by_entry.get(hit.oot3d_entry)
        if current is None or hit.score > current.score:
            reviewed_defer_by_entry[hit.oot3d_entry] = hit
    reviewed_defer_hits = list(reviewed_defer_by_entry.values())
    lines.extend(
        [
            "",
            "## Name Conflict Review Queue",
            "",
            "These rows are strong enough for batch review, but multiple OOT3D functions currently map to the same proposed N64-derived name. Resolve the split/helper identity before direct promotion.",
            "",
            "| OOT3D | N64 source | N64 function | Score | Suggested name | Suggested use |",
            "| --- | --- | --- | ---: | --- | --- |",
        ]
    )
    for hit in sorted(name_conflict_hits, key=lambda item: (-item.score, item.oot3d_entry))[:40]:
        lines.append(
            f"| `{hit.oot3d_entry}` `{hit.oot3d_name}` | `{hit.n64_path}` | `{hit.n64_name}` | "
            f"{hit.score:.2f} | `{suggested_oot3d_name(hit.n64_name)}` | review as a file lane; promote only after assigning a unique semantic role |"
        )

    lines.extend(
        [
            "",
            "## Reviewed Deferred Queue",
            "",
            "These rows were already reviewed and explicitly deferred in `analysis/reviewed_promotions/*.csv`; keep them in lane context, but do not auto-promote them without new evidence.",
            "",
            "| OOT3D | N64 source | N64 function | Score | Suggested name | Review note |",
            "| --- | --- | --- | ---: | --- | --- |",
        ]
    )
    for hit in sorted(reviewed_defer_hits, key=lambda item: (-item.score, item.oot3d_entry))[:40]:
        note = hit.review_note.replace("|", "\\|")
        lines.append(
            f"| `{hit.oot3d_entry}` `{hit.oot3d_name}` | `{hit.n64_path}` | `{hit.n64_name}` | "
            f"{hit.score:.2f} | `{suggested_oot3d_name(hit.n64_name)}` | {note} |"
        )

    lines.extend(
        [
            "",
            "## Fan-Out Review Queue",
            "",
            "These rows identify strong subsystem/source-file correlations, but one N64 function maps to too many OOT3D functions for safe direct promotion.",
            "",
            "| OOT3D | N64 source | N64 function | Score | Suggested use |",
            "| --- | --- | --- | ---: | --- |",
        ]
    )
    for hit in sorted(fanout_hits, key=lambda item: (-item.score, item.oot3d_entry))[:40]:
        lines.append(
            f"| `{hit.oot3d_entry}` `{hit.oot3d_name}` | `{hit.n64_path}` | `{hit.n64_name}` | "
            f"{hit.score:.2f} | port shared structs/macros first, then split/rename manually |"
        )

    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def n64_root_from_rank_json(rank_json: Path) -> Path:
    data = json.loads(rank_json.read_text(encoding="utf-8"))
    raw = Path(data.get("n64_root", "external/oot"))
    if raw.is_absolute():
        return raw
    direct = ROOT / raw
    if direct.exists():
        return direct
    return ROOT.parent / raw


def packet_slug(index: int, n64_path: str) -> str:
    slug = re.sub(r"[^A-Za-z0-9]+", "_", n64_path).strip("_").lower()
    if len(slug) > 72:
        slug = slug[-72:]
    return f"{index:02d}_{slug}"


def copy_packet_text(src: Path, dst: Path, name_overlay: dict[str, str]) -> None:
    text = src.read_text(encoding="utf-8", errors="replace")
    text = overlay_identifier_text(text, name_overlay)
    lines = text.splitlines()
    cleaned = [line.rstrip() for line in lines]
    while cleaned and not cleaned[-1]:
        cleaned.pop()
    dst.write_text("\n".join(cleaned) + "\n", encoding="utf-8")


def emit_batch_packets(
    out_dir: Path,
    lanes: list[FileLane],
    rank_jsons: list[Path],
    count: int,
    max_entries: int,
    copy_n64_source: bool,
    name_overlay: dict[str, str],
) -> None:
    if count <= 0:
        return

    n64_root = n64_root_from_rank_json(rank_jsons[0])
    out_dir.mkdir(parents=True, exist_ok=True)
    for index, lane in enumerate(lanes[:count], start=1):
        packet_dir = out_dir / packet_slug(index, lane.n64_path)
        packet_dir.mkdir(parents=True, exist_ok=True)
        n64_source = n64_root / lane.n64_path
        n64_source_copy = packet_dir / "n64_source.c"
        if copy_n64_source and n64_source.is_file():
            shutil.copy2(n64_source, packet_dir / "n64_source.c")
        elif n64_source_copy.exists():
            n64_source_copy.unlink()

        selected = lane.hits[:max_entries]
        for hit in selected:
            src = ROOT / hit.oot3d_path
            if src.is_file():
                dst = packet_dir / f"oot3d_{hit.oot3d_entry}_{hit.oot3d_name}.c"
                copy_packet_text(src, dst, name_overlay)

        lines = [
            f"# Batch Port Packet `{lane.n64_path}`",
            "",
            f"- Action: `{lane.action}`",
            f"- Lane score: `{lane.lane_score:.2f}`",
            f"- Unique top N64 functions: `{len(lane.top_n64_counts)}`",
            f"- Fan-out review rows: `{len(lane.fanout_hits)}`",
            f"- Name-conflict review rows: `{len(lane.name_conflict_hits)}`",
            f"- Reviewed defer rows: `{len(lane.reviewed_defer_hits)}`",
            f"- Manual symbol overlay applied: `{bool(name_overlay)}`",
            f"- N64 source copy: `n64_source.c`" if copy_n64_source and n64_source.is_file() else (
                f"- N64 source path: `{rel(n64_source, ROOT.parent)}`" if n64_source.is_file() else "- N64 source path: missing"
            ),
            "",
            "## Functions",
            "",
            "| OOT3D | OOT3D file | N64 function | Rank | Score | Confidence | Suggested name | Evidence | Flags |",
            "| --- | --- | --- | ---: | ---: | --- | --- | --- | --- |",
        ]
        fanout_names = lane.fanout_n64_names
        for hit in selected:
            flags = []
            if hit.is_top and hit.n64_name in fanout_names:
                flags.append("fanout-review")
            if hit in lane.promotion_ready_hits:
                flags.append("promote")
            if hit in lane.name_conflict_hits:
                flags.append("name-conflict-review")
            if hit in lane.matching_seed_hits:
                flags.append("match-seed")
            if hit.reviewed_defer:
                flags.append("reviewed-defer")
            lines.append(
                f"| `{hit.oot3d_entry}` `{hit.oot3d_name}` | "
                f"`oot3d_{hit.oot3d_entry}_{hit.oot3d_name}.c` | `{hit.n64_name}` | "
                f"{hit.candidate_rank} | {hit.score:.2f} | `{hit.confidence}` | "
                f"`{suggested_oot3d_name(hit.n64_name)}` | {evidence_summary(hit) or '-'} | "
                f"{', '.join(flags) if flags else '-'} |"
            )
        lines.extend(
            [
                "",
                "## Batch Workflow",
                "",
                "1. Review the N64 source once for shared structs, macros, and state fields.",
                "2. Use rows flagged `fanout-review` for subsystem reconstruction, not direct rename.",
                "3. Use rows flagged `name-conflict-review` to resolve split/helper identity and assign unique reviewed names before promotion.",
                "4. Promote only rows flagged `promote` after confirming control-flow/data meaning; rows flagged `reviewed-defer` already failed this pass and need new evidence.",
                "5. Port rows flagged `match-seed` into one maintained source file and add them to the runtime build only when they compile.",
                "6. Re-run `scripts\\build-runtime-objects.ps1` and keep exact matches only.",
                "",
            ]
        )
        (packet_dir / "README.md").write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rank-json", action="append", type=Path, dest="rank_jsons")
    parser.add_argument("--include-named", action="store_true")
    parser.add_argument("--top-lanes", type=int, default=20)
    parser.add_argument("--emit-batch-packets", type=int, default=0)
    parser.add_argument("--max-packet-entries", type=int, default=12)
    parser.add_argument("--copy-n64-source", action="store_true")
    parser.add_argument("--no-symbol-overlay", action="store_true")
    parser.add_argument("--packet-dir", type=Path, default=ANALYSIS / "port_batches")
    parser.add_argument("--out-json", type=Path, default=ANALYSIS / "n64_batch_porting_plan.json")
    parser.add_argument("--out-md", type=Path, default=ANALYSIS / "n64_batch_porting_plan.md")
    parser.add_argument("--out-csv", type=Path, default=ANALYSIS / "n64_batch_porting_plan.csv")
    args = parser.parse_args()

    rank_jsons = args.rank_jsons or [path for path in DEFAULT_RANK_JSONS if path.is_file()]
    if not rank_jsons:
        raise FileNotFoundError("no rank JSONs supplied or found")

    manual_entries = load_manual_entries(MANUAL_SYMBOLS)
    reviewed_defer_entries = load_reviewed_defer_entries(REVIEWED_PROMOTIONS_DIR)
    function_pointer_refs = load_function_pointer_refs(FUNCTION_POINTER_REFS)
    hits: list[CandidateHit] = []
    for path in rank_jsons:
        hits.extend(load_rank_hits(path, manual_entries, reviewed_defer_entries, function_pointer_refs, args.include_named))
    hits = merge_hits_by_entry_source(hits)
    lanes = build_lanes(hits)
    name_overlay = {} if args.no_symbol_overlay else identifier_overlay(load_manual_symbols(MANUAL_SYMBOLS))

    write_json(args.out_json, lanes, rank_jsons)
    write_markdown(args.out_md, lanes, rank_jsons, args.top_lanes)
    write_csv(args.out_csv, lanes)
    emit_batch_packets(
        args.packet_dir,
        lanes,
        rank_jsons,
        args.emit_batch_packets,
        args.max_packet_entries,
        args.copy_n64_source,
        name_overlay,
    )

    print(
        json.dumps(
            {
                "rank_jsons": [str(path) for path in rank_jsons],
                "lanes": len(lanes),
                "report": str(args.out_md),
                "batch_packets": args.emit_batch_packets,
            },
            indent=2,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
