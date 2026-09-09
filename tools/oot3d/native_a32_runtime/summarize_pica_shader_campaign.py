"""Report native shader/pipeline coverage without equating it to game coverage."""

from __future__ import annotations

import argparse
from collections import Counter
import json
from pathlib import Path


def summarize(inventory: dict, baseline: dict | None = None) -> dict:
    if inventory.get("format") != "oot3d_pica_effective_shader_inventory_v1":
        raise ValueError("unsupported native shader inventory")
    previous = {(s["stage"], s["source_id"]) for s in (baseline or {}).get("shaders", [])}
    sources = {(s["stage"], s["source_id"]): s for s in inventory["shaders"]}
    recipes = {r["native_pipeline_id"]: r for r in inventory["native_pipeline_recipes"]}
    scenarios = {}
    for frame in inventory["capture_import"]["frames"]:
        row = scenarios.setdefault(frame["scenario"], {"frames": 0, "draws": 0, "failures": 0, "pipelines": set()})
        row["frames"] += 1
        row["draws"] += frame["draws"]
        row["failures"] += frame["failures"]
        row["pipelines"].update(frame["native_pipeline_ids"])
    for name, row in scenarios.items():
        if not row["pipelines"] <= recipes.keys():
            raise ValueError(f"scenario references an absent pipeline: {name}")
        used = set()
        for identity in row["pipelines"]:
            recipe = recipes[identity]
            used.update((stage, recipe[field]) for stage, field in (
                ("vertex", "vertex_source_id"), ("fragment", "fragment_source_id"),
                ("nri_fragment", "nri_fragment_source_id")))
        if not used <= sources.keys():
            raise ValueError(f"pipeline references an absent shader: {name}")
        row["native_pipeline_count"] = len(row["pipelines"])
        row["shader_count"] = len(used)
        row["shaders_absent_from_baseline"] = len(used - previous)
        row["native_pipeline_ids"] = sorted(row.pop("pipelines"))
    # This is a compact replay selection for the observed native recipe universe,
    # not a promise that unobserved gameplay states need no further collection.
    uncovered = set(recipes)
    remaining = {name: set(row["native_pipeline_ids"]) for name, row in scenarios.items()}
    cover = []
    while uncovered and remaining:
        name = min(remaining, key=lambda n: (-len(remaining[n] & uncovered), n))
        gained = remaining.pop(name) & uncovered
        if not gained:
            break
        uncovered -= gained
        cover.append({"scenario_id": name, "new_native_pipeline_count": len(gained)})
    return {
        "format": "oot3d_pica_shader_campaign_summary_v1",
        "game_coverage_proven": False,
        "device_pipeline_prewarm": False,
        "counts": {"scenarios": len(scenarios), "native_pipelines": len(recipes),
                   "shader_modules": len(sources), "new_shader_modules": len(sources.keys() - previous),
                   "draws": inventory["capture_import"]["draws"],
                   "complete_draws": inventory["capture_import"]["complete_draws"],
                   "failures": inventory["capture_import"]["failures"]},
        "module_stages": dict(Counter(stage for stage, _ in sources)),
        "new_module_stages": dict(Counter(stage for stage, _ in sources.keys() - previous)),
        "scenarios": [{"scenario_id": name, **row} for name, row in sorted(scenarios.items())],
        "compact_native_pipeline_cover": cover,
        "uncovered_native_pipeline_ids": sorted(uncovered),
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--inventory", type=Path, required=True)
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.output.resolve() in {p.resolve() for p in (args.inventory, args.baseline) if p}:
        parser.error("output must not overwrite an input")
    report = summarize(json.loads(args.inventory.read_text(encoding="utf-8-sig")),
                       json.loads(args.baseline.read_text(encoding="utf-8-sig")) if args.baseline else None)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report["counts"]))


if __name__ == "__main__":
    main()
