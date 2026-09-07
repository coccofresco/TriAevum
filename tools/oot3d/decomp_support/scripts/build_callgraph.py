#!/usr/bin/env python3
"""Build callgraph and module-clustering reports from functions_enriched.json."""

from __future__ import annotations

import json
from collections import defaultdict, deque
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ANALYSIS = ROOT / "analysis"
INPUT = ANALYSIS / "functions_enriched.json"


def load_functions() -> tuple[dict[str, dict[str, object]], dict[str, str]]:
    functions = json.loads(INPUT.read_text(encoding="utf-8"))
    by_entry = {str(item["entry"]).lower(): item for item in functions}
    name_to_entry: dict[str, str] = {}
    for entry, item in by_entry.items():
        name_to_entry[str(item["name"])] = entry
    return by_entry, name_to_entry


def build_edges(by_entry: dict[str, dict[str, object]], name_to_entry: dict[str, str]) -> list[dict[str, str]]:
    edges: set[tuple[str, str]] = set()
    for caller_entry, item in by_entry.items():
        for callee_name in item.get("calls", []):
            callee_entry = name_to_entry.get(str(callee_name))
            if callee_entry is None:
                continue
            edges.add((caller_entry, callee_entry))
    return [{"caller": caller, "callee": callee} for caller, callee in sorted(edges)]


def connected_components(entries: list[str], edges: list[dict[str, str]]) -> list[list[str]]:
    neighbors: dict[str, set[str]] = {entry: set() for entry in entries}
    for edge in edges:
        caller = edge["caller"]
        callee = edge["callee"]
        neighbors.setdefault(caller, set()).add(callee)
        neighbors.setdefault(callee, set()).add(caller)

    seen: set[str] = set()
    components: list[list[str]] = []
    for entry in entries:
        if entry in seen:
            continue
        queue = deque([entry])
        seen.add(entry)
        component: list[str] = []
        while queue:
            current = queue.popleft()
            component.append(current)
            for nxt in neighbors.get(current, set()):
                if nxt not in seen:
                    seen.add(nxt)
                    queue.append(nxt)
        components.append(sorted(component, key=lambda value: int(value, 16)))
    components.sort(key=len, reverse=True)
    return components


def address_clusters(by_entry: dict[str, dict[str, object]], bucket_size: int = 0x10000) -> list[dict[str, object]]:
    buckets: dict[int, list[dict[str, object]]] = defaultdict(list)
    for item in by_entry.values():
        bucket = int(str(item["entry"]), 16) // bucket_size
        buckets[bucket].append(item)

    clusters: list[dict[str, object]] = []
    for bucket, items in sorted(buckets.items()):
        start = bucket * bucket_size
        end = start + bucket_size - 1
        named = [item for item in items if not str(item["name"]).startswith("FUN_")]
        clusters.append(
            {
                "range": f"{start:08x}-{end:08x}",
                "function_count": len(items),
                "manual_name_count": len(named),
                "total_lines": sum(int(item.get("line_count", 0)) for item in items),
                "total_calls": sum(int(item.get("call_count", 0)) for item in items),
                "top_by_lines": [
                    {
                        "entry": str(item["entry"]),
                        "name": str(item["name"]),
                        "line_count": int(item.get("line_count", 0)),
                        "call_count": int(item.get("call_count", 0)),
                    }
                    for item in sorted(items, key=lambda value: int(value.get("line_count", 0)), reverse=True)[:10]
                ],
            }
        )
    return clusters


def top_nodes(by_entry: dict[str, dict[str, object]], field: str, count: int = 50) -> list[dict[str, object]]:
    return [
        {
            "entry": str(item["entry"]),
            "name": str(item["name"]),
            field: int(item.get(field, 0)),
            "line_count": int(item.get("line_count", 0)),
            "call_count": int(item.get("call_count", 0)),
            "caller_count": int(item.get("caller_count", 0)),
            "file": str(item.get("file", "")),
        }
        for item in sorted(by_entry.values(), key=lambda value: int(value.get(field, 0)), reverse=True)[:count]
    ]


