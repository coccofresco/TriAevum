#!/usr/bin/env python3
"""Generate an Azahar-native OOT3D location and state coverage catalog."""

from __future__ import annotations

import argparse
import ast
import hashlib
import json
import re
import subprocess
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any, Iterable


CATALOG_FORMAT = "oot3d_azahar_coverage_catalog_v1"
CODE_LOAD_BASE = 0x00100000
GLOBAL_ENTRANCE_TABLE = 0x00543BB8
GLOBAL_ENTRANCE_ENTRY_SIZE = 4
GLOBAL_ENTRANCE_EXTENT_ZERO_RUN = 16
CUTSCENE_INDEX_LAYER_BASE = 0xFFF0

DEFAULT_RUNTIME_CONTRACT = {
    "title_id": 0x0004000000033600,
    "player_update_function": 0x001E1B54,
    "game_state_update_function": 0x00417014,
    "play_update_function": 0x002E2E60,
    "play_init_function": 0x00449440,
    "play_main_function": 0x004523AC,
    "request_transition_function": 0x003348E8,
    "prepare_transition_effect_function": 0x0035DA3C,
    "current_entrance_address": 0x00587958,
    "entrance_table_address": GLOBAL_ENTRANCE_TABLE,
    "play_scene_id_offset": 0x0104,
    "pending_entrance_offset": 0x5C32,
    "pending_trigger_offset": 0x5C2D,
    "transition_effect_offset": 0x5C76,
    "transition_trigger": 0x14,
}


def file_identity(path: Path) -> dict[str, Any]:
    payload = path.read_bytes()
    return {
        "path": path.as_posix(),
        "bytes": len(payload),
        "sha256": hashlib.sha256(payload).hexdigest(),
    }


def git_identity(root: Path) -> dict[str, Any]:
    def run(*arguments: str) -> str:
        return subprocess.check_output(
            ["git", "-C", str(root), *arguments], text=True
        ).strip()

    status = run("status", "--porcelain").splitlines()
    return {
        "root": root.as_posix(),
        "commit": run("rev-parse", "HEAD"),
        "branch": run("branch", "--show-current"),
        "dirty": bool(status),
        "dirty_path_count": len(status),
    }


def parse_c_integer(token: str) -> int:
    value = re.sub(r"[uUlL]+$", "", token.strip())
    return int(value, 0)


def parse_c_string(token: str) -> str:
    value = ast.literal_eval(token.strip())
    if not isinstance(value, str):
        raise ValueError(f"expected a C string literal, got: {token}")
    return value


def split_initializer_fields(row: str) -> list[str]:
    fields: list[str] = []
    start = 0
    depth = 0
    quote = ""
    escaped = False
    for index, character in enumerate(row):
        if quote:
            if escaped:
                escaped = False
            elif character == "\\":
                escaped = True
            elif character == quote:
                quote = ""
            continue
        if character in {'"', "'"}:
            quote = character
        elif character in "{([":
            depth += 1
        elif character in "})]":
            depth -= 1
        elif character == "," and depth == 0:
            fields.append(row[start:index].strip())
            start = index + 1
    tail = row[start:].strip()
    if tail:
        fields.append(tail)
    return fields


def extract_initializer_rows(path: Path, symbol: str) -> list[list[str]]:
    text = path.read_text(encoding="utf-8")
    declaration = re.search(
        rf"\b{re.escape(symbol)}\s*\[\s*\]\s*=\s*\{{", text
    )
    if declaration is None:
        raise ValueError(f"initializer {symbol} not found in {path}")

    rows: list[list[str]] = []
    outer_depth = 1
    row_start: int | None = None
    quote = ""
    escaped = False
    for index in range(declaration.end(), len(text)):
        character = text[index]
        if quote:
            if escaped:
                escaped = False
            elif character == "\\":
                escaped = True
            elif character == quote:
                quote = ""
            continue
        if character in {'"', "'"}:
            quote = character
            continue
        if character == "{":
            outer_depth += 1
            if outer_depth == 2:
                row_start = index + 1
        elif character == "}":
            if outer_depth == 2 and row_start is not None:
                rows.append(split_initializer_fields(text[row_start:index]))
                row_start = None
            outer_depth -= 1
            if outer_depth == 0:
                break
    return rows


