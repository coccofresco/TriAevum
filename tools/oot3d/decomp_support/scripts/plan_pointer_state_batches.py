#!/usr/bin/env python3
"""Plan batchable state/action-table work from resolved function pointers.

The N64 ranker is useful for finding similar bodies, but it stalls on large
fan-outs where one N64 function maps to many OOT3D functions. Function pointer
references give a second axis: source functions that install many callback or
state targets are natural batch boundaries. This report turns those pointer
edges into packet-sized work units that can be reviewed and ported together.
"""

from __future__ import annotations

import argparse
import csv
import json
import re
import shutil
from collections import Counter, defaultdict, deque
from dataclasses import dataclass, field
from pathlib import Path
from statistics import mean


ROOT = Path(__file__).resolve().parents[1]
ANALYSIS = ROOT / "analysis"
DECOMPILED = ROOT / "ghidra_export" / "decompiled"
FUNCTIONS = ROOT / "ghidra_export" / "functions.csv"
MANUAL_SYMBOLS = ROOT / "symbols" / "manual_symbols.csv"
FUNCTION_POINTER_REFS = ANALYSIS / "function_pointer_refs.json"
N64_BATCH_PLAN = ANALYSIS / "n64_batch_porting_plan.json"
DEFAULT_PACKET_DIR = ANALYSIS / "pointer_state_packets"

FILE_ENTRY_RE = re.compile(r"^[0-9]+_(?P<entry>[0-9a-fA-F]{8})_")


@dataclass
class N64Hit:
    lane_rank: int
    n64_path: str
    n64_name: str
    score: float
    confidence: str
    flags: list[str] = field(default_factory=list)


@dataclass
class PointerRow:
    source_entry: str
    source_name: str
    source_file: str
    line: int
    ref_name: str
    literal_address: str
    pointer_value: str
    target_entry: str
    target_name: str
    context: str


