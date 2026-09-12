#!/usr/bin/env python3
"""Summarize native PICA shader/pipeline identities from one Azahar capture."""

from __future__ import annotations

import argparse
import hashlib
import json
from collections import Counter
from pathlib import Path
from typing import Any, Iterable


OUTPUT_FORMAT = "oot3d_azahar_shader_coverage_v1"
IDENTITY_FORMAT = "oot3d.azahar.pica_shader_identity.v1"


def canonical_id(prefix: str, value: Any) -> str:
    encoded = json.dumps(value, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return f"{prefix}_{hashlib.sha256(encoded).hexdigest()[:16]}"


def identity_record(identity: dict[str, Any]) -> tuple[tuple[Any, ...], tuple[Any, ...] | None, str]:
    if identity.get("format") != IDENTITY_FORMAT:
        raise ValueError(f"unsupported shader identity: {identity.get('format')!r}")
    vertex = identity["vertex"]
    vertex_key = (
        vertex["program_hash"],
        vertex["swizzle_hash"],
        int(vertex["entry_point"]),
        int(vertex["program_words"]),
        int(vertex["swizzle_words"]),
    )
    geometry = identity["geometry"]
    geometry_key = None
    if geometry["enabled"]:
        geometry_key = (
            geometry["program_hash"],
            geometry["swizzle_hash"],
            int(geometry["entry_point"]),
            int(geometry["program_words"]),
            int(geometry["swizzle_words"]),
        )
    return vertex_key, geometry_key, str(identity["fragment_config_hash"])


def counted_records(counter: Counter[Any], prefix: str) -> list[dict[str, Any]]:
    records = []
    for key, count in sorted(counter.items(), key=lambda item: repr(item[0])):
        records.append({"id": canonical_id(prefix, key), "identity": key, "draw_count": count})
    return records


def iter_events(path: Path) -> Iterable[dict[str, Any]]:
    with path.open("r", encoding="utf-8") as stream:
        for line_number, line in enumerate(stream, 1):
            if not line.strip():
                continue
            try:
                yield json.loads(line)
            except json.JSONDecodeError as error:
                raise ValueError(f"invalid JSONL at {path}:{line_number}: {error}") from error


def summarize(capture_summary_path: Path) -> dict[str, Any]:
    capture_summary = json.loads(capture_summary_path.read_text(encoding="utf-8"))
    if capture_summary.get("format") != "oot3d_azahar_coverage_capture_v1":
        raise ValueError("unsupported Azahar coverage capture summary")

    vertex_programs: Counter[Any] = Counter()
    geometry_programs: Counter[Any] = Counter()
    fragment_configs: Counter[str] = Counter()
    pipelines: Counter[Any] = Counter()
    texture_formats: Counter[Any] = Counter()
    shadow_states: Counter[Any] = Counter()
    draw_modes: Counter[str] = Counter()
    frame_records: list[dict[str, Any]] = []
    total_draws = 0
    seed_draws = program_payloads = lut_snapshots = 0

    for trace_name in capture_summary["pica_frames"]:
        trace_path = Path(trace_name)
        draw_count = 0
        frame_pipeline_ids = set()
        capture_complete = False
        has_program = has_luts = False
        for event in iter_events(trace_path):
            event_kind = event.get("event")
            if event_kind == "shader_seed_program":
                has_program = True
                program_payloads += 1
            elif event_kind == "shader_seed_luts":
                has_luts = True
                lut_snapshots += 1
            if event_kind == "capture_end":
                capture_complete = True
                continue
            if event_kind != "draw_begin":
                continue
            seeded = bool(event.get("shader_seed_resources"))
            if capture_summary.get("shader_seed_capture") and not (seeded and has_program and has_luts):
                raise ValueError(f"shader-seed capture lacks resources; check instrumented Azahar: {trace_path}")
            seed_draws += seeded
            identity = event.get("shader_identity")
            if not isinstance(identity, dict):
                raise ValueError(f"draw without native shader identity in {trace_path}")
            vertex_key, geometry_key, fragment_hash = identity_record(identity)
            topology = int(event["triangle_topology"])
            draw_mode = str(event["mode"])
            pipeline_key = (vertex_key, geometry_key, fragment_hash, topology, draw_mode)
            vertex_programs[vertex_key] += 1
            if geometry_key is not None:
                geometry_programs[geometry_key] += 1
            fragment_configs[fragment_hash] += 1
            pipelines[pipeline_key] += 1
            frame_pipeline_ids.add(canonical_id("pipeline", pipeline_key))
            draw_modes[draw_mode] += 1

            for texture in event.get("textures", []):
                if int(texture.get("enabled", 0)) != 0:
                    texture_formats[(int(texture["format"]), int(texture["type"]))] += 1
            shadow = event.get("shadow_summary")
            if isinstance(shadow, dict):
                shadow_key = tuple(sorted((str(key), value) for key, value in shadow.items()))
                shadow_states[shadow_key] += 1
            draw_count += 1
            total_draws += 1
        if not capture_complete:
            raise ValueError(f"PICA capture lacks capture_end: {trace_path}")
        frame_records.append({"path": str(trace_path.resolve()), "draw_count": draw_count,
                              "pipeline_ids": sorted(frame_pipeline_ids)})

    if total_draws == 0:
        raise ValueError("Azahar capture contains no draw_begin events")

    scenario = capture_summary["scenario"]
    windows = []
    seen_pipelines = set()
    for window in capture_summary.get("capture_windows", []):
        first, count = int(window["first_frame_index"]), int(window["frame_count"])
        if first < 0 or count < 1 or first + count > len(frame_records):
            raise ValueError("capture window references unavailable frames")
        observed = set().union(*(set(f["pipeline_ids"]) for f in frame_records[first:first + count]))
        windows.append({**window, "unique_pipelines": len(observed),
                        "new_pipeline_ids": sorted(observed - seen_pipelines)})
        seen_pipelines.update(observed)
    return {
        "format": OUTPUT_FORMAT,
        "evidence_role": "validation_only_not_runtime_input",
        "source_capture_summary": str(capture_summary_path.resolve()),
        "scenario_id": scenario["id"],
        "scene_id": int(scenario["scene_id"]),
        "entrance_index": int(scenario["entrance_index"]),
        "backend": capture_summary["backend"],
        "identity_basis": {
            "vertex": "Azahar ShaderSetup program/swizzle hashes plus native entry point",
            "fragment": "Azahar renderer FSConfig::Hash over native PICA state",
            "pipeline": "vertex, optional geometry, fragment config, topology and draw mode",
        },
        "counts": {
            "frames": len(frame_records),
            "draws": total_draws,
            "shader_seed_draws": seed_draws,
            "program_payloads": program_payloads,
            "lut_snapshots": lut_snapshots,
            "unique_vertex_programs": len(vertex_programs),
            "unique_geometry_programs": len(geometry_programs),
            "unique_fragment_configs": len(fragment_configs),
            "unique_pipelines": len(pipelines),
            "unique_enabled_texture_format_types": len(texture_formats),
            "unique_shadow_states": len(shadow_states),
        },
        "frames": frame_records,
        "capture_windows": windows,
        "vertex_programs": counted_records(vertex_programs, "vs"),
        "geometry_programs": counted_records(geometry_programs, "gs"),
        "fragment_configs": [
            {"id": key, "draw_count": count}
            for key, count in sorted(fragment_configs.items())
        ],
        "pipelines": counted_records(pipelines, "pipeline"),
        "enabled_texture_format_types": [
            {"format": key[0], "type": key[1], "draw_count": count}
            for key, count in sorted(texture_formats.items())
        ],
        "shadow_states": counted_records(shadow_states, "shadow"),
        "draw_modes": dict(sorted(draw_modes.items())),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--capture-summary", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    document = summarize(args.capture_summary)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")
    print(
        f"{document['scenario_id']}: {document['counts']['draws']} draws, "
        f"{document['counts']['unique_vertex_programs']} VS, "
        f"{document['counts']['unique_fragment_configs']} FS configs, "
        f"{document['counts']['unique_pipelines']} pipelines"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
