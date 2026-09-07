#!/usr/bin/env python3
"""Build an intro/title camera timeline from OOT3D-native cutscene camera data."""

from __future__ import annotations

import csv
import json
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_BLOB_TABLE = ROOT / "analysis" / "scene_cutscene_camera_blob_table.json"
DEFAULT_CMAD_TABLE = ROOT / "analysis" / "scene_cutscene_camera_cmad_table.json"
DEFAULT_KEYFRAME_TABLE = ROOT / "analysis" / "scene_cutscene_camera_keyframe_table.json"
DEFAULT_MISC_ACTION_TABLE = ROOT / "analysis" / "scene_cutscene_misc_action_table.json"
DEFAULT_OUT_JSON = ROOT / "analysis" / "scene_cutscene_intro_camera_timeline.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "scene_cutscene_intro_camera_timeline.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "scene_cutscene_intro_camera_timeline.md"
DEFAULT_OUT_HEADER = ROOT / "include" / "oot3d" / "scene_cutscene_intro_camera_timeline.h"
DEFAULT_OUT_SOURCE = ROOT / "src" / "code" / "z_scene_cutscene_intro_camera_timeline.c"

INTRO_SCENE_PATHS = (
    "link_info.zsi",
    "spot04_info.zsi",
    "spot00_info.zsi",
    "spot99_info.zsi",
)
NO_STRT_LABEL = 0xFFFF


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


def c_string(value: Any) -> str:
    text = "" if value is None else str(value)
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def c_u8(value: Any) -> str:
    return f"{int_value(value) & 0xFF}u"


def c_u16(value: Any) -> str:
    return f"{int_value(value) & 0xFFFF}u"


def c_s32(value: Any) -> str:
    raw = int_value(value)
    return str(max(-2147483648, min(2147483647, raw)))


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as handle:
        json.dump(payload, handle, indent=2, sort_keys=True, allow_nan=False)
        handle.write("\n")


def csv_value(value: Any) -> str:
    if isinstance(value, list):
        return "; ".join(str(item) for item in value)
    if isinstance(value, dict):
        return json.dumps(value, sort_keys=True)
    return str(value)