def parse_scene_rows(path: Path) -> dict[int, dict[str, Any]]:
    scenes: dict[int, dict[str, Any]] = {}
    for fields in extract_initializer_rows(path, "oot3d_scene_source_rows"):
        if len(fields) != 16:
            raise ValueError(f"unexpected scene-source row width {len(fields)}")
        scene_id = parse_c_integer(fields[0])
        scenes[scene_id] = {
            "scene_id": scene_id,
            "resource_block": fields[1],
            "resource_record_index": parse_c_integer(fields[2]),
            "scene_path": parse_c_string(fields[3]),
            "archive_path": parse_c_string(fields[4]),
            "source_basename": parse_c_string(fields[5]),
            "source_c_file": parse_c_string(fields[6]),
            "scene_index_symbol": parse_c_string(fields[7]),
            "setup_count": parse_c_integer(fields[11]),
            "room_count": parse_c_integer(fields[12]),
            "command_count": parse_c_integer(fields[13]),
            "promoted_entrance_ref_count": parse_c_integer(fields[15]),
        }
    return scenes


def parse_setup_rows(path: Path) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for fields in extract_initializer_rows(path, "oot3d_scene_setup_source_rows"):
        if len(fields) != 23:
            raise ValueError(f"unexpected scene-setup row width {len(fields)}")
        rows.append(
            {
                "setup_source_index": parse_c_integer(fields[0]),
                "binding_kind": fields[1],
                "binding_index": parse_c_integer(fields[2]),
                "scene_id": parse_c_integer(fields[3]),
                "setup_index": parse_c_integer(fields[4]),
                "scene_path": parse_c_string(fields[5]),
                "source_basename": parse_c_string(fields[6]),
                "scene_index_symbol": parse_c_string(fields[7]),
                "setup_role": parse_c_string(fields[8]),
                "command_ref_count": parse_c_integer(fields[11]),
                "handler_resolved_count": parse_c_integer(fields[12]),
                "handler_missing_count": parse_c_integer(fields[13]),
                "native_control_marker_count": parse_c_integer(fields[14]),
                "decoded_count_mismatch_count": parse_c_integer(fields[15]),
                "has_spawns": bool(parse_c_string(fields[16])),
                "has_entrances": bool(parse_c_string(fields[17])),
                "has_exits": bool(parse_c_string(fields[18])),
                "has_transition_actors": bool(parse_c_string(fields[19])),
                "has_light_settings": bool(parse_c_string(fields[20])),
                "has_paths": bool(parse_c_string(fields[21])),
                "has_cutscenes": bool(parse_c_string(fields[22])),
            }
        )
    return rows


def count_rows_by_scene(path: Path, symbol: str, scene_field: int) -> Counter[int]:
    counts: Counter[int] = Counter()
    for fields in extract_initializer_rows(path, symbol):
        if len(fields) <= scene_field:
            raise ValueError(f"{symbol} row is too short: {len(fields)}")
        counts[parse_c_integer(fields[scene_field])] += 1
    return counts


def infer_entrance_table_extent(code: bytes) -> tuple[int, dict[str, Any]]:
    table_offset = GLOBAL_ENTRANCE_TABLE - CODE_LOAD_BASE
    maximum = (len(code) - table_offset) // GLOBAL_ENTRANCE_ENTRY_SIZE
    zero_run = 0
    for index in range(maximum):
        offset = table_offset + index * GLOBAL_ENTRANCE_ENTRY_SIZE
        if code[offset : offset + GLOBAL_ENTRANCE_ENTRY_SIZE] == b"\0\0\0\0":
            zero_run += 1
            if zero_run == GLOBAL_ENTRANCE_EXTENT_ZERO_RUN:
                first = index - zero_run + 1
                return first, {
                    "method": f"first_{GLOBAL_ENTRANCE_EXTENT_ZERO_RUN}_zero_entries",
                    "first_zero_entry": first,
                }
        else:
            zero_run = 0
    return maximum, {"method": "code_image_end"}


