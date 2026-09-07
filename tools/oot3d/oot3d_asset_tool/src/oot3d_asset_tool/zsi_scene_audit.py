from __future__ import annotations

import json
import re
from collections import Counter
from pathlib import Path

from .binary import ParseError
from .romfs_inventory import sorted_counter
from .zsi import ZsiFile

ROOM_ZSI_RE = re.compile(r"^(.+)_(\d+)(?:_dd)?_info\.zsi$", re.IGNORECASE)
SCENE_ZSI_RE = re.compile(r"^(.+?)(?:_dd)?_info\.zsi$", re.IGNORECASE)


def audit_zsi_scene_metadata(
    scene_root: Path,
    output_path: Path | None = None,
    *,
    sample_limit: int = 100,
    include_records: bool = True,
) -> dict[str, object]:
    if not scene_root.is_dir():
        raise ParseError(f"{scene_root}: expected an extracted OOT3D scene directory")

    records: list[dict[str, object]] = []
    sample_records: list[dict[str, object]] = []
    parse_errors: list[dict[str, object]] = []

    role_counts: Counter[str] = Counter()
    scene_file_counts_by_stem: Counter[str] = Counter()
    room_counts_by_scene: Counter[str] = Counter()
    setup_count_counts: Counter[str] = Counter()
    setup_count_by_role: Counter[str] = Counter()
    command_id_counts: Counter[str] = Counter()
    command_parameter_counts: Counter[str] = Counter()
    command_sequence_counts: Counter[str] = Counter()
    embedded_cmb_count_counts: Counter[str] = Counter()
    collision_candidate_count_counts: Counter[str] = Counter()

    zsi_file_count = 0
    setup_file_count = 0
    setup_total = 0
    command_total = 0
    collision_file_count = 0
    collision_candidate_total = 0
    camera_position_vector_total = 0
    water_box_total = 0
    bgcam_total = 0
    collision_vertex_total = 0
    collision_raw_polygon_total = 0
    collision_effective_polygon_total = 0
    collision_surface_type_total = 0

    for path in sorted(scene_root.rglob("*.zsi")):
        zsi_file_count += 1
        rel = path.relative_to(scene_root).as_posix()
        role = zsi_scene_role(path)
        stem = zsi_scene_stem(path)
        role_counts[role] += 1
        if role == "scene":
            scene_file_counts_by_stem[stem] += 1
        elif role == "room":
            room_counts_by_scene[stem] += 1

        setup_summaries: list[dict[str, object]] = []
        setups = []
        try:
            setups = ZsiFile.from_path(path).scene_setups()
        except Exception as exc:
            add_parse_error(parse_errors, rel, "scene_setup", exc)
        setup_count = len(setups)
        setup_count_counts[str(setup_count)] += 1
        setup_count_by_role[f"{role}:{setup_count}"] += 1
        if setup_count:
            setup_file_count += 1
            setup_total += setup_count

        for setup in setups:
            command_ids = []
            for command in setup.commands:
                command_id = f"0x{command.command_id:02x}"
                parameter = f"0x{command.parameter:02x}"
                command_ids.append(command_id)
                command_id_counts[command_id] += 1
                command_parameter_counts[f"{command_id}:{parameter}"] += 1
                command_total += 1
            command_sequence_counts[" ".join(command_ids)] += 1
            setup_summaries.append(
                {
                    "index": setup.index,
                    "offset": setup.offset,
                    "end_offset": setup.end_offset,
                    "command_count": len(setup.commands),
                    "command_ids": command_ids,
                }
            )

        embedded_cmb_count = 0
        try:
            embedded_cmb_count = len(ZsiFile.from_path(path).embedded_cmbs())
        except Exception as exc:
            add_parse_error(parse_errors, rel, "embedded_cmb", exc)
        embedded_cmb_count_counts[str(embedded_cmb_count)] += 1

        collision_summaries: list[dict[str, object]] = []
        collision_candidates = []
        try:
            collision_candidates = ZsiFile.from_path(path).collision_header_candidates()
        except Exception as exc:
            add_parse_error(parse_errors, rel, "collision_header_candidate", exc)
        collision_candidate_count = len(collision_candidates)
        collision_candidate_count_counts[str(collision_candidate_count)] += 1
        if collision_candidate_count:
            collision_file_count += 1
            collision_candidate_total += collision_candidate_count

        for candidate in collision_candidates:
            camera_position_vector_total += len(candidate.camera_position_vectors)
            water_box_total += candidate.water_box_count
            bgcam_total += candidate.bgcam_count
            collision_vertex_total += candidate.vertex_count
            collision_raw_polygon_total += candidate.raw_polygon_count
            collision_effective_polygon_total += candidate.effective_polygon_count
            collision_surface_type_total += candidate.surface_type_count
            collision_summaries.append(candidate.summary())

        record = {
            "path": rel,
            "role": role,
            "scene_stem": stem,
            "size": path.stat().st_size,
            "setup_count": setup_count,
            "command_count": sum(len(setup.commands) for setup in setups),
            "embedded_cmb_count": embedded_cmb_count,
            "collision_candidate_count": collision_candidate_count,
            "scene_setups": setup_summaries,
            "collision_header_candidates": collision_summaries,
        }
        if include_records:
            records.append(record)
        if len(sample_records) < sample_limit:
            sample_records.append(record)

    scene_file_multiplicity_counts = Counter(
        str(count) for count in scene_file_counts_by_stem.values()
    )
    room_count_counts = Counter(str(count) for count in room_counts_by_scene.values())

    audit: dict[str, object] = {
        "format": "oot3d_zsi_scene_metadata_audit_v1",
        "scene_root": str(scene_root),
        "zsi_file_count": zsi_file_count,
        "role_counts": sorted_counter(role_counts),
        "scene_stem_count": len(scene_file_counts_by_stem),
        "room_stem_count": len(room_counts_by_scene),
        "room_count_total": sum(room_counts_by_scene.values()),
        "scene_file_multiplicity_counts": sorted_counter(scene_file_multiplicity_counts),
        "room_count_counts": sorted_counter(room_count_counts),
        "room_counts_by_scene": sorted_counter(room_counts_by_scene),
        "setup_file_count": setup_file_count,
        "setup_total": setup_total,
        "setup_count_counts": sorted_counter(setup_count_counts),
        "setup_count_by_role": sorted_counter(setup_count_by_role),
        "command_total": command_total,
        "command_id_counts": sorted_counter(command_id_counts),
        "command_parameter_counts": sorted_counter(command_parameter_counts),
        "command_sequence_counts": sorted_counter(command_sequence_counts),
        "embedded_cmb_count_counts": sorted_counter(embedded_cmb_count_counts),
        "collision_candidate_count_counts": sorted_counter(collision_candidate_count_counts),
        "collision_file_count": collision_file_count,
        "collision_candidate_total": collision_candidate_total,
        "camera_position_vector_total": camera_position_vector_total,
        "water_box_total": water_box_total,
        "bgcam_total": bgcam_total,
        "collision_vertex_total": collision_vertex_total,
        "collision_raw_polygon_total": collision_raw_polygon_total,
        "collision_effective_polygon_total": collision_effective_polygon_total,
        "collision_surface_type_total": collision_surface_type_total,
        "parse_error_count": len(parse_errors),
        "parse_errors": parse_errors,
        "sample_records": sample_records,
        "records": records if include_records else [],
    }

    if output_path is not None:
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(
            json.dumps(audit, indent=2) + "\n",
            encoding="utf-8",
            newline="\n",
        )
    return audit


def zsi_scene_role(path: Path) -> str:
    name = path.name.lower()
    if ROOM_ZSI_RE.match(name):
        return "room"
    if SCENE_ZSI_RE.match(name):
        return "scene"
    return "other"


def zsi_scene_stem(path: Path) -> str:
    name = path.name.lower()
    room_match = ROOM_ZSI_RE.match(name)
    if room_match:
        return room_match.group(1)
    scene_match = SCENE_ZSI_RE.match(name)
    if scene_match:
        return scene_match.group(1)
    return path.stem.lower()


def add_parse_error(
    parse_errors: list[dict[str, object]],
    path: str,
    stage: str,
    exc: Exception,
) -> None:
    parse_errors.append(
        {
            "path": path,
            "stage": stage,
            "error": str(exc),
        }
    )
