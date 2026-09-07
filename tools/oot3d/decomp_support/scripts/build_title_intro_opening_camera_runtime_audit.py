#!/usr/bin/env python3
"""Build the OOT3D open-title camera runtime bridge audit."""

from __future__ import annotations

import csv
import json
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
ANALYSIS = ROOT / "analysis"

ORCHESTRATION = ANALYSIS / "title_intro_opening_orchestration.json"
SLOT6_MATCH = ANALYSIS / "title_intro_slot6_camera_match.json"
CAMERA_RUNTIME_HEADER = ROOT / "include" / "oot3d" / "scene_cutscene_camera_runtime.h"
CAMERA_RUNTIME_SOURCE = ROOT / "src" / "code" / "z_scene_cutscene_camera_runtime.c"
CMAD_APPLY_SOURCE = ROOT / "src" / "code" / "z_scene_cutscene_camera_cmad_apply.c"
CURVE_EVAL_SOURCE = ROOT / "src" / "code" / "z_scene_cutscene_camera_curve_eval.c"

OUT_JSON = ANALYSIS / "title_intro_opening_camera_runtime_audit.json"
OUT_MD = ANALYSIS / "title_intro_opening_camera_runtime_audit.md"
OUT_CSV = ANALYSIS / "title_intro_opening_camera_runtime_reference.csv"
OUT_HEADER = ROOT / "include" / "oot3d" / "title_intro_opening_camera_runtime.h"
OUT_SOURCE = ROOT / "src" / "code" / "z_title_intro_opening_camera_runtime.c"


def read_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace") if path.is_file() else ""


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")


def write_json(path: Path, data: Any) -> None:
    write_text(path, json.dumps(data, indent=2) + "\n")