def write_csv(path: Path, rows: list[dict[str, Any]], fieldnames: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(
            handle,
            fieldnames=fieldnames,
            extrasaction="ignore",
            lineterminator="\n",
        )
        writer.writeheader()
        for row in rows:
            writer.writerow({key: csv_value(row.get(key, "")) for key in fieldnames})


def build_label_index(
    strt_rows: list[dict[str, Any]],
    strt_label_rows: list[dict[str, Any]],
) -> dict[tuple[int, int, int], dict[str, Any]]:
    labels_by_strt: dict[int, list[dict[str, Any]]] = defaultdict(list)
    for label in strt_label_rows:
        labels_by_strt[int_value(label.get("strt_source_index"))].append(label)

    labels_by_blob_window: dict[tuple[int, int, int], dict[str, Any]] = {}
    for strt in strt_rows:
        blob_index = int_value(strt.get("camera_blob_source_index"), -1)
        for label in labels_by_strt.get(int_value(strt.get("strt_source_index")), []):
            if not label.get("parsed_label"):
                continue
            key = (
                blob_index,
                int_value(label.get("parsed_start_frame"), -1),
                int_value(label.get("parsed_end_frame"), -1),
            )
            labels_by_blob_window[key] = {
                "strt_source_index": int_value(strt.get("strt_source_index")),
                "strt_label_source_index": int_value(label.get("strt_label_source_index")),
                "strt_label": str(label.get("label", "")),
                "strt_parsed_camera_index": int_value(label.get("parsed_camera_index")),
                "strt_parsed_start_frame": int_value(label.get("parsed_start_frame")),
                "strt_parsed_end_frame": int_value(label.get("parsed_end_frame")),
                "strt_curve_role": str(strt.get("curve_role", "")),
            }
    return labels_by_blob_window


def group_counts(rows: list[dict[str, Any]], key_name: str) -> Counter[int]:
    counts: Counter[int] = Counter()
    for row in rows:
        counts[int_value(row.get(key_name), -1)] += 1
    return counts


def keyframe_counts_by_segment(
    curve_rows: list[dict[str, Any]],
    curve_keyframe_ref_rows: list[dict[str, Any]],
) -> Counter[int]:
    counts: Counter[int] = Counter()
    refs_by_curve = {
        int_value(ref.get("camera_curve_source_index")): int_value(ref.get("keyframe_ref_count"))
        for ref in curve_keyframe_ref_rows
    }
    for curve in curve_rows:
        counts[int_value(curve.get("camera_blob_segment_source_index"), -1)] += refs_by_curve.get(
            int_value(curve.get("camera_curve_source_index")),
            0,
        )
    return counts


def first_cmad_index_by_segment(rows: list[dict[str, Any]]) -> dict[int, int]:
    first: dict[int, int] = {}
    for row in rows:
        segment_index = int_value(row.get("camera_blob_segment_source_index"), -1)
        cmad_index = int_value(row.get("cmad_record_source_index"))
        if segment_index not in first or cmad_index < first[segment_index]:
            first[segment_index] = cmad_index
    return first


def build_table(
    blob_table_path: Path = DEFAULT_BLOB_TABLE,
    cmad_table_path: Path = DEFAULT_CMAD_TABLE,
    keyframe_table_path: Path = DEFAULT_KEYFRAME_TABLE,
    misc_action_table_path: Path = DEFAULT_MISC_ACTION_TABLE,
) -> dict[str, Any]:
    blob_table = load_json(blob_table_path)
    cmad_table = load_json(cmad_table_path)
    keyframe_table = load_json(keyframe_table_path)
    misc_action_table = load_json(misc_action_table_path)

    blob_rows = [as_dict(row) for row in as_list(blob_table.get("camera_blob_rows"))]
    segment_rows = [as_dict(row) for row in as_list(blob_table.get("camera_blob_segment_rows"))]
    cmad_rows = [as_dict(row) for row in as_list(cmad_table.get("cmad_record_rows"))]
    curve_rows = [as_dict(row) for row in as_list(cmad_table.get("camera_curve_rows"))]
    curve_keyframe_ref_rows = [
        as_dict(row) for row in as_list(keyframe_table.get("curve_keyframe_ref_rows"))
    ]
    strt_rows = [as_dict(row) for row in as_list(keyframe_table.get("strt_rows"))]
    strt_label_rows = [as_dict(row) for row in as_list(keyframe_table.get("strt_label_rows"))]
    misc_action_rows = [as_dict(row) for row in as_list(misc_action_table.get("misc_action_rows"))]

    intro_scene_paths = set(INTRO_SCENE_PATHS)
    intro_blob_rows = [
        row for row in blob_rows if str(row.get("scene_path", "")) in intro_scene_paths
    ]
    intro_blob_indices = {
        int_value(row.get("camera_blob_source_index"))
        for row in intro_blob_rows
    }
    labels_by_blob_window = build_label_index(strt_rows, strt_label_rows)
    cmad_count_by_segment = group_counts(cmad_rows, "camera_blob_segment_source_index")
    curve_count_by_segment = group_counts(curve_rows, "camera_blob_segment_source_index")
    keyframe_count_by_segment = keyframe_counts_by_segment(curve_rows, curve_keyframe_ref_rows)
    first_cmad_by_segment = first_cmad_index_by_segment(cmad_rows)

    timeline_rows: list[dict[str, Any]] = []
    errors: list[str] = []
    for segment in segment_rows:
        blob_index = int_value(segment.get("camera_blob_source_index"))
        if blob_index not in intro_blob_indices:
            continue
        start_frame = int_value(segment.get("start_frame"))
        end_frame = int_value(segment.get("end_frame"))
        label_key = (blob_index, start_frame, end_frame)
        label = labels_by_blob_window.get(label_key)
        if label is None:
            errors.append(
                "missing strt label for "
                f"blob {blob_index} segment {segment.get('camera_blob_segment_source_index')} "
                f"frames {start_frame}-{end_frame}"
            )
            label = {}

        segment_source_index = int_value(segment.get("camera_blob_segment_source_index"))
        cmad_count = cmad_count_by_segment[segment_source_index]
        curve_count = curve_count_by_segment[segment_source_index]
        keyframe_count = keyframe_count_by_segment[segment_source_index]
        if cmad_count == 0 or curve_count == 0 or keyframe_count == 0:
            errors.append(f"segment {segment_source_index}: empty cmad/curve/keyframe slice")

        timeline_rows.append(
            {
                "intro_camera_timeline_source_index": len(timeline_rows),
                "cutscene_source_index": int_value(segment.get("cutscene_source_index")),
                "native_command_source_index": int_value(segment.get("native_command_source_index")),
                "camera_blob_source_index": blob_index,
                "camera_blob_segment_source_index": segment_source_index,
                "scene_id": next(
                    (
                        int_value(blob.get("scene_id"), 0xFF)
                        for blob in intro_blob_rows
                        if int_value(blob.get("camera_blob_source_index")) == blob_index
                    ),
                    0xFF,
                ),
                "scene_path": str(segment.get("scene_path", "")),
                "setup_index": int_value(segment.get("setup_index")),
                "local_command_index": int_value(segment.get("local_command_index")),
                "segment_index": int_value(segment.get("segment_index")),
                "start_frame": start_frame,
                "end_frame": end_frame,
                "duration_frames": max(0, end_frame - start_frame),
                "native_active_condition": "start_frame < current_frame && current_frame < end_frame",
                "sample_frame": start_frame + 1 if start_frame + 1 < end_frame else start_frame,
                "first_cmad_record_source_index": first_cmad_by_segment.get(
                    segment_source_index,
                    0,
                ),
                "cmad_record_count": cmad_count,
                "curve_count": curve_count,
                "keyframe_count": keyframe_count,
                "strt_source_index": int_value(label.get("strt_source_index"), NO_STRT_LABEL),
                "strt_label_source_index": int_value(
                    label.get("strt_label_source_index"),
                    NO_STRT_LABEL,
                ),
                "strt_label": str(label.get("strt_label", "")),
                "strt_parsed_camera_index": int_value(
                    label.get("strt_parsed_camera_index"),
                    NO_STRT_LABEL,
                ),
                "strt_parsed_start_frame": int_value(label.get("strt_parsed_start_frame"), -1),
                "strt_parsed_end_frame": int_value(label.get("strt_parsed_end_frame"), -1),
                "strt_curve_role": str(label.get("strt_curve_role", "")),
            }
        )

    timeline_rows.sort(
        key=lambda row: (
            int_value(row.get("scene_path") != "link_info.zsi"),
            int_value(row.get("cutscene_source_index")),
            int_value(row.get("start_frame")),
        )
    )
    for index, row in enumerate(timeline_rows):
        row["intro_camera_timeline_source_index"] = index

    grouped_by_blob: dict[int, list[dict[str, Any]]] = defaultdict(list)
    for row in timeline_rows:
        grouped_by_blob[int_value(row.get("camera_blob_source_index"))].append(row)
    for blob_index, rows in grouped_by_blob.items():
        rows.sort(key=lambda row: int_value(row.get("segment_index")))
        expected_count = next(
            int_value(blob.get("segment_ref_count"))
            for blob in intro_blob_rows
            if int_value(blob.get("camera_blob_source_index")) == blob_index
        )
        if len(rows) != expected_count:
            errors.append(f"blob {blob_index}: segment count {len(rows)} != {expected_count}")
        for previous, current in zip(rows, rows[1:]):
            if int_value(previous.get("end_frame")) > int_value(current.get("start_frame")):
                errors.append(
                    f"blob {blob_index}: overlapping camera windows at "
                    f"{previous.get('strt_label')} and {current.get('strt_label')}"
                )

    cutscene_rows: list[dict[str, Any]] = []
    for cutscene_index in sorted({int_value(row.get("cutscene_source_index")) for row in timeline_rows}):
        camera_rows = [
            row for row in timeline_rows if int_value(row.get("cutscene_source_index")) == cutscene_index
        ]
        misc_rows = [
            row for row in misc_action_rows if int_value(row.get("cutscene_source_index")) == cutscene_index
        ]
        cutscene_rows.append(
            {
                "cutscene_source_index": cutscene_index,
                "scene_path": camera_rows[0]["scene_path"] if camera_rows else "",
                "setup_index": camera_rows[0]["setup_index"] if camera_rows else 0,
                "camera_timeline_ref_start": int_value(camera_rows[0]["intro_camera_timeline_source_index"])
                if camera_rows
                else 0,
                "camera_timeline_ref_count": len(camera_rows),
                "camera_frame_start": min(int_value(row.get("start_frame")) for row in camera_rows)
                if camera_rows
                else 0,
                "camera_frame_end": max(int_value(row.get("end_frame")) for row in camera_rows)
                if camera_rows
                else 0,
                "misc_action_count": len(misc_rows),
                "misc_actions": [
                    {
                        "action_id_hex": row.get("action_id_hex", ""),
                        "action_name": row.get("action_name", ""),
                        "start_frame": int_value(row.get("start_frame")),
                        "end_frame": int_value(row.get("end_frame")),
                    }
                    for row in misc_rows
                ],
            }
        )

    intro_cutscene_indices = {
        int_value(row.get("cutscene_source_index")) for row in timeline_rows
    }
    intro_misc_rows = [
        row
        for row in misc_action_rows
        if int_value(row.get("cutscene_source_index")) in intro_cutscene_indices
    ]
    summary = {
        "format": "oot3d_scene_cutscene_intro_camera_timeline_v1",
        "intro_scene_paths": list(INTRO_SCENE_PATHS),
        "camera_blob_count": len(intro_blob_rows),
        "camera_timeline_row_count": len(timeline_rows),
        "cutscene_count": len(cutscene_rows),
        "strt_label_match_count": sum(1 for row in timeline_rows if row.get("strt_label")),
        "misc_action_count": len(intro_misc_rows),
        "scene_counts": Counter(str(row.get("scene_path")) for row in timeline_rows),
        "native_active_condition": "start_frame < current_frame && current_frame < end_frame",
        "status": "pass" if not errors else "fail",
        "errors": errors,
    }

    return {
        "summary": summary,
        "source_policy": {
            "primary_source": "OOT3D native ZSI command 0x97 ccb/caad/mads/cmad data and decoded strt labels",
            "runtime_reference": "Cutscene_ProcessCommands camera blob handling and existing Oot3d_CutsceneCameraBuildRuntimeView",
            "strict_n64_policy": "N64 source is not used for this extraction.",
        },
        "cutscene_rows": cutscene_rows,
        "camera_timeline_rows": timeline_rows,
    }


def write_header(path: Path, row_count: int, cutscene_count: int) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "#ifndef OOT3D_SCENE_CUTSCENE_INTRO_CAMERA_TIMELINE_H",
        "#define OOT3D_SCENE_CUTSCENE_INTRO_CAMERA_TIMELINE_H",
        "",
        '#include "oot3d/scene_cutscene_camera_runtime.h"',
        "",
        "enum {",
        f"    OOT3D_SCENE_CUTSCENE_INTRO_CAMERA_TIMELINE_ROW_COUNT = {row_count},",
        f"    OOT3D_SCENE_CUTSCENE_INTRO_CAMERA_CUTSCENE_ROW_COUNT = {cutscene_count},",
        "    OOT3D_SCENE_CUTSCENE_INTRO_CAMERA_NO_STRT_LABEL = 0xFFFF,",
        "};",
        "",
        "typedef struct {",
        "    u16 introCameraTimelineSourceIndex;",
        "    u16 cutsceneSourceIndex;",
        "    u16 nativeCommandSourceIndex;",
        "    u16 cameraBlobSourceIndex;",
        "    u16 cameraBlobSegmentSourceIndex;",
        "    u8 sceneId;",
        "    u16 setupIndex;",
        "    u16 localCommandIndex;",
        "    u16 segmentIndex;",
        "    s32 startFrame;",
        "    s32 endFrame;",
        "    s32 sampleFrame;",
        "    u16 firstCmadRecordSourceIndex;",
        "    u16 cmadRecordCount;",
        "    u16 curveCount;",
        "    u16 keyframeCount;",
        "    u16 strtSourceIndex;",
        "    u16 strtLabelSourceIndex;",
        "    u16 strtParsedCameraIndex;",
        "    s32 strtParsedStartFrame;",
        "    s32 strtParsedEndFrame;",
        "    const char* scenePath;",
        "    const char* strtLabel;",
        "    const char* strtCurveRole;",
        "} Oot3dSceneCutsceneIntroCameraTimelineRow;",
        "",
        "typedef struct {",
        "    u16 cutsceneSourceIndex;",
        "    u16 cameraTimelineRefStart;",
        "    u16 cameraTimelineRefCount;",
        "    u16 miscActionCount;",
        "    u16 setupIndex;",
        "    s32 cameraFrameStart;",
        "    s32 cameraFrameEnd;",
        "    const char* scenePath;",
        "} Oot3dSceneCutsceneIntroCameraCutsceneRow;",
        "",
        "typedef enum {",
        "    OOT3D_CUTSCENE_INTRO_CAMERA_OK = 0,",
        "    OOT3D_CUTSCENE_INTRO_CAMERA_NULL_VIEW,",
        "    OOT3D_CUTSCENE_INTRO_CAMERA_NO_ACTIVE_TIMELINE_ROW,",
        "    OOT3D_CUTSCENE_INTRO_CAMERA_RUNTIME_FAILED,",
        "} Oot3dCutsceneIntroCameraStatus;",
        "",
        "extern const Oot3dSceneCutsceneIntroCameraTimelineRow oot3d_scene_cutscene_intro_camera_timeline_rows[];",
        "extern const Oot3dSceneCutsceneIntroCameraCutsceneRow oot3d_scene_cutscene_intro_camera_cutscene_rows[];",
        "extern const u32 oot3d_scene_cutscene_intro_camera_timeline_row_count;",
        "extern const u32 oot3d_scene_cutscene_intro_camera_cutscene_row_count;",
        "",
        "const Oot3dSceneCutsceneIntroCameraTimelineRow* Oot3d_CutsceneIntroCameraFindTimelineRow(",
        "    u16 cutsceneSourceIndex,",
        "    s32 frame",
        ");",
        "",
        "Oot3dCutsceneIntroCameraStatus Oot3d_CutsceneIntroCameraBuildView(",
        "    u16 cutsceneSourceIndex,",
        "    float frameTime,",
        "    const Oot3dCutsceneCameraViewPerturbation* perturbation,",
        "    Oot3dCutsceneCameraViewFrame* outView",
        ");",
        "",
        "#endif",
        "",
    ]
    path.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def write_source(path: Path, payload: dict[str, Any]) -> None:
    rows = [as_dict(row) for row in as_list(payload.get("camera_timeline_rows"))]
    cutscene_rows = [as_dict(row) for row in as_list(payload.get("cutscene_rows"))]
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "/* Generated by build_scene_cutscene_intro_camera_timeline.py. */",
        "",
        '#include "oot3d/scene_cutscene_intro_camera_timeline.h"',
        "",
        "#include <stddef.h>",
        "",
        "const Oot3dSceneCutsceneIntroCameraTimelineRow oot3d_scene_cutscene_intro_camera_timeline_rows[] = {",
    ]
    for row in rows:
        lines.append(
            "    { "
            f"{c_u16(row.get('intro_camera_timeline_source_index'))}, "
            f"{c_u16(row.get('cutscene_source_index'))}, "
            f"{c_u16(row.get('native_command_source_index'))}, "
            f"{c_u16(row.get('camera_blob_source_index'))}, "
            f"{c_u16(row.get('camera_blob_segment_source_index'))}, "
            f"{c_u8(row.get('scene_id'))}, "
            f"{c_u16(row.get('setup_index'))}, "
            f"{c_u16(row.get('local_command_index'))}, "
            f"{c_u16(row.get('segment_index'))}, "
            f"{c_s32(row.get('start_frame'))}, "
            f"{c_s32(row.get('end_frame'))}, "
            f"{c_s32(row.get('sample_frame'))}, "
            f"{c_u16(row.get('first_cmad_record_source_index'))}, "
            f"{c_u16(row.get('cmad_record_count'))}, "
            f"{c_u16(row.get('curve_count'))}, "
            f"{c_u16(row.get('keyframe_count'))}, "
            f"{c_u16(row.get('strt_source_index'))}, "
            f"{c_u16(row.get('strt_label_source_index'))}, "
            f"{c_u16(row.get('strt_parsed_camera_index'))}, "
            f"{c_s32(row.get('strt_parsed_start_frame'))}, "
            f"{c_s32(row.get('strt_parsed_end_frame'))}, "
            f"{c_string(row.get('scene_path'))}, "
            f"{c_string(row.get('strt_label'))}, "
            f"{c_string(row.get('strt_curve_role'))} "
            "},"
        )
    lines.extend(
        [
            "};",
            "",
            "const Oot3dSceneCutsceneIntroCameraCutsceneRow oot3d_scene_cutscene_intro_camera_cutscene_rows[] = {",
        ]
    )
    for row in cutscene_rows:
        lines.append(
            "    { "
            f"{c_u16(row.get('cutscene_source_index'))}, "
            f"{c_u16(row.get('camera_timeline_ref_start'))}, "
            f"{c_u16(row.get('camera_timeline_ref_count'))}, "
            f"{c_u16(row.get('misc_action_count'))}, "
            f"{c_u16(row.get('setup_index'))}, "
            f"{c_s32(row.get('camera_frame_start'))}, "
            f"{c_s32(row.get('camera_frame_end'))}, "
            f"{c_string(row.get('scene_path'))} "
            "},"
        )
    lines.extend(
        [
            "};",
            "",
            "const u32 oot3d_scene_cutscene_intro_camera_timeline_row_count = OOT3D_SCENE_CUTSCENE_INTRO_CAMERA_TIMELINE_ROW_COUNT;",
            "const u32 oot3d_scene_cutscene_intro_camera_cutscene_row_count = OOT3D_SCENE_CUTSCENE_INTRO_CAMERA_CUTSCENE_ROW_COUNT;",
            "",
            "const Oot3dSceneCutsceneIntroCameraTimelineRow* Oot3d_CutsceneIntroCameraFindTimelineRow(",
            "    u16 cutsceneSourceIndex,",
            "    s32 frame",
            ") {",
            "    u32 rowIndex;",
            "",
            "    for (rowIndex = 0; rowIndex < oot3d_scene_cutscene_intro_camera_timeline_row_count; rowIndex++) {",
            "        const Oot3dSceneCutsceneIntroCameraTimelineRow* row =",
            "            &oot3d_scene_cutscene_intro_camera_timeline_rows[rowIndex];",
            "",
            "        if (row->cutsceneSourceIndex != cutsceneSourceIndex) {",
            "            continue;",
            "        }",
            "        if (row->startFrame < frame && frame < row->endFrame) {",
            "            return row;",
            "        }",
            "    }",
            "",
            "    return NULL;",
            "}",
            "",
            "Oot3dCutsceneIntroCameraStatus Oot3d_CutsceneIntroCameraBuildView(",
            "    u16 cutsceneSourceIndex,",
            "    float frameTime,",
            "    const Oot3dCutsceneCameraViewPerturbation* perturbation,",
            "    Oot3dCutsceneCameraViewFrame* outView",
            ") {",
            "    const Oot3dSceneCutsceneIntroCameraTimelineRow* row;",
            "    Oot3dCutsceneCameraRuntimeStatus status;",
            "",
            "    if (outView == NULL) {",
            "        return OOT3D_CUTSCENE_INTRO_CAMERA_NULL_VIEW;",
            "    }",
            "",
            "    row = Oot3d_CutsceneIntroCameraFindTimelineRow(cutsceneSourceIndex, (s32)frameTime);",
            "    if (row == NULL) {",
            "        return OOT3D_CUTSCENE_INTRO_CAMERA_NO_ACTIVE_TIMELINE_ROW;",
            "    }",
            "",
            "    status = Oot3d_CutsceneCameraBuildRuntimeView(",
            "        row->cameraBlobSegmentSourceIndex,",
            "        frameTime,",
            "        perturbation,",
            "        outView",
            "    );",
            "    if (status != OOT3D_CUTSCENE_CAMERA_RUNTIME_OK) {",
            "        return OOT3D_CUTSCENE_INTRO_CAMERA_RUNTIME_FAILED;",
            "    }",
            "",
            "    return OOT3D_CUTSCENE_INTRO_CAMERA_OK;",
            "}",
            "",
        ]
    )
    path.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def write_markdown(path: Path, payload: dict[str, Any]) -> None:
    summary = as_dict(payload.get("summary"))
    rows = [as_dict(row) for row in as_list(payload.get("camera_timeline_rows"))]
    cutscene_rows = [as_dict(row) for row in as_list(payload.get("cutscene_rows"))]
    lines = [
        "# Scene Cutscene Intro Camera Timeline",
        "",
        "Native camera timeline for first-intro and title-screen target scenes, joined from command `0x97` camera blobs, `caad` segment windows, `mads/cmad` records, type-2 keyframes, and decoded `strt` labels.",
        "",
        "## Summary",
        "",
        f"- Target scenes: `{', '.join(summary['intro_scene_paths'])}`",
        f"- Camera blobs: {summary['camera_blob_count']}",
        f"- Cutscenes: {summary['cutscene_count']}",
        f"- Timeline rows: {summary['camera_timeline_row_count']}",
        f"- Matched `strt` labels: {summary['strt_label_match_count']}",
        f"- Intro misc actions available for the same cutscenes: {summary['misc_action_count']}",
        f"- Native active condition: `{summary['native_active_condition']}`",
        f"- Status: `{summary['status']}`",
        "",
        "## Runtime Use",
        "",
        "- `Oot3d_CutsceneIntroCameraFindTimelineRow(cutsceneSourceIndex, frame)` selects the active native segment by the verified strict frame window.",
        "- `Oot3d_CutsceneIntroCameraBuildView(...)` forwards the selected `cameraBlobSegmentSourceIndex` to `Oot3d_CutsceneCameraBuildRuntimeView`; it does not duplicate camera curve interpretation.",
        "- `strt` labels are annotations validated against segment frame windows. Segment/source indices remain the authoritative runtime link.",
        "",
        "## Cutscene Windows",
        "",
        "| cutscene | scene | setup | camera rows | frames | misc actions |",
        "| ---: | --- | ---: | ---: | --- | ---: |",
    ]
    for row in cutscene_rows:
        lines.append(
            f"| {row['cutscene_source_index']} | `{row['scene_path']}` | {row['setup_index']} | "
            f"{row['camera_timeline_ref_count']} | {row['camera_frame_start']}-{row['camera_frame_end']} | "
            f"{row['misc_action_count']} |"
        )
    lines.extend(
        [
            "",
            "## Timeline Rows",
            "",
            "| row | cutscene | scene | segment | frames | label | cmad | curves | keyframes |",
            "| ---: | ---: | --- | ---: | --- | --- | ---: | ---: | ---: |",
        ]
    )
    for row in rows:
        lines.append(
            f"| {row['intro_camera_timeline_source_index']} | {row['cutscene_source_index']} | "
            f"`{row['scene_path']}` | {row['camera_blob_segment_source_index']} | "
            f"{row['start_frame']}-{row['end_frame']} | `{row['strt_label']}` | "
            f"{row['cmad_record_count']} | {row['curve_count']} | {row['keyframe_count']} |"
        )
    if summary.get("errors"):
        lines.extend(["", "## Errors", ""])
        for error in summary["errors"]:
            lines.append(f"- {error}")
    lines.append("")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def main() -> int:
    payload = build_table()
    rows = [as_dict(row) for row in as_list(payload.get("camera_timeline_rows"))]
    cutscene_rows = [as_dict(row) for row in as_list(payload.get("cutscene_rows"))]
    write_json(DEFAULT_OUT_JSON, payload)
    write_csv(
        DEFAULT_OUT_CSV,
        rows,
        [
            "intro_camera_timeline_source_index",
            "cutscene_source_index",
            "native_command_source_index",
            "camera_blob_source_index",
            "camera_blob_segment_source_index",
            "scene_id",
            "scene_path",
            "setup_index",
            "local_command_index",
            "segment_index",
            "start_frame",
            "end_frame",
            "duration_frames",
            "native_active_condition",
            "sample_frame",
            "first_cmad_record_source_index",
            "cmad_record_count",
            "curve_count",
            "keyframe_count",
            "strt_source_index",
            "strt_label_source_index",
            "strt_label",
            "strt_parsed_camera_index",
            "strt_parsed_start_frame",
            "strt_parsed_end_frame",
            "strt_curve_role",
        ],
    )
    write_markdown(DEFAULT_OUT_MD, payload)
    write_header(DEFAULT_OUT_HEADER, len(rows), len(cutscene_rows))
    write_source(DEFAULT_OUT_SOURCE, payload)

    summary = as_dict(payload.get("summary"))
    if summary.get("status") != "pass":
        for error in as_list(summary.get("errors"))[:20]:
            print(f"error: {error}")
        return 1

    print(
        "built intro camera timeline: "
        f"{len(rows)} rows, {len(cutscene_rows)} cutscenes, "
        f"{summary.get('strt_label_match_count')} strt labels"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
