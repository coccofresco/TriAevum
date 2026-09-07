from __future__ import annotations

import json
import re
from pathlib import Path
from typing import Any

from .room_compilation_unit import COMPILER_VERSION as ROOM_UNIT_COMPILER_VERSION
from .room_compilation_unit import FORMAT as ROOM_UNIT_FORMAT
from .room_compilation_unit import validate_room_compilation_unit


ROOM_COMPILATION_UNIT_CATALOG_ARCHIVE_PATH = (
    "oot3d/catalog/oot3d_room_compilation_units.json"
)
ROOM_COMPILATION_UNIT_ARCHIVE_ROOT = "oot3d/room_units"


def _version_tuple(value: object) -> tuple[int, int, int] | None:
    if not isinstance(value, str):
        return None
    match = re.fullmatch(r"(\d+)\.(\d+)\.(\d+)", value)
    if match is None:
        return None
    return tuple(int(part) for part in match.groups())


def _room_unit_compiler_version(unit: dict[str, Any]) -> str | None:
    provenance = unit.get("provenance")
    compiler = provenance.get("compiler") if isinstance(provenance, dict) else None
    version = compiler.get("version") if isinstance(compiler, dict) else None
    return version if isinstance(version, str) else None


def _has_current_behavior_graph_contract(unit: dict[str, Any]) -> bool:
    profiles = unit.get("actor_profiles")
    closure = unit.get("closure")
    count_keys = (
        "behavior_graph_profile_count",
        "behavior_function_count",
        "behavior_action_transition_count",
        "behavior_structure_field_count",
    )
    return (
        isinstance(profiles, list)
        and all(isinstance(profile, dict)
                and isinstance(profile.get("behavior_graph"), dict)
                for profile in profiles)
        and isinstance(closure, dict)
        and all(isinstance(closure.get(key), int) for key in count_keys)
    )


def _archive_path(route_id: str, setup_index: int, digest: str) -> str:
    slug = re.sub(r"[^a-z0-9]+", "_", route_id.lower()).strip("_")
    if not slug:
        raise ValueError("room compilation unit route id cannot produce an archive name")
    return (
        f"{ROOM_COMPILATION_UNIT_ARCHIVE_ROOT}/{slug}"
        f"__setup_{setup_index}__{digest[:12]}.json"
    )


def _require_sequence(unit: dict[str, Any], key: str) -> list[Any]:
    value = unit.get(key)
    if not isinstance(value, list):
        raise ValueError(f"room compilation unit {key} must be an array")
    return value