def write_csv(path: Path, rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as handle:
        if not rows:
            return
        writer = csv.DictWriter(handle, fieldnames=list(rows[0].keys()), lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


def rel(path: Path) -> str:
    try:
        return path.relative_to(ROOT).as_posix()
    except ValueError:
        return path.as_posix()


def c_string(value: Any) -> str:
    text = "" if value is None else str(value)
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def c_u16(value: Any) -> str:
    return f"{int(value) & 0xFFFF}u"


def c_s32(value: Any) -> str:
    return str(int(value))


def c_float(value: Any) -> str:
    text = f"{float(value):.9g}"
    if "e" not in text.lower() and "." not in text:
        text += ".0"
    return text + "f"


def build_reference_row(orchestration: dict[str, Any], slot6: dict[str, Any]) -> dict[str, Any]:
    row = orchestration["orchestration_rows"][0]
    best = slot6["matches"][0]
    return {
        "reference_index": 0,
        "orchestration_index": row["orchestration_index"],
        "scene_path": row["scene_path"],
        "setup_index": row["setup_index"],
        "cutscene_source_index": row["cutscene_source_index"],
        "camera_blob_source_index": row["camera_blob_source_index"],
        "camera_blob_segment_source_index": row["camera_blob_segment_source_index"],
        "camera_cmad_record_ref_start": row["camera_cmad_record_ref_start"],
        "camera_cmad_record_ref_count": row["camera_cmad_record_ref_count"],
        "camera_curve_ref_start": row["camera_curve_ref_start"],
        "camera_curve_ref_count": row["camera_curve_ref_count"],
        "segment_start_frame": best["start_frame"],
        "segment_end_frame": best["end_frame"],
        "sample_frame": best["sample_frame"],
        "slot6_eye_x": best["eye"]["x"],
        "slot6_eye_y": best["eye"]["y"],
        "slot6_eye_z": best["eye"]["z"],
        "slot6_eye_distance_to_emulator": best["eye_distance_to_emulator"],
        "trace_path": slot6["summary"]["trace_path"],
        "basis": (
            "native spot00_info.zsi command 0x97 CMAD segment selected by "
            "title_intro_slot6_camera_match against Azahar PICA camera uniform"
        ),
    }


def build_report() -> dict[str, Any]:
    orchestration = read_json(ORCHESTRATION)
    slot6 = read_json(SLOT6_MATCH)
    reference = build_reference_row(orchestration, slot6)
    camera_runtime_header = read_text(CAMERA_RUNTIME_HEADER)
    camera_runtime_source = read_text(CAMERA_RUNTIME_SOURCE)
    cmad_apply_source = read_text(CMAD_APPLY_SOURCE)
    curve_eval_source = read_text(CURVE_EVAL_SOURCE)
    checks = {
        "orchestration_audit_ok": bool(orchestration["summary"]["ok"]),
        "slot6_match_status_pass": slot6["summary"]["status"] == "pass",
        "reference_uses_orchestration_segment": reference["camera_blob_segment_source_index"]
        == orchestration["orchestration_rows"][0]["camera_blob_segment_source_index"],
        "reference_uses_orchestration_blob": reference["camera_blob_source_index"]
        == orchestration["orchestration_rows"][0]["camera_blob_source_index"],
        "camera_curve_ref_count_is_6": reference["camera_curve_ref_count"] == 6,
        "cmad_record_ref_count_is_2": reference["camera_cmad_record_ref_count"] == 2,
        "runtime_header_exposes_build_view": "Oot3d_CutsceneCameraBuildRuntimeView" in camera_runtime_header,
        "runtime_source_builds_segment_state": "Oot3d_CutsceneCameraBuildSegmentState" in camera_runtime_source,
        "cmad_apply_uses_native_output_offsets": "case OOT3D_CUTSCENE_CAMERA_FIELD_8C" in cmad_apply_source
        and "case OOT3D_CUTSCENE_CAMERA_FIELD_80" in cmad_apply_source,
        "curve_eval_uses_hermite_type2": "Oot3d_CutsceneCameraInterpolateHermite" in curve_eval_source
        and "OOT3D_SCENE_CUTSCENE_CAMERA_CURVE_TYPE_HERMITE" in curve_eval_source,
    }
    return {
        "format": "oot3d_title_intro_opening_camera_runtime_audit_v1",
        "inputs": {
            "orchestration": rel(ORCHESTRATION),
            "slot6_match": rel(SLOT6_MATCH),
            "camera_runtime_header": rel(CAMERA_RUNTIME_HEADER),
            "camera_runtime_source": rel(CAMERA_RUNTIME_SOURCE),
            "cmad_apply_source": rel(CMAD_APPLY_SOURCE),
            "curve_eval_source": rel(CURVE_EVAL_SOURCE),
        },
        "summary": {
            "ok": all(checks.values()),
            "checks": checks,
            "next_gate": (
                "Attach the open-title camera sample to the engine intro scene, then bind "
                "the remaining logo scheduler and mounted transform consumer."
            ),
        },
        "reference_rows": [reference],
        "camera_curve_refs": orchestration["camera_curve_refs"],
        "camera_cmad_record_refs": orchestration["camera_cmad_record_refs"],
        "unresolved": [
            "This wrapper samples the native CMAD camera and projects the view; it does not yet drive the engine render camera.",
            "Camera perturbation/quake input is passed through as an optional native runtime parameter and is disabled for slot-6 validation.",
            "Logo scheduling and Link/Epona mounted transform ownership remain separate open-title runtime gates.",
        ],
    }


def write_header(path: Path, data: dict[str, Any]) -> None:
    lines = [
        "#ifndef OOT3D_TITLE_INTRO_OPENING_CAMERA_RUNTIME_H",
        "#define OOT3D_TITLE_INTRO_OPENING_CAMERA_RUNTIME_H",
        "",
        '#include "oot3d/scene_cutscene_camera_runtime.h"',
        '#include "oot3d/title_intro_opening_orchestration.h"',
        '#include "oot3d/types.h"',
        "",
        "typedef enum {",
        "    OOT3D_TITLE_INTRO_OPENING_CAMERA_RUNTIME_OK = 0,",
        "    OOT3D_TITLE_INTRO_OPENING_CAMERA_RUNTIME_NULL_OUTPUT,",
        "    OOT3D_TITLE_INTRO_OPENING_CAMERA_RUNTIME_MISSING_ORCHESTRATION,",
        "    OOT3D_TITLE_INTRO_OPENING_CAMERA_RUNTIME_APPLY_FAILED,",
        "    OOT3D_TITLE_INTRO_OPENING_CAMERA_RUNTIME_PROJECT_FAILED,",
        "} Oot3dTitleIntroOpeningCameraRuntimeStatus;",
        "",
        "typedef struct Oot3dTitleIntroOpeningCameraReferenceRow {",
        "    u16 referenceIndex;",
        "    u16 orchestrationIndex;",
        "    u16 setupIndex;",
        "    u16 cutsceneSourceIndex;",
        "    u16 cameraBlobSourceIndex;",
        "    u16 cameraBlobSegmentSourceIndex;",
        "    u16 cameraCmadRecordRefStart;",
        "    u16 cameraCmadRecordRefCount;",
        "    u16 cameraCurveRefStart;",
        "    u16 cameraCurveRefCount;",
        "    s32 segmentStartFrame;",
        "    s32 segmentEndFrame;",
        "    s32 sampleFrame;",
        "    float slot6EyeX;",
        "    float slot6EyeY;",
        "    float slot6EyeZ;",
        "    float slot6EyeDistanceToEmulator;",
        "    const char* scenePath;",
        "    const char* tracePath;",
        "    const char* basis;",
        "} Oot3dTitleIntroOpeningCameraReferenceRow;",
        "",
        "typedef struct Oot3dTitleIntroOpeningCameraSample {",
        "    float frame;",
        "    u16 orchestrationIndex;",
        "    u16 cameraBlobSourceIndex;",
        "    u16 cameraBlobSegmentSourceIndex;",
        "    u8 hasSlot6Reference;",
        "    float slot6ReferenceDistance;",
        "    float slot6EyeDistanceToEmulator;",
        "    Oot3dCutsceneCameraState csParams;",
        "    Oot3dCutsceneCameraViewFrame view;",
        "} Oot3dTitleIntroOpeningCameraSample;",
        "",
        "extern const Oot3dTitleIntroOpeningCameraReferenceRow gOot3dTitleIntroOpeningCameraReferenceRows[];",
        "extern const u32 gOot3dTitleIntroOpeningCameraReferenceRowCount;",
        "",
        "const Oot3dTitleIntroOpeningCameraReferenceRow* Oot3d_TitleIntroOpeningCameraRuntimeGetReference(u16 referenceIndex);",
        "Oot3dTitleIntroOpeningCameraRuntimeStatus Oot3d_TitleIntroOpeningCameraRuntimeBuildState(",
        "    u16 orchestrationIndex,",
        "    float frame,",
        "    Oot3dCutsceneCameraState* outState",
        ");",
        "Oot3dTitleIntroOpeningCameraRuntimeStatus Oot3d_TitleIntroOpeningCameraRuntimeBuildView(",
        "    u16 orchestrationIndex,",
        "    float frame,",
        "    const Oot3dCutsceneCameraViewPerturbation* perturbation,",
        "    Oot3dCutsceneCameraViewFrame* outView",
        ");",
        "Oot3dTitleIntroOpeningCameraRuntimeStatus Oot3d_TitleIntroOpeningCameraRuntimeSample(",
        "    u16 orchestrationIndex,",
        "    float frame,",
        "    const Oot3dCutsceneCameraViewPerturbation* perturbation,",
        "    Oot3dTitleIntroOpeningCameraSample* outSample",
        ");",
        "Oot3dTitleIntroOpeningCameraRuntimeStatus Oot3d_TitleIntroOpeningCameraRuntimeSampleSlot6Initial(",
        "    Oot3dTitleIntroOpeningCameraSample* outSample",
        ");",
        "",
        "#endif",
    ]
    write_text(path, "\n".join(lines) + "\n")


def write_source(path: Path, data: dict[str, Any]) -> None:
    lines = [
        '#include "oot3d/title_intro_opening_camera_runtime.h"',
        "",
        "#include <math.h>",
        "#include <string.h>",
        "",
        "const Oot3dTitleIntroOpeningCameraReferenceRow gOot3dTitleIntroOpeningCameraReferenceRows[] = {",
    ]
    for row in data["reference_rows"]:
        values = [
            c_u16(row["reference_index"]),
            c_u16(row["orchestration_index"]),
            c_u16(row["setup_index"]),
            c_u16(row["cutscene_source_index"]),
            c_u16(row["camera_blob_source_index"]),
            c_u16(row["camera_blob_segment_source_index"]),
            c_u16(row["camera_cmad_record_ref_start"]),
            c_u16(row["camera_cmad_record_ref_count"]),
            c_u16(row["camera_curve_ref_start"]),
            c_u16(row["camera_curve_ref_count"]),
            c_s32(row["segment_start_frame"]),
            c_s32(row["segment_end_frame"]),
            c_s32(row["sample_frame"]),
            c_float(row["slot6_eye_x"]),
            c_float(row["slot6_eye_y"]),
            c_float(row["slot6_eye_z"]),
            c_float(row["slot6_eye_distance_to_emulator"]),
            c_string(row["scene_path"]),
            c_string(row["trace_path"]),
            c_string(row["basis"]),
        ]
        lines.append("    { " + ", ".join(values) + " },")
    lines.extend(
        [
            "};",
            "const u32 gOot3dTitleIntroOpeningCameraReferenceRowCount = sizeof(gOot3dTitleIntroOpeningCameraReferenceRows) / sizeof(gOot3dTitleIntroOpeningCameraReferenceRows[0]);",
            "",
            "static float Oot3d_TitleIntroOpeningCameraSquare(float value) {",
            "    return value * value;",
            "}",
            "",
            "static float Oot3d_TitleIntroOpeningCameraDistance3(",
            "    float ax,",
            "    float ay,",
            "    float az,",
            "    float bx,",
            "    float by,",
            "    float bz",
            ") {",
            "    return sqrtf(",
            "        Oot3d_TitleIntroOpeningCameraSquare(ax - bx) +",
            "        Oot3d_TitleIntroOpeningCameraSquare(ay - by) +",
            "        Oot3d_TitleIntroOpeningCameraSquare(az - bz)",
            "    );",
            "}",
            "",
            "const Oot3dTitleIntroOpeningCameraReferenceRow* Oot3d_TitleIntroOpeningCameraRuntimeGetReference(u16 referenceIndex) {",
            "    if (referenceIndex >= gOot3dTitleIntroOpeningCameraReferenceRowCount) {",
            "        return 0;",
            "    }",
            "    return &gOot3dTitleIntroOpeningCameraReferenceRows[referenceIndex];",
            "}",
            "",
            "Oot3dTitleIntroOpeningCameraRuntimeStatus Oot3d_TitleIntroOpeningCameraRuntimeBuildState(",
            "    u16 orchestrationIndex,",
            "    float frame,",
            "    Oot3dCutsceneCameraState* outState",
            ") {",
            "    const Oot3dTitleIntroOpeningOrchestrationRow* orchestration;",
            "    Oot3dCutsceneCameraApplyStatus applyStatus;",
            "",
            "    if (outState == 0) {",
            "        return OOT3D_TITLE_INTRO_OPENING_CAMERA_RUNTIME_NULL_OUTPUT;",
            "    }",
            "    orchestration = Oot3d_TitleIntroOpeningGetOrchestrationRow(orchestrationIndex);",
            "    if (orchestration == 0) {",
            "        memset(outState, 0, sizeof(*outState));",
            "        return OOT3D_TITLE_INTRO_OPENING_CAMERA_RUNTIME_MISSING_ORCHESTRATION;",
            "    }",
            "    applyStatus = Oot3d_CutsceneCameraBuildSegmentState(",
            "        orchestration->cameraBlobSegmentSourceIndex,",
            "        frame,",
            "        outState",
            "    );",
            "    if (applyStatus != OOT3D_CUTSCENE_CAMERA_APPLY_OK) {",
            "        return OOT3D_TITLE_INTRO_OPENING_CAMERA_RUNTIME_APPLY_FAILED;",
            "    }",
            "    return OOT3D_TITLE_INTRO_OPENING_CAMERA_RUNTIME_OK;",
            "}",
            "",
            "Oot3dTitleIntroOpeningCameraRuntimeStatus Oot3d_TitleIntroOpeningCameraRuntimeBuildView(",
            "    u16 orchestrationIndex,",
            "    float frame,",
            "    const Oot3dCutsceneCameraViewPerturbation* perturbation,",
            "    Oot3dCutsceneCameraViewFrame* outView",
            ") {",
            "    Oot3dCutsceneCameraState state;",
            "    Oot3dTitleIntroOpeningCameraRuntimeStatus status;",
            "    Oot3dCutsceneCameraRuntimeStatus viewStatus;",
            "",
            "    if (outView == 0) {",
            "        return OOT3D_TITLE_INTRO_OPENING_CAMERA_RUNTIME_NULL_OUTPUT;",
            "    }",
            "    status = Oot3d_TitleIntroOpeningCameraRuntimeBuildState(orchestrationIndex, frame, &state);",
            "    if (status != OOT3D_TITLE_INTRO_OPENING_CAMERA_RUNTIME_OK) {",
            "        memset(outView, 0, sizeof(*outView));",
            "        return status;",
            "    }",
            "    viewStatus = Oot3d_CutsceneCameraProjectView(&state, perturbation, outView);",
            "    if (viewStatus != OOT3D_CUTSCENE_CAMERA_RUNTIME_OK) {",
            "        return OOT3D_TITLE_INTRO_OPENING_CAMERA_RUNTIME_PROJECT_FAILED;",
            "    }",
            "    return OOT3D_TITLE_INTRO_OPENING_CAMERA_RUNTIME_OK;",
            "}",
            "",
            "Oot3dTitleIntroOpeningCameraRuntimeStatus Oot3d_TitleIntroOpeningCameraRuntimeSample(",
            "    u16 orchestrationIndex,",
            "    float frame,",
            "    const Oot3dCutsceneCameraViewPerturbation* perturbation,",
            "    Oot3dTitleIntroOpeningCameraSample* outSample",
            ") {",
            "    const Oot3dTitleIntroOpeningOrchestrationRow* orchestration;",
            "    const Oot3dTitleIntroOpeningCameraReferenceRow* reference;",
            "    Oot3dTitleIntroOpeningCameraRuntimeStatus status;",
            "    Oot3dCutsceneCameraRuntimeStatus viewStatus;",
            "",
            "    if (outSample == 0) {",
            "        return OOT3D_TITLE_INTRO_OPENING_CAMERA_RUNTIME_NULL_OUTPUT;",
            "    }",
            "    memset(outSample, 0, sizeof(*outSample));",
            "    outSample->frame = frame;",
            "    outSample->orchestrationIndex = orchestrationIndex;",
            "    orchestration = Oot3d_TitleIntroOpeningGetOrchestrationRow(orchestrationIndex);",
            "    if (orchestration == 0) {",
            "        return OOT3D_TITLE_INTRO_OPENING_CAMERA_RUNTIME_MISSING_ORCHESTRATION;",
            "    }",
            "    outSample->cameraBlobSourceIndex = orchestration->cameraBlobSourceIndex;",
            "    outSample->cameraBlobSegmentSourceIndex = orchestration->cameraBlobSegmentSourceIndex;",
            "    status = Oot3d_TitleIntroOpeningCameraRuntimeBuildState(",
            "        orchestrationIndex,",
            "        frame,",
            "        &outSample->csParams",
            "    );",
            "    if (status != OOT3D_TITLE_INTRO_OPENING_CAMERA_RUNTIME_OK) {",
            "        return status;",
            "    }",
            "    viewStatus = Oot3d_CutsceneCameraProjectView(&outSample->csParams, perturbation, &outSample->view);",
            "    if (viewStatus != OOT3D_CUTSCENE_CAMERA_RUNTIME_OK) {",
            "        return OOT3D_TITLE_INTRO_OPENING_CAMERA_RUNTIME_PROJECT_FAILED;",
            "    }",
            "    reference = Oot3d_TitleIntroOpeningCameraRuntimeGetReference(0);",
            "    if (reference != 0 && reference->orchestrationIndex == orchestrationIndex &&",
            "        (s32)frame == reference->sampleFrame) {",
            "        outSample->hasSlot6Reference = 1u;",
            "        outSample->slot6ReferenceDistance = Oot3d_TitleIntroOpeningCameraDistance3(",
            "            outSample->view.eye.x,",
            "            outSample->view.eye.y,",
            "            outSample->view.eye.z,",
            "            reference->slot6EyeX,",
            "            reference->slot6EyeY,",
            "            reference->slot6EyeZ",
            "        );",
            "        outSample->slot6EyeDistanceToEmulator = reference->slot6EyeDistanceToEmulator;",
            "    }",
            "    return OOT3D_TITLE_INTRO_OPENING_CAMERA_RUNTIME_OK;",
            "}",
            "",
            "Oot3dTitleIntroOpeningCameraRuntimeStatus Oot3d_TitleIntroOpeningCameraRuntimeSampleSlot6Initial(",
            "    Oot3dTitleIntroOpeningCameraSample* outSample",
            ") {",
            "    const Oot3dTitleIntroOpeningCameraReferenceRow* reference =",
            "        Oot3d_TitleIntroOpeningCameraRuntimeGetReference(0);",
            "    if (reference == 0) {",
            "        if (outSample != 0) {",
            "            memset(outSample, 0, sizeof(*outSample));",
            "        }",
            "        return OOT3D_TITLE_INTRO_OPENING_CAMERA_RUNTIME_MISSING_ORCHESTRATION;",
            "    }",
            "    return Oot3d_TitleIntroOpeningCameraRuntimeSample(",
            "        reference->orchestrationIndex,",
            "        (float)reference->sampleFrame,",
            "        0,",
            "        outSample",
            "    );",
            "}",
        ]
    )
    write_text(path, "\n".join(lines) + "\n")


def write_markdown(path: Path, data: dict[str, Any]) -> None:
    ref = data["reference_rows"][0]
    lines = [
        "# OOT3D Open Title Camera Runtime Audit",
        "",
        "This audit promotes the open-title camera bridge from the orchestration row into the native CMAD camera runtime.",
        "",
        "## Summary",
        "",
        f"- OK: {data['summary']['ok']}",
        f"- Next gate: {data['summary']['next_gate']}",
        "",
        "## Checks",
        "",
        "| Check | Status |",
        "| --- | --- |",
    ]
    for key, value in data["summary"]["checks"].items():
        lines.append(f"| `{key}` | `{value}` |")
    lines.extend(
        [
            "",
            "## Slot 6 Reference",
            "",
            "| Field | Value |",
            "| --- | --- |",
            f"| Scene | `{ref['scene_path']}` |",
            f"| Setup | `{ref['setup_index']}` |",
            f"| Cutscene | `{ref['cutscene_source_index']}` |",
            f"| Camera blob/segment | `{ref['camera_blob_source_index']}` / `{ref['camera_blob_segment_source_index']}` |",
            f"| Segment frames | `{ref['segment_start_frame']}..{ref['segment_end_frame']}` |",
            f"| Sample frame | `{ref['sample_frame']}` |",
            f"| Reference eye | `{ref['slot6_eye_x']:.9f}, {ref['slot6_eye_y']:.9f}, {ref['slot6_eye_z']:.9f}` |",
            f"| Distance to emulator trace | `{ref['slot6_eye_distance_to_emulator']:.9f}` |",
            "",
            "## Camera Curves",
            "",
            "| Ref | Channel | Slot | Output | Points | Role |",
            "| ---: | ---: | ---: | --- | ---: | --- |",
        ]
    )
    for row in data["camera_curve_refs"]:
        lines.append(
            f"| `{row['camera_curve_ref_index']}` | `{row['channel_type']}` | `{row['curve_slot_index']}` | "
            f"`0x{int(row['output_field_offset']):02X}` | `{row['point_count']}` | `{row['curve_role']}` |"
        )
    lines.extend(["", "## Unresolved", ""])
    for item in data["unresolved"]:
        lines.append(f"- {item}")
    write_text(path, "\n".join(lines) + "\n")


def main() -> None:
    data = build_report()
    write_json(OUT_JSON, data)
    write_markdown(OUT_MD, data)
    write_csv(OUT_CSV, data["reference_rows"])
    write_header(OUT_HEADER, data)
    write_source(OUT_SOURCE, data)
    if not data["summary"]["ok"]:
        raise SystemExit("title-intro opening camera runtime audit failed")


if __name__ == "__main__":
    main()