def decode_entrances(code: bytes, scenes: dict[int, dict[str, Any]]) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    count, extent_evidence = infer_entrance_table_extent(code)
    table_offset = GLOBAL_ENTRANCE_TABLE - CODE_LOAD_BASE
    rows: list[dict[str, Any]] = []
    unmapped_scene_ids: Counter[int] = Counter()
    for entrance_index in range(count):
        offset = table_offset + entrance_index * GLOBAL_ENTRANCE_ENTRY_SIZE
        raw = code[offset : offset + GLOBAL_ENTRANCE_ENTRY_SIZE]
        scene_id = raw[0]
        if scene_id not in scenes:
            unmapped_scene_ids[scene_id] += 1
            continue
        rows.append(
            {
                "entrance_index": entrance_index,
                "scene_id": scene_id,
                "local_entrance_index": raw[1],
                "field": int.from_bytes(raw[2:4], "little"),
                "raw_hex": raw.hex(),
                "table_file_offset": offset,
            }
        )
    return rows, {
        "inferred_entry_count": count,
        "mapped_entry_count": len(rows),
        "unmapped_entry_count": count - len(rows),
        "unmapped_scene_ids": dict(sorted(unmapped_scene_ids.items())),
        "extent_evidence": extent_evidence,
    }


def load_previously_validated_entrances(path: Path | None) -> set[int]:
    if path is None or not path.is_file():
        return set()
    document = json.loads(path.read_text(encoding="utf-8"))
    return {
        int(recipe["entrance_index"])
        for recipe in document.get("recipes", [])
        if isinstance(recipe, dict) and "entrance_index" in recipe
    }


def select_representatives(
    entrances: list[dict[str, Any]], validated: set[int]
) -> tuple[dict[int, int], dict[tuple[int, int], int]]:
    by_scene: dict[int, list[dict[str, Any]]] = defaultdict(list)
    by_local: dict[tuple[int, int], list[dict[str, Any]]] = defaultdict(list)
    for row in entrances:
        by_scene[row["scene_id"]].append(row)
        by_local[(row["scene_id"], row["local_entrance_index"])].append(row)

    def choose(rows: Iterable[dict[str, Any]]) -> int:
        return min(
            rows,
            key=lambda row: (
                0 if row["entrance_index"] in validated else 1,
                row["entrance_index"],
            ),
        )["entrance_index"]

    scene_representatives = {
        scene_id: choose(rows) for scene_id, rows in by_scene.items()
    }
    # These two entries have direct runtime evidence and stable seed-state
    # coverage. Prefer them over lower aliases of the same native scene.
    for scene_id, entrance_index in {0x34: 0x00BB, 0x55: 0x0211}.items():
        if any(row["entrance_index"] == entrance_index for row in by_scene[scene_id]):
            scene_representatives[scene_id] = entrance_index
    return (
        scene_representatives,
        {key: choose(rows) for key, rows in by_local.items()},
    )


def scenario_coverage(
    scene: dict[str, Any],
    setup_rows: list[dict[str, Any]],
    actor_counts: Counter[int],
    light_counts: Counter[int],
    cutscene_counts: Counter[int],
) -> dict[str, Any]:
    role_counts = Counter(row["setup_role"] for row in setup_rows)
    return {
        "scene_setup_count": len(setup_rows),
        "gameplay_setup_count": role_counts["gameplay"],
        "cutscene_setup_count": role_counts["cutscene"],
        "room_count": scene["room_count"],
        "scene_command_count": scene["command_count"],
        "actor_source_count": actor_counts[scene["scene_id"]],
        "light_source_count": light_counts[scene["scene_id"]],
        "cutscene_source_count": cutscene_counts[scene["scene_id"]],
    }


def scene_runtime_availability(scene: dict[str, Any]) -> dict[str, Any]:
    if scene["source_basename"] and scene["scene_index_symbol"]:
        return {
            "launchable": True,
            "classification": "native_indexed_resource",
            "reason": "The decomp source table has a native archive and indexed ZSI source.",
        }
    if scene["archive_path"]:
        return {
            "launchable": False,
            "classification": "native_zsi_not_indexed",
            "reason": (
                "The native table names an archive but exposes no indexed ZSI, setup, "
                "room, or command source for this build."
            ),
        }
    return {
        "launchable": False,
        "classification": "native_resource_not_present",
        "reason": (
            "The native table retains only a scene name; no archive or indexed scene "
            "resource is present in the extracted title."
        ),
    }


