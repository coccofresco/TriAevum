#!/usr/bin/env python3
"""Match the Azahar title-intro slot-6 camera trace against native title camera rows."""

from __future__ import annotations

import json
import math
import sys
from collections import defaultdict
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
REPO_ROOT = ROOT.parents[2]
CAPTURE_ROOT = REPO_ROOT / "captures" / "azahar_pica"
DEFAULT_OUT_JSON = ROOT / "analysis" / "title_intro_slot6_camera_match.json"
DEFAULT_OUT_MD = ROOT / "analysis" / "title_intro_slot6_camera_match.md"
TITLE_SCENE_PATHS = {"spot00_info.zsi", "spot99_info.zsi"}

sys.path.insert(0, str(ROOT / "scripts"))
import verify_scene_cutscene_camera_runtime as camera_runtime  # noqa: E402


def as_dict(value: Any) -> dict[str, Any]:
    return value if isinstance(value, dict) else {}


def as_list(value: Any) -> list[Any]:
    return value if isinstance(value, list) else []


def int_value(value: Any, default: int = 0) -> int:
    try:
        if value is None:
            return default
        return int(value)
    except (TypeError, ValueError):
        return default


def float_value(value: Any, default: float = 0.0) -> float:
    try:
        if value is None:
            return default
        return float(value)
    except (TypeError, ValueError):
        return default


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as handle:
        json.dump(payload, handle, indent=2, sort_keys=True, allow_nan=False)
        handle.write("\n")


def latest_slot6_trace() -> Path:
    candidates = sorted(CAPTURE_ROOT.glob("title_intro_slot6_initial_*/derived/*.native_pica_register_trace.json"))
    if not candidates:
        raise FileNotFoundError(f"no title_intro_slot6_initial trace found below {CAPTURE_ROOT}")
    return candidates[-1]


def extract_emulator_camera_uniform(trace: dict[str, Any]) -> dict[str, Any]:
    for draw in as_list(as_dict(trace.get("frame_capture")).get("draw_events")):
        uniforms = as_dict(draw.get("vs_uniforms_f76_f86"))
        f76 = as_list(uniforms.get("f76"))
        f77 = as_list(uniforms.get("f77"))
        f78 = as_list(uniforms.get("f78"))
        if len(f76) < 4 or len(f77) < 4 or len(f78) < 4:
            continue
        eye = {
            "x": float_value(f76[3]),
            "y": float_value(f77[3]),
            "z": float_value(f78[3]),
        }
        if math.sqrt(eye["x"] * eye["x"] + eye["y"] * eye["y"] + eye["z"] * eye["z"]) < 1.0:
            continue
        return {
            "draw_index": int_value(draw.get("draw_index")),
            "eye_from_vsh_f76_f77_f78_w": eye,
            "basis": "Azahar PICA VSH uniforms f76.w/f77.w/f78.w from first non-zero title slot-6 draw",
        }
    raise ValueError("slot-6 trace did not contain a non-zero f76/f77/f78 camera uniform block")


def vec_distance(a: dict[str, float], b: dict[str, float]) -> float:
    return math.sqrt(sum((float_value(a[axis]) - float_value(b[axis])) ** 2 for axis in ("x", "y", "z")))


def build_match_rows(emulator_eye: dict[str, float]) -> list[dict[str, Any]]:
    blob_table = load_json(ROOT / "analysis" / "scene_cutscene_camera_blob_table.json")
    cmad_table = load_json(ROOT / "analysis" / "scene_cutscene_camera_cmad_table.json")
    keyframe_table = load_json(ROOT / "analysis" / "scene_cutscene_camera_keyframe_table.json")
    timeline_table = load_json(ROOT / "analysis" / "scene_cutscene_intro_camera_timeline.json")

    segments = [as_dict(row) for row in as_list(blob_table.get("camera_blob_segment_rows"))]
    cmads = [as_dict(row) for row in as_list(cmad_table.get("cmad_record_rows"))]
    curves = [as_dict(row) for row in as_list(cmad_table.get("camera_curve_rows"))]
    refs = [as_dict(row) for row in as_list(keyframe_table.get("curve_keyframe_ref_rows"))]
    keyframes = [as_dict(row) for row in as_list(keyframe_table.get("keyframe_rows"))]
    timeline_rows = [as_dict(row) for row in as_list(timeline_table.get("camera_timeline_rows"))]

    cmads_by_segment: dict[int, list[dict[str, Any]]] = defaultdict(list)
    for cmad in cmads:
        cmads_by_segment[int_value(cmad.get("camera_blob_segment_source_index"), -1)].append(cmad)

    timeline_by_segment = {
        int_value(row.get("camera_blob_segment_source_index"), -1): row
        for row in timeline_rows
        if row.get("scene_path") in TITLE_SCENE_PATHS
    }

    matches: list[dict[str, Any]] = []
    for segment in segments:
        if segment.get("scene_path") not in TITLE_SCENE_PATHS:
            continue
        start_frame = int_value(segment.get("start_frame"))
        end_frame = int_value(segment.get("end_frame"))
        if end_frame <= start_frame + 1:
            continue
        sample_frames = set(range(start_frame, end_frame + 1))
        timeline_row = timeline_by_segment.get(int_value(segment.get("camera_blob_segment_source_index"), -1))
        if timeline_row is not None:
            sample_frames.add(max(start_frame, min(end_frame, int_value(timeline_row.get("sample_frame")))))

        best_for_segment: dict[str, Any] | None = None
        for frame in sorted(sample_frames):
            state = camera_runtime.build_segment_state(segment, frame, cmads_by_segment, curves, refs, keyframes)
            view = camera_runtime.project_view(state)
            eye = {axis: float_value(view["eye"][axis]) for axis in ("x", "y", "z")}
            distance = vec_distance(eye, emulator_eye)
            row = {
                "cutscene_source_index": int_value(segment.get("cutscene_source_index")),
                "setup_index": int_value(segment.get("setup_index")),
                "camera_blob_segment_source_index": int_value(segment.get("camera_blob_segment_source_index")),
                "camera_blob_source_index": int_value(segment.get("camera_blob_source_index")),
                "segment_index": int_value(segment.get("segment_index")),
                "start_frame": start_frame,
                "end_frame": end_frame,
                "sample_frame": frame,
                "eye": eye,
                "eye_distance_to_emulator": distance,
                "scene_path": segment.get("scene_path", ""),
                "strt_label": timeline_row.get("strt_label", "") if timeline_row else "",
            }
            if best_for_segment is None or distance < best_for_segment["eye_distance_to_emulator"]:
                best_for_segment = row
        if best_for_segment is not None:
            matches.append(best_for_segment)

    matches.sort(key=lambda row: row["eye_distance_to_emulator"])
    return matches


