#!/usr/bin/env python3
"""Generate native scene-transition recipes from decoded OOT3D evidence."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any


CATALOG_FORMAT = "oot3d_structural_scenario_catalog_v2"
ELIGIBLE_VALIDATION_STATUSES = {
    "native_kokiri_slot5_entrypoint_confirmed",
    "native_scene_candidate_local_entrance_covered",
    "native_scene_candidate_spawn_resolved",
}


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as stream:
        value = json.load(stream)
    if not isinstance(value, dict):
        raise ValueError(f"{path} does not contain a JSON object")
    return value


def file_identity(path: Path) -> dict[str, Any]:
    payload = path.read_bytes()
    return {
        "path": path.as_posix(),
        "bytes": len(payload),
        "sha256": hashlib.sha256(payload).hexdigest(),
    }


def recipe_id(row: dict[str, Any]) -> str:
    stem = re.sub(
        r"[^a-z0-9]+",
        "_",
        str(row["native_scene_source_basename"]).lower(),
    ).strip("_")
    return f"{stem}_entry_{int(row['entrance_index']):04x}"


def aggregate_rows(
    rows: list[dict[str, Any]], key: str = "scene_path"
) -> dict[str, list[dict[str, Any]]]:
    result: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for row in rows:
        scene_path = str(row.get(key, ""))
        if scene_path:
            result[scene_path].append(row)
    return result


def setup_coverage(rows: list[dict[str, Any]]) -> dict[str, Any]:
    role_counts = Counter(str(row.get("setup_role", "unknown")) for row in rows)
    return {
        "setup_count": len(rows),
        "gameplay_setup_count": role_counts.get("gameplay", 0),
        "cutscene_setup_count": role_counts.get("cutscene", 0),
        "spawn_count": sum(int(row.get("spawn_count", 0)) for row in rows),
        "transition_actor_count": sum(
            int(row.get("transition_count", 0)) for row in rows
        ),
        "light_setting_count": sum(
            int(row.get("light_settings_count", 0)) for row in rows
        ),
        "cutscene_reference_count": sum(
            int(row.get("cutscene_count", 0)) for row in rows
        ),
        "unresolved_command_count": sum(
            int(row.get("unresolved_command_count", 0)) for row in rows
        ),
    }


def build_catalog(args: argparse.Namespace) -> dict[str, Any]:
    entrance_path = args.entrances.resolve()
    setup_path = args.setups.resolve()
    actor_path = args.actors.resolve()
    light_path = args.lights.resolve()
    cutscene_path = args.cutscenes.resolve()
    transition_path = args.transition_contract.resolve()

    entrances = load_json(entrance_path)
    setups = load_json(setup_path)
    actors = load_json(actor_path)
    lights = load_json(light_path)
    cutscenes = load_json(cutscene_path)
    transition = load_json(transition_path)

    setup_by_scene = aggregate_rows(list(setups.get("rows", [])))
    actor_by_scene = aggregate_rows(list(actors.get("rows", [])))
    light_by_scene = aggregate_rows(list(lights.get("rows", [])))
    cutscene_by_scene = aggregate_rows(list(cutscenes.get("cutscene_rows", [])))

    helper = transition.get("helpers", {}).get("FUN_003348e8")
    if not isinstance(helper, dict):
        raise ValueError("transition evidence has no FUN_003348e8 helper")
    helper_entry = int(str(helper["entry"]), 16)
    if helper_entry == 0:
        raise ValueError("transition helper entry is zero")

    recipes: list[dict[str, Any]] = []
    seen_ids: set[str] = set()
    for row in entrances.get("rows", []):
        validation_status = str(row.get("native_validation_status", ""))
        if validation_status not in ELIGIBLE_VALIDATION_STATUSES:
            continue
        scene_path = str(row.get("native_scene_resource_zsi_path", ""))
        identifier = recipe_id(row)
        if identifier in seen_ids:
            raise ValueError(f"duplicate generated recipe id: {identifier}")
        seen_ids.add(identifier)
        setup_rows = setup_by_scene.get(scene_path, [])
        actor_rows = actor_by_scene.get(scene_path, [])
        light_rows = light_by_scene.get(scene_path, [])
        cutscene_rows = cutscene_by_scene.get(scene_path, [])
        coverage = setup_coverage(setup_rows)
        coverage.update(
            {
                "actor_placement_count": len(actor_rows),
                "decoded_light_record_count": len(light_rows),
                "cutscene_source_count": len(cutscene_rows),
            }
        )
        recipes.append(
            {
                "id": identifier,
                "category": "native_scene_entrance",
                "entrance_index": int(row["entrance_index"]),
                "scene_id": int(row["scene_id"]),
                "local_entrance_index": int(row["local_entrance_index"]),
                "field": int(row["field"]),
                "scene_path": scene_path,
                "scene_source_basename": str(
                    row.get("native_scene_source_basename", "")
                ),
                "scene_index_symbol": str(row.get("native_scene_index_symbol", "")),
                "semantic_scene": str(row.get("secondary_scene_enum", "")),
                "validation_status": validation_status,
                "reference_kinds": str(row.get("reference_kinds", "")),
                "reference_use_count": int(row.get("reference_use_count", 0)),
                "coverage": coverage,
                "execution": {
                    "input_profile": "idle",
                    "settle_frames": args.settle_frames,
                    "capture_frames": args.capture_frames,
                    "ready_timeout_guest_frames": args.ready_timeout_frames,
                },
            }
        )

    recipes.sort(key=lambda recipe: (recipe["scene_id"], recipe["entrance_index"]))
    if not recipes:
        raise ValueError("no validated native entrance recipes were generated")

    unique_scenes = {int(recipe["scene_id"]) for recipe in recipes}
    return {
        "format": CATALOG_FORMAT,
        "purpose": "native_loader_structural_scene_coverage",
        "source_policy": (
            "Recipes select code.bin global entrances decoded from OOT3D and "
            "invoke the original transition helper. The host never constructs "
            "PlayState, rooms, actors, cameras, lights, or materials."
        ),
        "generated_from": [
            file_identity(path)
            for path in (
                entrance_path,
                setup_path,
                actor_path,
                light_path,
                cutscene_path,
                transition_path,
            )
        ],
        "runtime_contract": {
            "source": (
                "OOT3D code.bin FUN_003365B0 direct-exit sequence, "
                "Save_GetEntranceIndex/PlayState scene identity, transition "
                "helper semantics, and live runtime observations"
            ),
            "request_transition_function": helper_entry,
            "prepare_transition_effect_function": args.prepare_transition_effect,
            "direct_call_return_address": args.direct_call_return,
            "current_entrance_address": args.current_entrance_address,
            "play_scene_id_offset": 0x104,
            "pending_entrance_offset": 0x5C32,
            "pending_trigger_offset": 0x5C2D,
            "transition_effect_offset": 0x5C76,
            "transition_trigger": 0x14,
            "accepted_transition_identity": (
                "requested_entrance_observed_before_target_scene"
            ),
            "native_departure_completion": "complete_after_settle",
        },
        "summary": {
            "recipe_count": len(recipes),
            "unique_scene_count": len(unique_scenes),
            "source_entrance_row_count": len(entrances.get("rows", [])),
            "eligible_validation_statuses": sorted(ELIGIBLE_VALIDATION_STATUSES),
        },
        "recipes": recipes,
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--entrances", type=Path, required=True)
    parser.add_argument("--setups", type=Path, required=True)
    parser.add_argument("--actors", type=Path, required=True)
    parser.add_argument("--lights", type=Path, required=True)
    parser.add_argument("--cutscenes", type=Path, required=True)
    parser.add_argument("--transition-contract", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--settle-frames", type=int, default=180)
    parser.add_argument("--capture-frames", type=int, default=180)
    parser.add_argument("--ready-timeout-frames", type=int, default=900)
    parser.add_argument("--direct-call-return", type=lambda value: int(value, 0), default=0x60000000)
    parser.add_argument("--prepare-transition-effect", type=lambda value: int(value, 0), default=0x0035DA3C)
    parser.add_argument("--current-entrance-address", type=lambda value: int(value, 0), default=0x00587958)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if min(args.settle_frames, args.capture_frames, args.ready_timeout_frames) <= 0:
        raise ValueError("scenario frame counts must be positive")
    catalog = build_catalog(args)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(catalog, indent=2, sort_keys=False) + "\n", encoding="utf-8"
    )
    print(
        f"generated {catalog['summary']['recipe_count']} recipes for "
        f"{catalog['summary']['unique_scene_count']} scenes: {args.output}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