@dataclass
class SourceBatch:
    source_entry: str
    source_name: str
    source_file: str
    rows: list[PointerRow]
    target_sources: Counter[str]
    manual_names: dict[str, str]
    n64_hits: dict[str, list[N64Hit]]

    @property
    def targets(self) -> list[str]:
        return sorted({row.target_entry for row in self.rows}, key=addr_key)

    @property
    def entries(self) -> list[str]:
        return [self.source_entry] + [entry for entry in self.targets if entry != self.source_entry]

    @property
    def ref_count(self) -> int:
        return len(self.rows)

    @property
    def edge_count(self) -> int:
        return len(self.targets)

    @property
    def top_bucket(self) -> str:
        bucket, _ = self.bucket_stats
        return bucket

    @property
    def locality_ratio(self) -> float:
        _, ratio = self.bucket_stats
        return ratio

    @property
    def bucket_stats(self) -> tuple[str, float]:
        if not self.targets:
            return "", 0.0
        counts: Counter[int] = Counter(addr_int(entry) >> 16 for entry in self.targets)
        bucket, count = counts.most_common(1)[0]
        return f"0x{bucket:02x}xxxx", count / len(self.targets)

    @property
    def named_count(self) -> int:
        return sum(1 for entry in self.entries if is_named(entry, entry_name(entry, self.rows), self.manual_names))

    @property
    def unnamed_count(self) -> int:
        return len(self.entries) - self.named_count

    @property
    def n64_lane_paths(self) -> list[str]:
        paths = {
            hit.n64_path
            for entry in self.entries
            for hit in self.n64_hits.get(entry, [])
        }
        return sorted(paths)

    @property
    def n64_best_score(self) -> float:
        scores = [hit.score for entry in self.entries for hit in self.n64_hits.get(entry, [])]
        return max(scores) if scores else 0.0

    @property
    def fanout_n64_hits(self) -> int:
        return sum(
            1
            for entry in self.entries
            for hit in self.n64_hits.get(entry, [])
            if "fanout-review" in hit.flags
        )

    @property
    def reviewed_defer_hits(self) -> int:
        return sum(
            1
            for entry in self.entries
            for hit in self.n64_hits.get(entry, [])
            if "reviewed-defer" in hit.flags
        )

    @property
    def average_target_indegree(self) -> float:
        if not self.targets:
            return 0.0
        return mean(self.target_sources[target] for target in self.targets)

    @property
    def address_span(self) -> int:
        values = [addr_int(entry) for entry in self.targets]
        if not values:
            return 0
        return max(values) - min(values)

    @property
    def action(self) -> str:
        if self.reviewed_defer_hits and self.edge_count <= 3:
            return "keep-deferred-context"
        if self.named_count and self.unnamed_count >= 2 and self.locality_ratio >= 0.65:
            return "propagate-owner-names"
        if self.n64_lane_paths and self.locality_ratio >= 0.65:
            return "state-table-port-batch"
        if self.edge_count >= 4 and self.locality_ratio >= 0.75:
            return "state-table-discovery"
        if self.n64_lane_paths:
            return "n64-crosscheck-batch"
        return "pointer-context-review"

    @property
    def priority(self) -> float:
        locality_bonus = 90.0 * self.locality_ratio
        target_bonus = 12.0 * min(self.edge_count, 12)
        ref_bonus = 1.5 * min(self.ref_count, 24)
        if self.locality_ratio >= 0.75 and self.edge_count >= 3:
            shape_bonus = 170.0
        elif self.locality_ratio >= 0.65 and self.edge_count >= 3:
            shape_bonus = 105.0
        elif self.locality_ratio >= 0.50 and self.edge_count >= 5:
            shape_bonus = 35.0
        else:
            shape_bonus = 0.0
        lane_count = min(len(self.n64_lane_paths), 3)
        if self.n64_lane_paths and self.locality_ratio >= 0.65:
            n64_bonus = 18.0 * lane_count + min(55.0, self.n64_best_score / 6.0)
        elif self.n64_lane_paths:
            n64_bonus = 8.0 * lane_count + min(35.0, self.n64_best_score / 10.0)
        else:
            n64_bonus = 0.0
        named_bonus = 12.0 * self.named_count
        fanout_penalty = 8.0 * self.fanout_n64_hits
        shared_target_penalty = 4.0 * max(0.0, self.average_target_indegree - 4.0)
        span_penalty = min(45.0, (self.address_span / 0x10000) * 2.5)
        return (
            locality_bonus
            + target_bonus
            + ref_bonus
            + shape_bonus
            + n64_bonus
            + named_bonus
            - fanout_penalty
            - shared_target_penalty
            - span_penalty
        )


@dataclass
class Component:
    component_id: int
    entries: list[str]
    sources: list[str]
    targets: list[str]
    refs: int
    manual_names: dict[str, str]
    n64_hits: dict[str, list[N64Hit]]

    @property
    def named_count(self) -> int:
        return sum(1 for entry in self.entries if is_named(entry, "", self.manual_names))

    @property
    def n64_lane_paths(self) -> list[str]:
        paths = {
            hit.n64_path
            for entry in self.entries
            for hit in self.n64_hits.get(entry, [])
        }
        return sorted(paths)

    @property
    def locality_ratio(self) -> float:
        if not self.entries:
            return 0.0
        counts: Counter[int] = Counter(addr_int(entry) >> 16 for entry in self.entries)
        _, count = counts.most_common(1)[0]
        return count / len(self.entries)

    @property
    def priority(self) -> float:
        return (
            10.0 * len(self.entries)
            + 2.0 * self.refs
            + 50.0 * self.locality_ratio
            + 18.0 * len(self.n64_lane_paths)
            + 10.0 * self.named_count
        )


def normalize_entry(entry: str) -> str:
    return entry.strip().lower().removeprefix("0x").zfill(8)


def addr_int(entry: str) -> int:
    return int(normalize_entry(entry), 16)


def addr_key(entry: str) -> int:
    return addr_int(entry)


def rel(path: Path, root: Path = ROOT) -> str:
    try:
        return str(path.relative_to(root)).replace("\\", "/")
    except ValueError:
        try:
            return str(path.relative_to(ROOT.parent)).replace("\\", "/")
        except ValueError:
            return str(path).replace("\\", "/")