def write_markdown(path: Path, payload: dict[str, Any]) -> None:
    summary = as_dict(payload.get("summary"))
    rows = as_list(payload.get("matches"))[:20]
    lines = [
        "# Title Intro Slot 6 Camera Match",
        "",
        "This report compares the first non-zero Azahar slot-6 PICA vertex-shader camera uniform against native title-scene command `0x97` camera segments decoded from OOT3D assets.",
        "",
        "## Summary",
        "",
        f"- Trace: `{summary.get('trace_path', '')}`",
        f"- Emulator draw index: {summary.get('emulator_draw_index')}",
        f"- Best cutscene source: {summary.get('best_cutscene_source_index')}",
        f"- Best setup: {summary.get('best_setup_index')}",
        f"- Best segment: {summary.get('best_camera_blob_segment_source_index')}",
        f"- Best eye distance: {summary.get('best_eye_distance_to_emulator')}",
        "",
        "## Matches",
        "",
        "| rank | cutscene | setup | segment | frames | sample | eye distance | eye | label |",
        "| ---: | ---: | ---: | ---: | --- | ---: | ---: | --- | --- |",
    ]
    for rank, row in enumerate(rows, 1):
        eye = row["eye"]
        lines.append(
            f"| {rank} | {row['cutscene_source_index']} | {row['setup_index']} | "
            f"{row['camera_blob_segment_source_index']} | {row['start_frame']}-{row['end_frame']} | "
            f"{row['sample_frame']} | {row['eye_distance_to_emulator']:.6f} | "
            f"({eye['x']:.3f}, {eye['y']:.3f}, {eye['z']:.3f}) | `{row['strt_label']}` |"
        )
    lines.append("")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def main() -> int:
    trace_path = latest_slot6_trace()
    trace = load_json(trace_path)
    emulator_camera = extract_emulator_camera_uniform(trace)
    matches = build_match_rows(emulator_camera["eye_from_vsh_f76_f77_f78_w"])
    best = matches[0] if matches else {}
    payload = {
        "summary": {
            "format": "oot3d_title_intro_slot6_camera_match_v1",
            "status": "pass" if matches else "fail",
            "trace_path": str(trace_path),
            "emulator_draw_index": emulator_camera["draw_index"],
            "emulator_camera_uniform": emulator_camera,
            "best_cutscene_source_index": best.get("cutscene_source_index"),
            "best_setup_index": best.get("setup_index"),
            "best_camera_blob_segment_source_index": best.get("camera_blob_segment_source_index"),
            "best_eye_distance_to_emulator": best.get("eye_distance_to_emulator"),
            "match_basis": "Native camera runtime CMAD/keyframe evaluation, ranked by f76/f77/f78 uniform eye distance.",
        },
        "matches": matches,
    }
    write_json(DEFAULT_OUT_JSON, payload)
    write_markdown(DEFAULT_OUT_MD, payload)
    print(
        "matched title slot6 camera: "
        f"cutscene {best.get('cutscene_source_index')} setup {best.get('setup_index')} "
        f"segment {best.get('camera_blob_segment_source_index')} "
        f"distance {best.get('eye_distance_to_emulator'):.6f}"
    )
    return 0 if matches else 1


if __name__ == "__main__":
    raise SystemExit(main())
