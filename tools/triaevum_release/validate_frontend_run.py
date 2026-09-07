"""Capture the native file-select flow without editing user settings or saves."""

import argparse
import os
from pathlib import Path
import subprocess

from .common import atomic_write_json, load_json_object


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--profile", type=Path, required=True)
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--seconds", type=int, default=30)
    parser.add_argument("--trace", action="store_true")
    args = parser.parse_args()
    root = args.output.resolve()
    root.mkdir(parents=True, exist_ok=False)
    config = load_json_object(args.config)
    config.setdefault("CVars", {}).setdefault("gOpenWindows", {})["Oot3dGraphics"] = 0
    atomic_write_json(root / "config.json", config)
    atomic_write_json(root / "input.json", {
        "schema": "oot3d.native_game.input_timeline.v1",
        "segments": [{"start_frame": 240, "end_frame_exclusive": 246,
                      "buttons": ["start"]}],
    })
    command = [str(args.runtime.resolve()), "--launch-profile", str(args.profile.resolve()),
               "--config", str(root / "config.json"), "--save-data", str(root / "savedata"),
               "--output", str(root / "runtime.json"), "--max-seconds", str(args.seconds),
               "--frames", "360", "--input-timeline", str(root / "input.json"),
               "--screenshot", str(root / "framebuffer.bmp"), "--screenshot-sequence",
               "--screenshot-start-frame", "180", "--screenshot-interval", "60"]
    if args.trace:
        command += ["--pica-semantic-trace", str(root / "pica.jsonl")]
    environment = dict(os.environ, OOT3D_VULKAN_VALIDATION="1",
                       OOT3D_VULKAN_DIAGNOSTICS_PATH=str(root / "vulkan.json"),
                       OOT3D_VULKAN_DIAGNOSTICS_MAX_FRAMES="3000")
    result = {"command": command}
    try:
        with (root / "stdout.log").open("w", encoding="utf-8") as out, \
                (root / "stderr.log").open("w", encoding="utf-8") as err:
            process = subprocess.run(command, cwd=root, env=environment, stdout=out, stderr=err,
                                     timeout=args.seconds + 45, check=False)
        result["exit_code"] = process.returncode
    except subprocess.TimeoutExpired:
        result["timed_out"] = True
    result["captures"] = sorted(p.name for p in root.glob("*.bmp"))
    diagnostics = root / "vulkan.json"
    if diagnostics.exists():
        frames = load_json_object(diagnostics).get("frames", [])
        result["validation_errors"] = max((
            f.get("vulkan_validation_error_count", 0) +
            f.get("nri_validation_error_count", 0) for f in frames), default=0)
    runtime = root / "runtime.json"
    if runtime.exists():
        ui = load_json_object(runtime).get("n64_integrated_ui", {})
        result["active_file_select_states"] = ui.get("active_file_select_states", 0)
        result["bottom_overlay_frames"] = ui.get("native_bottom_overlay_frames", 0)
        result["bottom_frontend_frames"] = ui.get("native_bottom_frontend_presentation_frames", 0)
    result["structural_checks_passed"] = (
        result.get("exit_code") == 0 and bool(result["captures"]) and
        result.get("validation_errors") == 0 and
        result.get("active_file_select_states", 0) > 0 and
        result.get("bottom_overlay_frames") == 0 and
        result.get("bottom_frontend_frames") == 0)
    atomic_write_json(root / "run.json", result)
    print(result)
    return 0 if result["structural_checks_passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