def safe_name(name: str) -> str:
    cleaned = re.sub(r"[^A-Za-z0-9_]+", "_", name).strip("_")
    return cleaned[:96] or "unnamed"


def entry_name(entry: str, rows: list[PointerRow]) -> str:
    for row in rows:
        if row.source_entry == entry:
            return row.source_name
        if row.target_entry == entry:
            return row.target_name
    return ""


def display_name(entry: str, fallback: str, manual_names: dict[str, str]) -> str:
    return manual_names.get(entry, fallback or f"FUN_{entry}")


def is_named(entry: str, fallback: str, manual_names: dict[str, str]) -> bool:
    if entry in manual_names:
        return True
    return bool(fallback and not fallback.startswith("FUN_"))


def load_manual_names(path: Path) -> dict[str, str]:
    if not path.is_file():
        return {}
    with path.open(newline="", encoding="utf-8") as handle:
        return {
            normalize_entry(row.get("entry", "")): row.get("new_name", "").strip()
            for row in csv.DictReader(handle)
            if row.get("entry") and row.get("new_name")
        }


def load_decompiled_paths(root: Path) -> dict[str, Path]:
    paths: dict[str, Path] = {}
    for path in sorted(root.glob("*.c")):
        match = FILE_ENTRY_RE.match(path.name)
        if match:
            paths[normalize_entry(match.group("entry"))] = path
    return paths


def load_function_names(path: Path) -> dict[str, str]:
    if not path.is_file():
        return {}
    with path.open(newline="", encoding="utf-8") as handle:
        return {
            normalize_entry(row.get("entry", "")): row.get("name", "").strip()
            for row in csv.DictReader(handle)
            if row.get("entry")
        }


def load_pointer_rows(path: Path) -> list[PointerRow]:
    data = json.loads(path.read_text(encoding="utf-8"))
    rows = []
    for row in data.get("rows", []):
        source_entry = normalize_entry(str(row.get("source_entry", "")))
        target_entry = normalize_entry(str(row.get("target_entry", "")))
        if not source_entry or not target_entry:
            continue
        rows.append(
            PointerRow(
                source_entry=source_entry,
                source_name=str(row.get("source_name", "")),
                source_file=str(row.get("source_file", "")),
                line=int(row.get("line", 0) or 0),
                ref_name=str(row.get("ref_name", "")),
                literal_address=normalize_entry(str(row.get("literal_address", ""))),
                pointer_value=normalize_entry(str(row.get("pointer_value", ""))),
                target_entry=target_entry,
                target_name=str(row.get("target_name", "")),
                context=str(row.get("context", "")),
            )
        )
    return rows


def load_n64_hits(path: Path) -> dict[str, list[N64Hit]]:
    if not path.is_file():
        return {}
    data = json.loads(path.read_text(encoding="utf-8"))
    result: dict[str, list[N64Hit]] = defaultdict(list)
    for lane in data.get("lanes", []):
        lane_rank = int(lane.get("rank", 0) or 0)
        for hit in lane.get("hits", []):
            entry = normalize_entry(str(hit.get("oot3d_entry", "")))
            if not entry:
                continue
            flags = []
            for flag_name in (
                "promotion_ready",
                "matching_seed",
                "fanout_review",
                "name_conflict",
                "reviewed_defer",
            ):
                if hit.get(flag_name):
                    flags.append(flag_name.replace("_", "-"))
            result[entry].append(
                N64Hit(
                    lane_rank=lane_rank,
                    n64_path=str(hit.get("n64_path", lane.get("n64_path", ""))),
                    n64_name=str(hit.get("n64_name", "")),
                    score=float(hit.get("score", 0.0) or 0.0),
                    confidence=str(hit.get("confidence", "")),
                    flags=flags,
                )
            )
    for hits in result.values():
        hits.sort(key=lambda item: (item.lane_rank, -item.score, item.n64_path, item.n64_name))
        del hits[4:]
    return dict(result)


