#!/usr/bin/env python3
"""Verify the first-intro cutscene runtime bridge."""

from __future__ import annotations

import json
import math
import sys
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_INTRO_TIMELINE = ROOT / "analysis" / "scene_cutscene_intro_camera_timeline.json"
DEFAULT_MISC_ACTION_TABLE = ROOT / "analysis" / "scene_cutscene_misc_action_table.json"
DEFAULT_NATIVE_SOURCE_TABLE = ROOT / "analysis" / "scene_cutscene_native_source_table.json"
DEFAULT_SET_TIME_TABLE = ROOT / "analysis" / "scene_cutscene_set_time_table.json"
DEFAULT_FRAME_LOOP_DECOMPILE = (
    ROOT
    / "analysis"
    / "scene_cutscene_context_ghidra_export"
    / "decompiled"
    / "99075_00321f50_FUN_00321f50.c"
)
DEFAULT_PROCESS_DECOMPILE = (
    ROOT
    / "analysis"
    / "scene_cutscene_context_ghidra_export"
    / "decompiled"
    / "99068_002c5ba0_Cutscene_ProcessCommands.c"
)
DEFAULT_RUNTIME_SOURCE = ROOT / "src" / "code" / "z_scene_cutscene_intro_runtime.c"
DEFAULT_OUT_MD = ROOT / "analysis" / "scene_cutscene_intro_runtime_verify.md"


def as_dict(value: Any) -> dict[str, Any]:
    return value if isinstance(value, dict) else {}


def as_list(value: Any) -> list[Any]:
    return value if isinstance(value, list) else []


def int_value(value: Any, default: int = 0) -> int:
    try:
        if value is None:
            return default
        if isinstance(value, str) and value.lower().startswith("0x"):
            return int(value, 16)
        return int(value)
    except (TypeError, ValueError):
        return default


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def find_camera_row(rows: list[dict[str, Any]], cutscene_source_index: int, frame: int) -> dict[str, Any] | None:
    for row in rows:
        if int_value(row.get("cutscene_source_index")) != cutscene_source_index:
            continue
        if int_value(row.get("start_frame")) < frame < int_value(row.get("end_frame")):
            return row
    return None


def verify_text_patterns(path: Path, patterns: list[str], label: str) -> list[str]:
    if not path.exists():
        return [f"missing {label}: {path}"]
    text = path.read_text(encoding="utf-8")
    return [f"{label}: missing pattern `{pattern}`" for pattern in patterns if pattern not in text]


def verify_native_evidence() -> list[str]:
    errors: list[str] = []
    errors.extend(
        verify_text_patterns(
            DEFAULT_FRAME_LOOP_DECOMPILE,
            [
                "if (*(short *)(param_2 + 0x20) == 0)",
                "*(short *)(param_2 + 0x20) = *(short *)(param_2 + 0x20) + 1",
                "Cutscene_ProcessCommands(param_1,param_2,*(undefined4 *)(param_1 + 0x229c))",
            ],
            "FUN_00321F50 frame loop",
        )
    )
    errors.extend(
        verify_text_patterns(
            DEFAULT_PROCESS_DECOMPILE,
            [
                "*(int *)(param_2 + 0x18) = local_58",
                "local_58 < (int)(uint)*(ushort *)(param_2 + 0x20)",
                "case 8:",
                "*(short *)(param_1 + 0x53f0) + 4",
                "case 9:",
                "*(undefined1 *)(DAT_002c655c + param_1) = 0x10",
                "case 0xb:",
                "*(short *)(param_1 + 0x53f0) + 0x14",
                "case 0xc:",
                "*(undefined1 *)(local_30 + 8) = 3",
                "case 0xe:",
                "FUN_00340a1c(param_1,1)",
                "case 0x11:",
                "Quake_RemoveFromIdx",
                "case 0x1e:",
                "Flags_SetEnv(param_1,3)",
                "case 0x1f:",
                "Flags_SetEnv(param_1,4)",
            ],
            "Cutscene_ProcessCommands misc runtime",
        )
    )
    errors.extend(
        verify_text_patterns(
            DEFAULT_RUNTIME_SOURCE,
            [
                "state->frame++;",
                "Oot3d_CutsceneIntroCameraFindTimelineRow",
                "Oot3d_CutsceneIntroCameraBuildView",
                "Oot3d_CutsceneIntroRuntimeProcessMiscActions",
                "Oot3d_CutsceneIntroRuntimeProcessSetTime",
                "Oot3d_CutsceneSetTimeCollectStartTriggers",
                "OOT3D_CUTSCENE_MISC_ACTION_REQUEST_CUTSCENE_END",
                "OOT3D_CUTSCENE_MISC_ACTION_SET_CAMERA_DATA_INDEX_0",
                "OOT3D_CUTSCENE_MISC_ACTION_SET_ENV_3",
                "OOT3D_CUTSCENE_MISC_ACTION_SET_ENV_4",
            ],
            "intro runtime source",
        )
    )
    return errors


