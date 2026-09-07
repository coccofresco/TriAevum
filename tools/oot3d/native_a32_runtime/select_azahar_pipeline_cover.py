#!/usr/bin/env python3
"""Select a deterministic compact scenario cover from an Azahar matrix."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any, Iterable


MATRIX_FORMAT = "oot3d_azahar_coverage_matrix_v1"
OUTPUT_FORMAT = "oot3d_azahar_pipeline_cover_v1"
DIMENSION_FIELDS = {
    "pipeline": "pipeline_ids",
    "texture": "texture_format_ids",
    "shadow": "shadow_state_ids",
}


def identity_tokens(result: dict[str, Any], dimensions: Iterable[str]) -> set[str]:
    tokens: set[str] = set()
    for dimension in dimensions:
        field = DIMENSION_FIELDS[dimension]
        tokens.update(f"{dimension}:{value}" for value in result.get(field, []))
    return tokens


def select_cover(matrix: dict[str, Any], dimensions: tuple[str, ...]) -> dict[str, Any]:
    if matrix.get("format") != MATRIX_FORMAT:
        raise ValueError(f"unsupported Azahar matrix: {matrix.get('format')!r}")
    unknown = sorted(set(dimensions) - DIMENSION_FIELDS.keys())
    if unknown:
        raise ValueError(f"unknown coverage dimensions: {', '.join(unknown)}")

    latest_by_scenario: dict[str, dict[str, Any]] = {}
    for result in matrix["results"]:
        latest_by_scenario[str(result["scenario_id"])] = result
    latest = list(latest_by_scenario.values())
    passed = [result for result in latest if result["status"] == "passed"]
    failed = [result for result in latest if result["status"] == "failed"]
    unavailable = [result for result in latest if result["status"] == "unavailable"]
    candidates = [
        (index, result, identity_tokens(result, dimensions))
        for index, result in enumerate(passed)
    ]
    universe = set().union(*(tokens for _, _, tokens in candidates)) if candidates else set()
    uncovered = set(universe)
    selected: list[dict[str, Any]] = []
    remaining = list(candidates)

    while uncovered:
        ranked = sorted(
            remaining,
            key=lambda candidate: (
                -len(candidate[2] & uncovered),
                candidate[0],
                str(candidate[1]["scenario_id"]),
            ),
        )
        if not ranked or not (ranked[0][2] & uncovered):
            break
        index, result, tokens = ranked[0]
        newly_covered = tokens & uncovered
        uncovered -= newly_covered
        selected.append(
            {
                "scenario_id": result["scenario_id"],
                "scene_id": int(result["scene_id"]),
                "entrance_index": int(result["entrance_index"]),
                "new_identity_count": len(newly_covered),
                "cumulative_identity_count": len(universe) - len(uncovered),
            }
        )
        remaining = [candidate for candidate in remaining if candidate[0] != index]

    return {
        "format": OUTPUT_FORMAT,
        "evidence_role": "validation_only_not_runtime_input",
        "source_matrix": matrix.get("source_path"),
        "dimensions": list(dimensions),
        "source_counts": {
            "passed_scenarios": len(passed),
            "failed_scenarios": len(failed),
            "unavailable_scenarios": len(unavailable),
            "identity_tokens": len(universe),
        },
        "cover_counts": {
            "selected_scenarios": len(selected),
            "covered_identity_tokens": len(universe) - len(uncovered),
            "uncovered_identity_tokens": len(uncovered),
        },
        "scenario_ids": [entry["scenario_id"] for entry in selected],
        "selected_scenarios": selected,
        "failed_scenario_ids": [result["scenario_id"] for result in failed],
        "unavailable_scenario_ids": [
            result["scenario_id"] for result in unavailable
        ],
        "uncovered_tokens": sorted(uncovered),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--matrix", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--dimensions",
        default="pipeline,texture,shadow",
        help="Comma-separated coverage dimensions: pipeline,texture,shadow",
    )
    args = parser.parse_args()
    dimensions = tuple(value.strip() for value in args.dimensions.split(",") if value.strip())
    if not dimensions:
        raise ValueError("at least one coverage dimension is required")
    matrix = json.loads(args.matrix.read_text(encoding="utf-8"))
    matrix["source_path"] = str(args.matrix.resolve())
    document = select_cover(matrix, dimensions)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")
    counts = document["cover_counts"]
    print(
        f"selected {counts['selected_scenarios']} scenarios for "
        f"{counts['covered_identity_tokens']}/{document['source_counts']['identity_tokens']} "
        "identity tokens"
    )
    print(args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