def build_source_batches(
    rows: list[PointerRow],
    manual_names: dict[str, str],
    n64_hits: dict[str, list[N64Hit]],
    min_targets: int,
    min_refs: int,
) -> list[SourceBatch]:
    by_source: dict[str, list[PointerRow]] = defaultdict(list)
    target_sources: dict[str, set[str]] = defaultdict(set)
    for row in rows:
        by_source[row.source_entry].append(row)
        target_sources[row.target_entry].add(row.source_entry)

    target_source_counts = Counter({target: len(sources) for target, sources in target_sources.items()})
    batches = []
    for source_entry, source_rows in by_source.items():
        targets = {row.target_entry for row in source_rows}
        if len(targets) < min_targets and len(source_rows) < min_refs:
            continue
        first = source_rows[0]
        batch = SourceBatch(
            source_entry=source_entry,
            source_name=first.source_name,
            source_file=first.source_file,
            rows=source_rows,
            target_sources=target_source_counts,
            manual_names=manual_names,
            n64_hits=n64_hits,
        )
        batches.append(batch)

    batches.sort(key=lambda item: (-item.priority, -item.locality_ratio, -item.edge_count, item.source_entry))
    return batches


def build_components(
    batches: list[SourceBatch],
    rows: list[PointerRow],
    manual_names: dict[str, str],
    n64_hits: dict[str, list[N64Hit]],
    min_entries: int,
) -> list[Component]:
    eligible_sources = {
        batch.source_entry
        for batch in batches
        if batch.edge_count >= 3 and (batch.locality_ratio >= 0.60 or batch.n64_lane_paths)
    }
    adjacency: dict[str, set[str]] = defaultdict(set)
    ref_counts: Counter[tuple[str, str]] = Counter()
    for row in rows:
        if row.source_entry not in eligible_sources:
            continue
        adjacency[row.source_entry].add(row.target_entry)
        adjacency[row.target_entry].add(row.source_entry)
        ref_counts[(row.source_entry, row.target_entry)] += 1

    components = []
    seen: set[str] = set()
    component_id = 1
    for start in sorted(adjacency, key=addr_key):
        if start in seen:
            continue
        queue: deque[str] = deque([start])
        seen.add(start)
        entries = []
        while queue:
            current = queue.popleft()
            entries.append(current)
            for neighbor in sorted(adjacency[current], key=addr_key):
                if neighbor in seen:
                    continue
                seen.add(neighbor)
                queue.append(neighbor)

        if len(entries) < min_entries:
            continue
        sources = sorted([entry for entry in entries if entry in eligible_sources], key=addr_key)
        targets = sorted([entry for entry in entries if entry not in eligible_sources], key=addr_key)
        refs = sum(
            count
            for (source, target), count in ref_counts.items()
            if source in entries and target in entries
        )
        components.append(
            Component(
                component_id=component_id,
                entries=sorted(entries, key=addr_key),
                sources=sources,
                targets=targets,
                refs=refs,
                manual_names=manual_names,
                n64_hits=n64_hits,
            )
        )
        component_id += 1

    components.sort(key=lambda item: (-item.priority, -len(item.entries), item.entries[0]))
    return components


def n64_summary(entries: list[str], n64_hits: dict[str, list[N64Hit]], limit: int = 3) -> str:
    counts: Counter[str] = Counter()
    best: dict[str, N64Hit] = {}
    for entry in entries:
        for hit in n64_hits.get(entry, []):
            counts[hit.n64_path] += 1
            current = best.get(hit.n64_path)
            if current is None or hit.score > current.score:
                best[hit.n64_path] = hit
    parts = []
    for path, count in counts.most_common(limit):
        hit = best[path]
        parts.append(f"`{path}` ({count}, `{hit.n64_name}`, {hit.score:.1f})")
    if len(counts) > limit:
        parts.append(f"+{len(counts) - limit}")
    return ", ".join(parts)