def build_room_compilation_unit_catalog(
        root: Path) -> tuple[dict[str, Any], dict[str, dict[str, Any]]]:
    if not root.is_dir():
        raise ValueError(f"room compilation unit root does not exist: {root}")

    records: list[dict[str, Any]] = []
    excluded: list[dict[str, Any]] = []
    payloads: dict[str, dict[str, Any]] = {}
    route_setups: set[tuple[str, int]] = set()
    source_paths = sorted(root.glob("*.json"), key=lambda path: path.name.lower())
    for source_path in source_paths:
        unit = json.loads(source_path.read_text(encoding="utf-8-sig"))
        if not isinstance(unit, dict) or unit.get("format") != ROOM_UNIT_FORMAT:
            raise ValueError(f"{source_path}: not an OOT3D room compilation unit")

        identity = unit.get("identity")
        closure = unit.get("closure")
        if not isinstance(identity, dict) or not isinstance(closure, dict):
            raise ValueError(f"{source_path}: identity or closure is missing")

        route_id = identity.get("route_id")
        unit_id = identity.get("unit_id")
        setup_index = identity.get("setup_index")
        scene_id = identity.get("scene_id")
        scene_path = identity.get("scene_path")
        digest = identity.get("payload_sha256")
        initial_room_index = identity.get("initial_room_index")
        native_room_indices = identity.get("native_room_indices")
        if not isinstance(route_id, str) or not route_id:
            raise ValueError(f"{source_path}: route id is missing")
        if not isinstance(unit_id, str) or not unit_id:
            raise ValueError(f"{source_path}: unit id is missing")
        if not isinstance(setup_index, int) or not isinstance(scene_id, int):
            raise ValueError(f"{source_path}: native scene/setup identity is invalid")
        if not isinstance(scene_path, str) or not scene_path:
            raise ValueError(f"{source_path}: native scene path is missing")
        if not isinstance(digest, str) or len(digest) != 64:
            raise ValueError(f"{source_path}: payload digest is invalid")
        if (not isinstance(native_room_indices, list) or
                any(not isinstance(value, int) for value in native_room_indices)):
            raise ValueError(f"{source_path}: native room indices are invalid")

        rooms = _require_sequence(unit, "rooms")
        actor_instances = _require_sequence(unit, "actor_instances")
        actor_profiles = _require_sequence(unit, "actor_profiles")
        object_dependencies = _require_sequence(unit, "object_dependencies")
        unresolved = _require_sequence(unit, "unresolved")
        expected_counts = {
            "room_count": len(rooms),
            "actor_instance_count": len(actor_instances),
            "unique_actor_profile_count": len(actor_profiles),
            "object_dependency_count": len(object_dependencies),
            "unresolved_count": len(unresolved),
        }
        for key, expected in expected_counts.items():
            if closure.get(key) != expected:
                raise ValueError(
                    f"{source_path}: closure {key} does not match payload ({expected})"
                )

        composition_status = closure.get("composition_status")
        top_level_complete = str(unit.get("status", "")).startswith("composition_complete_")
        composition_complete = composition_status == "composition_complete"
        if top_level_complete != composition_complete:
            raise ValueError(f"{source_path}: inconsistent composition status")
        route_setup = (route_id, setup_index)
        if route_setup in route_setups:
            raise ValueError(
                f"duplicate room compilation unit route/setup: {route_id} / {setup_index}"
            )
        route_setups.add(route_setup)

        summary = {
            "route_id": route_id,
            "unit_id": unit_id,
            "scene_id": scene_id,
            "scene_path": scene_path,
            "setup_index": setup_index,
            "native_room_indices": native_room_indices,
            "initial_room_index": initial_room_index,
            "status": unit["status"],
            "composition_status": composition_status,
            "behavior_evidence_status": closure.get("behavior_evidence_status"),
            "payload_sha256": digest,
            "closure": expected_counts,
        }
        compiler_version = _room_unit_compiler_version(unit)
        compiler_version_tuple = _version_tuple(compiler_version)
        current_version_tuple = _version_tuple(ROOM_UNIT_COMPILER_VERSION)
        if not _has_current_behavior_graph_contract(unit):
            if (
                compiler_version_tuple is not None
                and current_version_tuple is not None
                and compiler_version_tuple < current_version_tuple
            ):
                excluded.append(summary | {
                    "source_name": source_path.name,
                    "reason": "obsolete_room_unit_contract",
                    "compiler_version": compiler_version,
                    "required_compiler_version": ROOM_UNIT_COMPILER_VERSION,
                })
                continue
        validate_room_compilation_unit(unit)

        if not composition_complete:
            excluded.append(summary | {
                "source_name": source_path.name,
                "reason": "composition_incomplete",
            })
            continue

        entrypoint = unit.get("entrypoint")
        if (
            not isinstance(initial_room_index, int)
            or not isinstance(entrypoint, dict)
            or not isinstance(entrypoint.get("entry"), dict)
        ):
            excluded.append(
                summary
                | {
                    "source_name": source_path.name,
                    "reason": "native_entrypoint_missing",
                }
            )
            continue

        resource_path = _archive_path(route_id, setup_index, digest)
        records.append(summary | {"resource_path": resource_path})
        payloads[resource_path] = unit

    records.sort(key=lambda row: (row["route_id"], row["setup_index"]))
    excluded.sort(key=lambda row: (row["route_id"], row["setup_index"]))
    return ({
        "format": "oot3d_room_compilation_unit_catalog_v1",
        "schema_version": 1,
        "status": "complete",
        "unit_count": len(records),
        "excluded_unit_count": len(excluded),
        "records": records,
        "excluded": excluded,
    }, payloads)