def write_dot(by_entry: dict[str, dict[str, object]], edges: list[dict[str, str]], max_edges: int = 2500) -> None:
    degree: dict[str, int] = defaultdict(int)
    for edge in edges:
        degree[edge["caller"]] += 1
        degree[edge["callee"]] += 1
    included = {entry for entry, _ in sorted(degree.items(), key=lambda item: item[1], reverse=True)[:400]}
    lines = ["digraph oot3d_callgraph {", "  rankdir=LR;"]
    for entry in sorted(included, key=lambda value: int(value, 16)):
        name = str(by_entry[entry]["name"]).replace('"', '\\"')
        lines.append(f'  "{entry}" [label="{name}\\n{entry}"];')
    emitted = 0
    for edge in edges:
        if edge["caller"] in included and edge["callee"] in included:
            lines.append(f'  "{edge["caller"]}" -> "{edge["callee"]}";')
            emitted += 1
            if emitted >= max_edges:
                break
    lines.append("}")
    (ANALYSIS / "callgraph_top.dot").write_text("\n".join(lines), encoding="utf-8")


def write_markdown(
    by_entry: dict[str, dict[str, object]],
    edges: list[dict[str, str]],
    components: list[list[str]],
    clusters: list[dict[str, object]],
) -> None:
    lines = [
        "# Callgraph And Module Clusters",
        "",
        "Generated from `scripts/build_callgraph.py`.",
        "",
        f"- Functions: {len(by_entry)}",
        f"- Edges: {len(edges)}",
        f"- Connected components: {len(components)}",
        f"- Largest component size: {len(components[0]) if components else 0}",
        f"- Address clusters: {len(clusters)}",
        "",
        "## Highest Fan-In",
        "",
        "| Entry | Name | Callers | Lines | Calls | File |",
        "| --- | --- | --- | --- | --- | --- |",
    ]
    for item in top_nodes(by_entry, "caller_count", 30):
        lines.append(
            f"| {item['entry']} | {item['name']} | {item['caller_count']} | {item['line_count']} | {item['call_count']} | {item['file']} |"
        )
    lines.extend(["", "## Highest Fan-Out", "", "| Entry | Name | Calls | Lines | Callers | File |", "| --- | --- | --- | --- | --- | --- |"])
    for item in top_nodes(by_entry, "call_count", 30):
        lines.append(
            f"| {item['entry']} | {item['name']} | {item['call_count']} | {item['line_count']} | {item['caller_count']} | {item['file']} |"
        )
    lines.extend(["", "## Densest Address Clusters", "", "| Range | Functions | Manual Names | Calls | Lines | Top Function |", "| --- | --- | --- | --- | --- | --- |"])
    for cluster in sorted(clusters, key=lambda item: int(item["function_count"]), reverse=True)[:30]:
        top = cluster["top_by_lines"][0] if cluster["top_by_lines"] else {}
        lines.append(
            f"| {cluster['range']} | {cluster['function_count']} | {cluster['manual_name_count']} | "
            f"{cluster['total_calls']} | {cluster['total_lines']} | {top.get('entry', '')} {top.get('name', '')} |"
        )
    lines.append("")
    (ANALYSIS / "callgraph_report.md").write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    by_entry, name_to_entry = load_functions()
    edges = build_edges(by_entry, name_to_entry)
    components = connected_components(sorted(by_entry, key=lambda value: int(value, 16)), edges)
    clusters = address_clusters(by_entry)

    graph = {
        "nodes": list(by_entry.values()),
        "edges": edges,
        "components": [
            {
                "id": idx,
                "size": len(component),
                "entries": component,
            }
            for idx, component in enumerate(components)
        ],
    }
    (ANALYSIS / "callgraph.json").write_text(json.dumps(graph, indent=2), encoding="utf-8")
    (ANALYSIS / "address_clusters.json").write_text(json.dumps(clusters, indent=2), encoding="utf-8")
    write_dot(by_entry, edges)
    write_markdown(by_entry, edges, components, clusters)

    summary = {
        "functions": len(by_entry),
        "edges": len(edges),
        "components": len(components),
        "largest_component": len(components[0]) if components else 0,
        "address_clusters": len(clusters),
    }
    print(json.dumps(summary, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