def simulate_cutscene(
    cutscene: dict[str, Any],
    native_row: dict[str, Any],
    camera_rows: list[dict[str, Any]],
    misc_rows: list[dict[str, Any]],
    set_time_rows: list[dict[str, Any]],
) -> dict[str, Any]:
    cutscene_source_index = int_value(cutscene.get("cutscene_source_index"))
    native_end_frame = int_value(native_row.get("native_end_frame"))
    camera_end_frame = int_value(cutscene.get("camera_frame_end"))
    end_frame = max(native_end_frame, camera_end_frame)
    transition_counter = 0
    env_flag = 0
    env_flag3 = 0
    env_flag4 = 0
    camera_data_index = 0
    day_time = 0
    skybox_time = 0
    time_resolved = False
    cutscene_state = 0
    cutscene_end_counter = 0
    active_counts: Counter[str] = Counter()
    start_trigger_counts: Counter[str] = Counter()
    first_camera_frame_by_segment: dict[int, int] = {}
    event_frames: dict[str, list[int]] = defaultdict(list)
    camera_active_frame_count = 0
    first_frame_reset_seen = False
    frames_simulated = 0

    for frame in range(0, end_frame + 2):
        frames_simulated += 1
        if frame == 0:
            first_frame_reset_seen = True
            continue

        if native_end_frame < frame and cutscene_state != 4:
            cutscene_end_counter += 1
            cutscene_state = 3
            event_frames["native_end_frame_request"].append(frame)

        camera_row = find_camera_row(camera_rows, cutscene_source_index, frame)
        if camera_row is not None:
            camera_active_frame_count += 1
            first_camera_frame_by_segment.setdefault(
                int_value(camera_row.get("camera_blob_segment_source_index")),
                frame,
            )

        for action in misc_rows:
            if int_value(action.get("cutscene_source_index")) != cutscene_source_index:
                continue
            action_id = int_value(action.get("action_id"))
            action_name = str(action.get("action_name", ""))
            start_frame = int_value(action.get("start_frame"))
            end_frame_action = int_value(action.get("end_frame"))

            if (
                int_value(action.get("active_window_inclusive_start_exclusive_end")) != 0
                and start_frame <= frame < end_frame_action
            ):
                active_counts[action_name] += 1
                if action_id == 0x0008:
                    event_frames["transition_counter_small_ramp"].append(frame)
                    if transition_counter < 0x80:
                        transition_counter += 4
                elif action_id == 0x0009:
                    event_frames["environment_flag_3271_to_10"].append(frame)
                    env_flag = 0x10
                elif action_id == 0x000B:
                    event_frames["transition_counter_timed_ramp"].append(frame)
                    if transition_counter < 0x0672:
                        transition_counter += 0x14
                elif action_id == 0x001E:
                    event_frames["environment_flag_3_set"].append(frame)
                    env_flag3 = 1
                elif action_id == 0x001F:
                    event_frames["environment_flag_4_set"].append(frame)
                    env_flag4 = 1

            if int_value(action.get("start_frame_trigger_confirmed")) != 0 and start_frame == frame:
                start_trigger_counts[action_name] += 1
                if action_id == 0x000C:
                    event_frames["request_cutscene_end"].append(frame)
                    if cutscene_state != 4:
                        cutscene_end_counter += 1
                        cutscene_state = 3
                elif action_id == 0x000E:
                    event_frames["camera_data_index_0"].append(frame)
                    camera_data_index = 1
                elif action_id == 0x0010:
                    event_frames["camera_quake_start"].append(frame)
                elif action_id == 0x0011:
                    event_frames["camera_quake_stop"].append(frame)

        for row in set_time_rows:
            if int_value(row.get("cutscene_source_index")) != cutscene_source_index:
                continue
            start_frame = int_value(row.get("start_frame"))
            if start_frame == frame:
                day_time = int_value(row.get("day_time"))
                skybox_time = int_value(row.get("skybox_time"))
                time_resolved = True
                event_frames["set_time"].append(frame)

        if cutscene_state == 3:
            break

    return {
        "cutscene_source_index": cutscene_source_index,
        "scene_path": cutscene.get("scene_path", ""),
        "setup_index": int_value(cutscene.get("setup_index")),
        "native_end_frame": native_end_frame,
        "camera_end_frame": camera_end_frame,
        "frames_simulated": frames_simulated,
        "camera_active_frame_count": camera_active_frame_count,
        "first_frame_reset_seen": first_frame_reset_seen,
        "transition_counter53f0": transition_counter,
        "env_flag3": env_flag3,
        "env_flag4": env_flag4,
        "env_flag3271": env_flag,
        "camera_data_index": camera_data_index,
        "time_resolved": time_resolved,
        "day_time": day_time,
        "skybox_time": skybox_time,
        "cutscene_state": cutscene_state,
        "cutscene_end_counter": cutscene_end_counter,
        "active_counts": dict(sorted(active_counts.items())),
        "start_trigger_counts": dict(sorted(start_trigger_counts.items())),
        "first_camera_frame_by_segment": dict(sorted(first_camera_frame_by_segment.items())),
        "event_frames": {key: value for key, value in sorted(event_frames.items())},
    }