def target_table_rows(batch: SourceBatch, function_names: dict[str, str]) -> list[dict[str, str]]:
    refs_by_target: Counter[str] = Counter(row.target_entry for row in batch.rows)
    first_row: dict[str, PointerRow] = {}
    for row in batch.rows:
        first_row.setdefault(row.target_entry, row)

    rows = []
    for target in batch.targets:
        pointer_row = first_row[target]
        n64 = n64_summary([target], batch.n64_hits, limit=1) or "-"
        fallback = function_names.get(target, pointer_row.target_name)
        rows.append(
            {
                "entry": target,
                "name": display_name(target, fallback, batch.manual_names),
                "refs": str(refs_by_target[target]),
                "literal": f"0x{pointer_row.literal_address}",
                "ref_name": pointer_row.ref_name,
                "n64": n64,
                "context": pointer_row.context.replace("|", "\\|"),
            }
        )
    return rows


def source_to_json(batch: SourceBatch, function_names: dict[str, str]) -> dict[str, object]:
    return {
        "source_entry": batch.source_entry,
        "source_name": display_name(batch.source_entry, batch.source_name, batch.manual_names),
        "source_file": batch.source_file,
        "priority": round(batch.priority, 2),
        "action": batch.action,
        "refs": batch.ref_count,
        "targets": batch.edge_count,
        "top_bucket": batch.top_bucket,
        "locality_ratio": round(batch.locality_ratio, 4),
        "named_entries": batch.named_count,
        "unnamed_entries": batch.unnamed_count,
        "average_target_indegree": round(batch.average_target_indegree, 2),
        "n64_lanes": batch.n64_lane_paths,
        "fanout_n64_hits": batch.fanout_n64_hits,
        "reviewed_defer_hits": batch.reviewed_defer_hits,
        "target_rows": target_table_rows(batch, function_names),
    }


def component_to_json(component: Component, function_names: dict[str, str]) -> dict[str, object]:
    return {
        "component_id": component.component_id,
        "priority": round(component.priority, 2),
        "entries": component.entries,
        "sources": component.sources,
        "targets": component.targets,
        "refs": component.refs,
        "locality_ratio": round(component.locality_ratio, 4),
        "named_entries": component.named_count,
        "n64_lanes": component.n64_lane_paths,
        "top_entries": [
            {
                "entry": entry,
                "name": display_name(entry, function_names.get(entry, ""), component.manual_names),
            }
            for entry in component.entries[:12]
        ],
    }


def write_json(
    path: Path,
    rows: list[PointerRow],
    batches: list[SourceBatch],
    components: list[Component],
    function_names: dict[str, str],
    args: argparse.Namespace,
) -> None:
    data = {
        "inputs": {
            "function_pointer_refs": rel(args.pointer_refs),
            "n64_batch_plan": rel(args.n64_plan),
            "manual_symbols": rel(args.manual_symbols),
        },
        "thresholds": {
            "min_targets": args.min_targets,
            "min_refs": args.min_refs,
            "min_component_entries": args.min_component_entries,
        },
        "resolved_pointer_refs": len(rows),
        "source_batch_count": len(batches),
        "component_count": len(components),
        "source_batches": [source_to_json(batch, function_names) for batch in batches],
        "components": [component_to_json(component, function_names) for component in components],
    }
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def write_csv(path: Path, batches: list[SourceBatch]) -> None:
    fields = [
        "rank",
        "source_entry",
        "source_name",
        "action",
        "priority",
        "refs",
        "targets",
        "top_bucket",
        "locality_ratio",
        "named_entries",
        "unnamed_entries",
        "n64_lanes",
        "fanout_n64_hits",
        "reviewed_defer_hits",
        "source_file",
    ]
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        for rank, batch in enumerate(batches, start=1):
            writer.writerow(
                {
                    "rank": rank,
                    "source_entry": batch.source_entry,
                    "source_name": display_name(batch.source_entry, batch.source_name, batch.manual_names),
                    "action": batch.action,
                    "priority": f"{batch.priority:.2f}",
                    "refs": batch.ref_count,
                    "targets": batch.edge_count,
                    "top_bucket": batch.top_bucket,
                    "locality_ratio": f"{batch.locality_ratio:.2f}",
                    "named_entries": batch.named_count,
                    "unnamed_entries": batch.unnamed_count,
                    "n64_lanes": "; ".join(batch.n64_lane_paths),
                    "fanout_n64_hits": batch.fanout_n64_hits,
                    "reviewed_defer_hits": batch.reviewed_defer_hits,
                    "source_file": batch.source_file,
                }
            )