def make_entrance_scenario(
    row: dict[str, Any],
    scene: dict[str, Any],
    tags: list[str],
    coverage: dict[str, Any],
    args: argparse.Namespace,
) -> dict[str, Any]:
    return {
        "id": f"{scene['source_basename']}_entry_{row['entrance_index']:04x}",
        "kind": "native_global_entrance",
        "selection_tags": tags,
        "entrance_index": row["entrance_index"],
        "scene_id": row["scene_id"],
        "local_entrance_index": row["local_entrance_index"],
        "field": row["field"],
        "native_state": {},
        "scene": scene,
        "runtime_availability": scene_runtime_availability(scene),
        "coverage": coverage,
        "execution": {
            "settle_presented_frames": args.settle_frames,
            "capture_frames": args.capture_frames,
            "ready_timeout_seconds": args.ready_timeout_seconds,
        },
    }


def build_catalog(args: argparse.Namespace) -> dict[str, Any]:
    decomp_root = args.decomp_root.resolve()
    code_path = decomp_root / "work/extract/exefs/code.bin"
    scene_path = decomp_root / "src/code/z_scene_source_table.c"
    setup_path = decomp_root / "src/code/z_scene_setup_source_table.c"
    actor_path = decomp_root / "src/code/z_scene_actor_source_table.c"
    light_path = decomp_root / "src/code/z_scene_light_source_table.c"
    cutscene_path = decomp_root / "src/code/z_scene_cutscene_source_table.c"
    input_paths = [
        code_path,
        scene_path,
        setup_path,
        actor_path,
        light_path,
        cutscene_path,
    ]
    missing = [path for path in input_paths if not path.is_file()]
    if missing:
        raise FileNotFoundError(f"missing decomp inputs: {missing}")

    scenes = parse_scene_rows(scene_path)
    setup_rows = parse_setup_rows(setup_path)
    setups_by_scene: dict[int, list[dict[str, Any]]] = defaultdict(list)
    for row in setup_rows:
        setups_by_scene[row["scene_id"]].append(row)
    actor_counts = count_rows_by_scene(
        actor_path, "oot3d_scene_actor_source_rows", 4
    )
    light_counts = count_rows_by_scene(
        light_path, "oot3d_scene_light_source_rows", 3
    )
    cutscene_counts = count_rows_by_scene(
        cutscene_path, "oot3d_scene_cutscene_source_rows", 3
    )

    entrances, entrance_summary = decode_entrances(code_path.read_bytes(), scenes)
    validated = load_previously_validated_entrances(args.reference_catalog)
    scene_representatives, local_representatives = select_representatives(
        entrances, validated
    )
    representative_by_scene = {
        row["scene_id"]: row for row in entrances
        if scene_representatives[row["scene_id"]] == row["entrance_index"]
    }

    smoke_entrances = {
        scene_representatives[scene_id]
        for scene_id in (0x34, 0x51, 0x55)
        if scene_id in scene_representatives
    }
    scenarios: list[dict[str, Any]] = []
    for row in entrances:
        scene = scenes[row["scene_id"]]
        tags = ["exhaustive"]
        availability = scene_runtime_availability(scene)
        tags.append("runtime_launchable" if availability["launchable"] else "catalog_only")
        if scene_representatives[row["scene_id"]] == row["entrance_index"]:
            tags.append("scene_representative")
        if row["entrance_index"] in smoke_entrances:
            tags.append("smoke")
        if local_representatives[
            (row["scene_id"], row["local_entrance_index"])
        ] == row["entrance_index"]:
            tags.append("local_entrance_representative")
        if row["entrance_index"] in validated:
            tags.append("previously_validated")
        coverage = scenario_coverage(
            scene,
            setups_by_scene[row["scene_id"]],
            actor_counts,
            light_counts,
            cutscene_counts,
        )
        scenarios.append(make_entrance_scenario(row, scene, tags, coverage, args))

    cutscene_setup_scenarios: list[dict[str, Any]] = []
    unresolved_gameplay_setups: list[dict[str, Any]] = []
    unresolved_variant_setups: list[dict[str, Any]] = []
    for setup in setup_rows:
        if setup["setup_index"] == 0:
            continue
        if setup["binding_kind"] != "OOT3D_SCENE_COMMAND_BINDING_SCENE_ROW":
            unresolved_variant_setups.append(setup)
            continue
        if setup["setup_role"] != "cutscene":
            unresolved_gameplay_setups.append(setup)
            continue
        entrance = representative_by_scene.get(setup["scene_id"])
        if entrance is None:
            continue
        scene = scenes[setup["scene_id"]]
        scenario = make_entrance_scenario(
            entrance,
            scene,
            ["setup_variant", "cutscene_setup"],
            scenario_coverage(
                scene,
                setups_by_scene[setup["scene_id"]],
                actor_counts,
                light_counts,
                cutscene_counts,
            ),
            args,
        )
        scenario["id"] = (
            f"{scene['source_basename']}_setup_{setup['setup_index']}_"
            f"entry_{entrance['entrance_index']:04x}"
        )
        scenario["kind"] = "native_cutscene_setup"
        scenario["native_state"] = {
            "cutscene_index": CUTSCENE_INDEX_LAYER_BASE | setup["setup_index"]
        }
        scenario["setup"] = setup
        cutscene_setup_scenarios.append(scenario)

    scenarios.extend(cutscene_setup_scenarios)
    scenarios.sort(
        key=lambda scenario: (
            scenario["scene_id"],
            scenario["entrance_index"],
            scenario["native_state"].get("cutscene_index", 0),
        )
    )
    if not scenarios:
        raise ValueError("no Azahar coverage scenarios were generated")

    return {
        "format": CATALOG_FORMAT,
        "purpose": "azahar_native_location_and_state_renderer_coverage",
        "source_policy": (
            "Every scenario enters through the original OOT3D transition helpers. "
            "The host selects only code.bin entrance indices and decomp-proven native "
            "state dimensions; it never constructs scenes, rooms, actors, cameras, "
            "lights, materials, or draw commands."
        ),
        "provenance": {
            "decomp": git_identity(decomp_root),
            "inputs": [file_identity(path) for path in input_paths],
            "reference_catalog": (
                file_identity(args.reference_catalog)
                if args.reference_catalog and args.reference_catalog.is_file()
                else None
            ),
        },
        "runtime_contract": DEFAULT_RUNTIME_CONTRACT,
        "summary": {
            **entrance_summary,
            "scene_source_count": len(scenes),
            "covered_scene_count": len({row["scene_id"] for row in entrances}),
            "entrance_scenario_count": len(entrances),
            "scene_representative_count": len(scene_representatives),
            "runtime_launchable_scene_count": sum(
                scene_runtime_availability(scene)["launchable"]
                for scene in scenes.values()
            ),
            "catalog_only_scene_count": sum(
                not scene_runtime_availability(scene)["launchable"]
                for scene in scenes.values()
            ),
            "local_entrance_representative_count": len(local_representatives),
            "previously_validated_entrance_count": len(validated),
            "cutscene_setup_scenario_count": len(cutscene_setup_scenarios),
            "total_scenario_count": len(scenarios),
            "unresolved_gameplay_setup_selector_count": len(
                unresolved_gameplay_setups
            ),
            "unresolved_variant_setup_selector_count": len(
                unresolved_variant_setups
            ),
        },
        "declared_gaps": {
            "policy": (
                "Rows are not injected until their native selector is evidenced. "
                "This prevents host-authored gameplay state from contaminating the corpus."
            ),
            "gameplay_setups_without_proven_selector": unresolved_gameplay_setups,
            "alternate_resource_setups_without_proven_selector": unresolved_variant_setups,
        },
        "scenarios": scenarios,
    }


def parse_args() -> argparse.Namespace:
    script_dir = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--decomp-root", type=Path, default=Path("I:/oot3decomp"))
    parser.add_argument(
        "--reference-catalog",
        type=Path,
        default=script_dir / "oot3d_structural_scenarios.json",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=script_dir / "oot3d_azahar_coverage_scenarios.json",
    )
    parser.add_argument("--settle-frames", type=int, default=180)
    parser.add_argument("--capture-frames", type=int, default=3)
    parser.add_argument("--ready-timeout-seconds", type=int, default=45)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if min(args.settle_frames, args.capture_frames, args.ready_timeout_seconds) <= 0:
        raise ValueError("execution budgets must be positive")
    catalog = build_catalog(args)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(catalog, indent=2) + "\n", encoding="utf-8"
    )
    summary = catalog["summary"]
    print(
        f"generated {summary['total_scenario_count']} Azahar scenarios: "
        f"{summary['covered_scene_count']}/{summary['scene_source_count']} scenes, "
        f"{summary['entrance_scenario_count']} entrances, "
        f"{summary['cutscene_setup_scenario_count']} cutscene setups"
    )
    print(args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