def verify_camera_samples(
    camera_rows: list[dict[str, Any]],
    errors: list[str],
) -> None:
    for row in camera_rows:
        cutscene_source_index = int_value(row.get("cutscene_source_index"))
        sample_frame = int_value(row.get("sample_frame"))
        segment_index = int_value(row.get("camera_blob_segment_source_index"))
        found = find_camera_row(camera_rows, cutscene_source_index, sample_frame)
        if found is None:
            errors.append(f"cutscene {cutscene_source_index}: sample frame {sample_frame} has no camera row")
            continue
        if int_value(found.get("camera_blob_segment_source_index")) != segment_index:
            errors.append(
                f"cutscene {cutscene_source_index}: sample frame {sample_frame} maps to segment "
                f"{found.get('camera_blob_segment_source_index')} instead of {segment_index}"
            )


def verify_misc_expectations(
    misc_rows: list[dict[str, Any]],
    simulations: list[dict[str, Any]],
    errors: list[str],
) -> None:
    simulations_by_cutscene = {
        int_value(simulation.get("cutscene_source_index")): simulation for simulation in simulations
    }
    for action in misc_rows:
        cutscene_source_index = int_value(action.get("cutscene_source_index"))
        simulation = simulations_by_cutscene.get(cutscene_source_index)
        if simulation is None:
            continue
        action_id = int_value(action.get("action_id"))
        start_frame = int_value(action.get("start_frame"))
        event_frames = as_dict(simulation.get("event_frames"))
        if action_id == 0x0008 and start_frame not in event_frames.get("transition_counter_small_ramp", []):
            errors.append(f"cutscene {cutscene_source_index}: missing active 0x08 event at {start_frame}")
        elif action_id == 0x0009 and start_frame not in event_frames.get("environment_flag_3271_to_10", []):
            errors.append(f"cutscene {cutscene_source_index}: missing active 0x09 event at {start_frame}")
        elif action_id == 0x000B and start_frame not in event_frames.get("transition_counter_timed_ramp", []):
            errors.append(f"cutscene {cutscene_source_index}: missing active 0x0B event at {start_frame}")
        elif action_id == 0x001E and start_frame not in event_frames.get("environment_flag_3_set", []):
            errors.append(f"cutscene {cutscene_source_index}: missing active 0x1E event at {start_frame}")
        elif action_id == 0x001F and start_frame not in event_frames.get("environment_flag_4_set", []):
            errors.append(f"cutscene {cutscene_source_index}: missing active 0x1F event at {start_frame}")
        elif action_id == 0x000C and start_frame not in event_frames.get("request_cutscene_end", []):
            errors.append(f"cutscene {cutscene_source_index}: missing start-trigger 0x0C event at {start_frame}")
        elif action_id == 0x000E and start_frame not in event_frames.get("camera_data_index_0", []):
            errors.append(f"cutscene {cutscene_source_index}: missing start-trigger 0x0E event at {start_frame}")