def write_markdown(
    path: Path,
    rows: list[PointerRow],
    batches: list[SourceBatch],
    components: list[Component],
    function_names: dict[str, str],
    args: argparse.Namespace,
) -> None:
    lines = [
        "# Pointer State Batch Plan",
        "",
        "Generated from `scripts/plan_pointer_state_batches.py`.",
        "",
        f"- Pointer refs: `{rel(args.pointer_refs)}`",
        f"- N64 batch plan: `{rel(args.n64_plan)}`",
        f"- Resolved pointer refs: `{len(rows)}`",
        f"- Source batches: `{len(batches)}`",
        f"- Connected components: `{len(components)}`",
        "",
        "## Next Batch Queue",
        "",
        "These are source functions that install several function pointers. Treat each row as a packet: review the source once, then port/name the targets as a group.",
        "",
        "| Rank | Source | Action | Priority | Refs | Targets | Locality | Named/Total | N64 lanes |",
        "| ---: | --- | --- | ---: | ---: | ---: | --- | ---: | --- |",
    ]
    for rank, batch in enumerate(batches[: args.top_sources], start=1):
        source_name = display_name(batch.source_entry, batch.source_name, batch.manual_names)
        lanes = n64_summary(batch.entries, batch.n64_hits, limit=2) or "-"
        lines.append(
            f"| {rank} | `{batch.source_entry}` `{source_name}` | `{batch.action}` | "
            f"{batch.priority:.2f} | {batch.ref_count} | {batch.edge_count} | "
            f"{batch.top_bucket} {batch.locality_ratio:.2f} | {batch.named_count}/{len(batch.entries)} | {lanes} |"
        )

    lines.extend(
        [
            "",
            "## N64 Crossovers",
            "",
            "Rows here already intersect the N64 lane plan, so they are the best candidates for porting source-file semantics rather than doing isolated Ghidra review.",
            "",
            "| Source | Action | Targets | N64 lanes | Top targets |",
            "| --- | --- | ---: | --- | --- |",
        ]
    )
    crossover_rows = [batch for batch in batches if batch.n64_lane_paths]
    for batch in crossover_rows[: args.top_crossovers]:
        target_text = ", ".join(
            f"`{row['entry']}` `{row['name']}`"
            for row in target_table_rows(batch, function_names)[:4]
        )
        if batch.edge_count > 4:
            target_text += f", +{batch.edge_count - 4}"
        lines.append(
            f"| `{batch.source_entry}` `{display_name(batch.source_entry, batch.source_name, batch.manual_names)}` | "
            f"`{batch.action}` | {batch.edge_count} | {n64_summary(batch.entries, batch.n64_hits, limit=3) or '-'} | "
            f"{target_text or '-'} |"
        )

    lines.extend(
        [
            "",
            "## Source Details",
            "",
        ]
    )
    for rank, batch in enumerate(batches[: args.detail_sources], start=1):
        source_name = display_name(batch.source_entry, batch.source_name, batch.manual_names)
        lines.extend(
            [
                f"### {rank}. `{batch.source_entry}` `{source_name}`",
                "",
                f"- Action: `{batch.action}`",
                f"- Source file: `{batch.source_file}`",
                f"- Refs/targets: `{batch.ref_count}` / `{batch.edge_count}`",
                f"- Locality: `{batch.top_bucket}` `{batch.locality_ratio:.2f}`",
                f"- N64 lane overlap: {n64_summary(batch.entries, batch.n64_hits, limit=4) or '-'}",
                "",
                "| Target | Refs | Literal | N64 hint | First context |",
                "| --- | ---: | --- | --- | --- |",
            ]
        )
        for target in target_table_rows(batch, function_names)[: args.detail_targets]:
            lines.append(
                f"| `{target['entry']}` `{target['name']}` | {target['refs']} | "
                f"`{target['literal']}` `{target['ref_name']}` | {target['n64']} | `{target['context']}` |"
            )
        if batch.edge_count > args.detail_targets:
            lines.append(f"| +{batch.edge_count - args.detail_targets} more | | | | |")
        lines.append("")

    lines.extend(
        [
            "## Connected Components",
            "",
            "Components use only batch-like pointer sources. They are useful when a source installs a target that in turn installs more callbacks.",
            "",
            "| Component | Priority | Entries | Sources | Refs | Locality | Named | N64 lanes | Top entries |",
            "| ---: | ---: | ---: | ---: | ---: | --- | ---: | --- | --- |",
        ]
    )
    for component in components[: args.top_components]:
        top_entries = ", ".join(
            f"`{entry}` `{display_name(entry, function_names.get(entry, ''), component.manual_names)}`"
            for entry in component.entries[:6]
        )
        if len(component.entries) > 6:
            top_entries += f", +{len(component.entries) - 6}"
        lines.append(
            f"| {component.component_id} | {component.priority:.2f} | {len(component.entries)} | "
            f"{len(component.sources)} | {component.refs} | {component.locality_ratio:.2f} | "
            f"{component.named_count} | {n64_summary(component.entries, component.n64_hits, limit=2) or '-'} | "
            f"{top_entries or '-'} |"
        )

    lines.extend(
        [
            "",
            "## Workflow",
            "",
            "1. Start from `analysis/pointer_state_packets/01_*` and keep the whole source-target set together.",
            "2. Use N64 crossovers as subsystem anchors; avoid direct promotion when the row is only a broad fan-out.",
            "3. Promote names in CSV batches only after the packet confirms the source field meaning and target roles.",
            "4. Regenerate this report after every promotion batch; named coverage should increase and unresolved packet size should drop.",
            "",
        ]
    )
    path.write_text("\n".join(lines), encoding="utf-8")