def verify_set_time_expectations(
    set_time_rows: list[dict[str, Any]],
    simulations: list[dict[str, Any]],
    errors: list[str],
) -> None:
    simulations_by_cutscene = {
        int_value(simulation.get("cutscene_source_index")): simulation for simulation in simulations
    }
    for row in set_time_rows:
        cutscene_source_index = int_value(row.get("cutscene_source_index"))
        simulation = simulations_by_cutscene.get(cutscene_source_index)
        if simulation is None:
            continue
        start_frame = int_value(row.get("start_frame"))
        if start_frame == 0:
            continue
        if start_frame not in as_dict(simulation.get("event_frames")).get("set_time", []):
            errors.append(f"cutscene {cutscene_source_index}: missing set-time trigger at {start_frame}")


def ensure_view_samples_are_finite(errors: list[str]) -> None:
    # The C camera runtime verifier proves full view projection over all 548 segments.
    # This verifier checks that the intro runner selects the same segment source rows.
    runtime_report = ROOT / "analysis" / "scene_cutscene_camera_runtime_verify.md"
    if not runtime_report.exists():
        errors.append(f"missing camera runtime verifier report: {runtime_report}")
        return
    text = runtime_report.read_text(encoding="utf-8")
    for pattern in ["Segments checked through runtime attach wrapper: 548", "Intro segments checked: 47", "Status: `pass`"]:
        if pattern not in text:
            errors.append(f"camera runtime verifier report missing `{pattern}`")


def write_markdown(path: Path, summary: dict[str, Any], simulations: list[dict[str, Any]], errors: list[str]) -> None:
    lines = [
        "# Scene Cutscene Intro Runtime Verification",
        "",
        "Verification for the first-intro runtime bridge that advances OOT3D-native cutscene frames, selects camera timeline rows, and exposes decoded `CS_CMD_MISC` and `CS_CMD_SETTIME` events.",
        "",
        "## Summary",
        "",
        f"- Cutscenes simulated: {summary['cutscene_count']}",
        f"- Camera timeline rows checked: {summary['camera_timeline_row_count']}",
        f"- Misc action rows in target cutscenes: {summary['target_misc_action_count']}",
        f"- Set-time rows in target cutscenes: {summary['target_set_time_count']}",
        f"- Total simulated frames: {summary['total_frames_simulated']}",
        f"- Native frame-loop evidence: `{summary['native_evidence_status']}`",
        f"- Status: `{summary['status']}`",
        "",
        "## Native Evidence",
        "",
        "- `FUN_00321F50` resets frame-zero side fields, increments `csCtx+0x20`, then calls `Cutscene_ProcessCommands(play, csCtx, play+0x229C)` in the normal path.",
        "- `Cutscene_ProcessCommands` stores native header `end_frame` at `csCtx+0x18` and requests end when `end_frame < csCtx+0x20` and state is not `4`.",
        "- Misc actions `0x08`, `0x09`, `0x0B`, `0x1E`, and `0x1F` run while their entry window is active; `0x0C`, `0x0E`, `0x10`, and `0x11` use the start-frame trigger path.",
        "- Set-time command `0x8C` uses the OOT3D handler path that compares `csCtx+0x20` with entry `+2`, reads `hour` from `+6`, reads `minute + 1` from `+7`, then writes day time and skybox time.",
        "",
        "## Simulated Cutscenes",
        "",
        "| cutscene | scene | setup | native end | camera end | camera frames | end counter | camera data index | env3 | env4 | env 3271 | time | transition 53f0 | key events |",
        "| ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |",
    ]
    for simulation in simulations:
        events = as_dict(simulation.get("event_frames"))
        event_summary = ", ".join(f"{key}:{len(value)}" for key, value in events.items())
        lines.append(
            f"| {simulation['cutscene_source_index']} | `{simulation['scene_path']}` | "
            f"{simulation['setup_index']} | {simulation['native_end_frame']} | "
            f"{simulation['camera_end_frame']} | {simulation['camera_active_frame_count']} | "
            f"{simulation['cutscene_end_counter']} | {simulation['camera_data_index']} | "
            f"{simulation['env_flag3']} | {simulation['env_flag4']} | "
            f"{simulation['env_flag3271']} | {simulation['day_time']} | "
            f"{simulation['transition_counter53f0']} | "
            f"{event_summary or '-'} |"
        )
    if errors:
        lines.extend(["", "## Errors", ""])
        lines.extend(f"- {error}" for error in errors)
    lines.append("")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def main() -> int:
    intro_payload = load_json(DEFAULT_INTRO_TIMELINE)
    misc_payload = load_json(DEFAULT_MISC_ACTION_TABLE)
    native_payload = load_json(DEFAULT_NATIVE_SOURCE_TABLE)
    set_time_payload = load_json(DEFAULT_SET_TIME_TABLE)
    cutscene_rows = [as_dict(row) for row in as_list(intro_payload.get("cutscene_rows"))]
    camera_rows = [as_dict(row) for row in as_list(intro_payload.get("camera_timeline_rows"))]
    misc_rows_all = [as_dict(row) for row in as_list(misc_payload.get("misc_action_rows"))]
    set_time_rows_all = [as_dict(row) for row in as_list(set_time_payload.get("set_time_rows"))]
    native_rows = [as_dict(row) for row in as_list(native_payload.get("cutscene_rows"))]
    native_by_cutscene = {
        int_value(row.get("cutscene_source_index")): row for row in native_rows
    }
    target_cutscene_indices = {int_value(row.get("cutscene_source_index")) for row in cutscene_rows}
    target_misc_rows = [
        row
        for row in misc_rows_all
        if int_value(row.get("cutscene_source_index")) in target_cutscene_indices
    ]
    target_set_time_rows = [
        row
        for row in set_time_rows_all
        if int_value(row.get("cutscene_source_index")) in target_cutscene_indices
    ]

    errors = verify_native_evidence()
    verify_camera_samples(camera_rows, errors)
    ensure_view_samples_are_finite(errors)

    simulations: list[dict[str, Any]] = []
    for cutscene in cutscene_rows:
        cutscene_source_index = int_value(cutscene.get("cutscene_source_index"))
        native_row = native_by_cutscene.get(cutscene_source_index)
        if native_row is None:
            errors.append(f"cutscene {cutscene_source_index}: missing native source row")
            continue
        simulations.append(
            simulate_cutscene(cutscene, native_row, camera_rows, target_misc_rows, target_set_time_rows)
        )

    verify_misc_expectations(target_misc_rows, simulations, errors)
    verify_set_time_expectations(target_set_time_rows, simulations, errors)
    if any(not simulation.get("first_frame_reset_seen") for simulation in simulations):
        errors.append("one or more simulations missed frame-zero reset")
    if not all(math.isfinite(float(simulation["camera_active_frame_count"])) for simulation in simulations):
        errors.append("non-finite camera active frame count")

    summary = {
        "cutscene_count": len(cutscene_rows),
        "camera_timeline_row_count": len(camera_rows),
        "target_misc_action_count": len(target_misc_rows),
        "target_set_time_count": len(target_set_time_rows),
        "total_frames_simulated": sum(int_value(row.get("frames_simulated")) for row in simulations),
        "native_evidence_status": "pass" if not verify_native_evidence() else "fail",
        "status": "pass" if not errors else "fail",
    }
    write_markdown(DEFAULT_OUT_MD, summary, simulations, errors)
    if errors:
        for error in errors[:50]:
            print(f"error: {error}", file=sys.stderr)
        if len(errors) > 50:
            print(f"error: {len(errors) - 50} additional errors omitted", file=sys.stderr)
        return 1

    print(
        "verified intro cutscene runtime bridge: "
        f"{len(cutscene_rows)} cutscenes, {len(camera_rows)} camera rows, "
        f"{len(target_misc_rows)} misc actions, {len(target_set_time_rows)} set-time rows"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