def packet_slug(index: int, batch: SourceBatch) -> str:
    name = display_name(batch.source_entry, batch.source_name, batch.manual_names)
    return f"{index:02d}_{batch.source_entry}_{safe_name(name).lower()}"


def copy_decompiled(path_map: dict[str, Path], entry: str, name: str, out_dir: Path, prefix: str) -> str:
    source = path_map.get(entry)
    if source is None or not source.is_file():
        return ""
    destination = out_dir / f"{prefix}_{entry}_{safe_name(name)}.c"
    text = source.read_text(encoding="utf-8", errors="replace")
    lines = [line.rstrip() for line in text.splitlines()]
    while lines and not lines[-1]:
        lines.pop()
    destination.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return destination.name


def emit_packets(
    packet_dir: Path,
    batches: list[SourceBatch],
    function_names: dict[str, str],
    path_map: dict[str, Path],
    count: int,
    max_targets: int,
) -> None:
    if count <= 0:
        return
    stale_packet_re = re.compile(r"^[0-9]{2}_[0-9a-f]{8}_")
    if packet_dir.exists():
        for child in packet_dir.iterdir():
            if not stale_packet_re.match(child.name):
                continue
            if child.is_dir():
                shutil.rmtree(child)
            else:
                child.unlink()
    packet_dir.mkdir(parents=True, exist_ok=True)
    for index, batch in enumerate(batches[:count], start=1):
        out_dir = packet_dir / packet_slug(index, batch)
        out_dir.mkdir(parents=True, exist_ok=True)
        source_name = display_name(batch.source_entry, batch.source_name, batch.manual_names)
        source_copy = copy_decompiled(path_map, batch.source_entry, source_name, out_dir, "source")

        target_copies: dict[str, str] = {}
        for target in batch.targets[:max_targets]:
            fallback = function_names.get(target, entry_name(target, batch.rows))
            target_name = display_name(target, fallback, batch.manual_names)
            copied = copy_decompiled(path_map, target, target_name, out_dir, "target")
            if copied:
                target_copies[target] = copied

        lines = [
            f"# Pointer State Packet `{batch.source_entry}`",
            "",
            f"- Source: `{batch.source_entry}` `{source_name}`",
            f"- Action: `{batch.action}`",
            f"- Priority: `{batch.priority:.2f}`",
            f"- Refs/targets: `{batch.ref_count}` / `{batch.edge_count}`",
            f"- Locality: `{batch.top_bucket}` `{batch.locality_ratio:.2f}`",
            f"- Named entries: `{batch.named_count}` / `{len(batch.entries)}`",
            f"- N64 lane overlap: {n64_summary(batch.entries, batch.n64_hits, limit=4) or '-'}",
            f"- Source copy: `{source_copy}`" if source_copy else "- Source copy: missing",
            "",
            "## Targets",
            "",
            "| Target | Refs | N64 hint | Decompiled copy | First context |",
            "| --- | ---: | --- | --- | --- |",
        ]
        for target in target_table_rows(batch, function_names)[:max_targets]:
            entry = target["entry"]
            lines.append(
                f"| `{entry}` `{target['name']}` | {target['refs']} | {target['n64']} | "
                f"`{target_copies.get(entry, '') or 'missing'}` | `{target['context']}` |"
            )
        if batch.edge_count > max_targets:
            lines.append(f"| +{batch.edge_count - max_targets} more | | | | |")

        lines.extend(
            [
                "",
                "## Packet Workflow",
                "",
                "1. Read the source copy first and identify which object field receives each callback pointer.",
                "2. Review all targets in one pass and assign role names relative to the source state machine.",
                "3. Cross-check any listed N64 lane before promoting names; fan-out hints are subsystem evidence, not direct matches.",
                "4. Record approved names in a reviewed CSV and run `scripts\\promote_manual_symbols_batch.py --update`.",
                "",
            ]
        )
        (out_dir / "README.md").write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pointer-refs", type=Path, default=FUNCTION_POINTER_REFS)
    parser.add_argument("--n64-plan", type=Path, default=N64_BATCH_PLAN)
    parser.add_argument("--manual-symbols", type=Path, default=MANUAL_SYMBOLS)
    parser.add_argument("--out-json", type=Path, default=ANALYSIS / "pointer_state_batches.json")
    parser.add_argument("--out-md", type=Path, default=ANALYSIS / "pointer_state_batches.md")
    parser.add_argument("--out-csv", type=Path, default=ANALYSIS / "pointer_state_batches.csv")
    parser.add_argument("--packet-dir", type=Path, default=DEFAULT_PACKET_DIR)
    parser.add_argument("--emit-packets", type=int, default=0)
    parser.add_argument("--max-packet-targets", type=int, default=16)
    parser.add_argument("--min-targets", type=int, default=3)
    parser.add_argument("--min-refs", type=int, default=6)
    parser.add_argument("--min-component-entries", type=int, default=4)
    parser.add_argument("--top-sources", type=int, default=40)
    parser.add_argument("--top-crossovers", type=int, default=30)
    parser.add_argument("--detail-sources", type=int, default=8)
    parser.add_argument("--detail-targets", type=int, default=12)
    parser.add_argument("--top-components", type=int, default=30)
    args = parser.parse_args()

    if not args.pointer_refs.is_file():
        raise FileNotFoundError(f"missing pointer refs: {args.pointer_refs}")

    manual_names = load_manual_names(args.manual_symbols)
    function_names = load_function_names(FUNCTIONS)
    path_map = load_decompiled_paths(DECOMPILED)
    pointer_rows = load_pointer_rows(args.pointer_refs)
    n64_hits = load_n64_hits(args.n64_plan)
    batches = build_source_batches(
        pointer_rows,
        manual_names,
        n64_hits,
        args.min_targets,
        args.min_refs,
    )
    components = build_components(
        batches,
        pointer_rows,
        manual_names,
        n64_hits,
        args.min_component_entries,
    )

    write_json(args.out_json, pointer_rows, batches, components, function_names, args)
    write_csv(args.out_csv, batches)
    write_markdown(args.out_md, pointer_rows, batches, components, function_names, args)
    emit_packets(args.packet_dir, batches, function_names, path_map, args.emit_packets, args.max_packet_targets)

    print(
        json.dumps(
            {
                "pointer_refs": len(pointer_rows),
                "source_batches": len(batches),
                "components": len(components),
                "report": rel(args.out_md),
                "packets": args.emit_packets,
            },
            indent=2,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
