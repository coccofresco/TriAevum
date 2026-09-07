"""Bounded CACAO reproduction with isolated settings/saves and native captures."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import time

from .common import atomic_write_json, load_json_object


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--profile", type=Path, required=True)
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--quality", type=int, choices=(-1, 0, 1), required=True)
    parser.add_argument("--seconds", type=int, default=25)
    parser.add_argument("--live", action="store_true")
    parser.add_argument("--live-start", type=int, default=100)
    parser.add_argument("--live-stride", type=int, default=25)
    parser.add_argument("--without-grass", action="store_true")
    parser.add_argument("--with-hiz-taa", action="store_true")
    parser.add_argument("--with-sssr", action="store_true")
    parser.add_argument("--guide-view", type=int, choices=(1, 2, 3))
    parser.add_argument("--screenshot-frame", type=int, default=120)
    args = parser.parse_args()
    root = args.output.resolve()
    root.mkdir(parents=True, exist_ok=False)
    config = load_json_object(args.config)
    config["Graphics"]["Effects"]["AO"].update(
        Mode="Off" if args.quality == -1 else "CACAO", Quality=max(0, args.quality))
    if args.without_grass:
        config["Graphics"]["Grass"]["Quality"] = "Off"
    if args.with_hiz_taa:
        config["Graphics"]["AA"]["Mode"] = "TAA"
        config["Graphics"]["Effects"]["Reflections"]["Mode"] = "HiZ"
    if args.with_sssr:
        config["Graphics"]["Effects"]["Reflections"]["Mode"] = "FidelityFXSSSR"
    atomic_write_json(root / "config.json", config)
    command = [str(args.runtime.resolve()), "--launch-profile", str(args.profile.resolve()),
               "--config", str(root / "config.json"), "--save-data", str(root / "savedata"),
               "--output", str(root / "runtime.json"), "--max-seconds", str(args.seconds),
               "--frames", "0", "--screenshot", str(root / "framebuffer.bmp"),
               "--screenshot-start-frame", str(args.screenshot_frame)]
    environment = dict(os.environ, OOT3D_VULKAN_VALIDATION="1",
                       OOT3D_VULKAN_DIAGNOSTICS_PATH=str(root / "vulkan.json"),
                       OOT3D_VULKAN_DIAGNOSTICS_MAX_FRAMES="3000")
    if args.live:
        environment["TRIAEVUM_CACAO_DIAGNOSTIC_SEQUENCE"] = json.dumps([
            {"frame": args.live_start + i * args.live_stride, "quality": quality}
            for i, quality in enumerate((0, 1, -1, 1, 0, -1))])
    if args.guide_view:
        environment["TRIAEVUM_GUIDE_DIAGNOSTIC_VIEW"] = str(args.guide_view)
    started = time.perf_counter()
    result = {"quality": args.quality, "command": command}
    try:
        with (root / "stdout.log").open("w", encoding="utf-8") as out, \
                (root / "stderr.log").open("w", encoding="utf-8") as err:
            process = subprocess.run(command, cwd=root, env=environment, stdout=out, stderr=err,
                                     timeout=args.seconds + 45, check=False)
        result["exit_code"] = process.returncode
    except subprocess.TimeoutExpired:
        result["timed_out"] = True
    result["wall_seconds"] = time.perf_counter() - started
    diagnostics = root / "vulkan.json"
    if diagnostics.exists():
        frames = load_json_object(diagnostics).get("frames", [])
        result["rendered_frames"] = len(frames)
        result["cacao_passes"] = sum(f.get("cacao_pass_count", 0) for f in frames)
        result["normal_guide_passes"] = sum(f.get("cacao_normal_guide_pass_count", 0) for f in frames)
        result["sssr_passes"] = sum(f.get("fidelityfx_sssr_count", 0) for f in frames)
        result["sssr_fallbacks"] = sum(f.get("fidelityfx_sssr_fallback_count", 0) for f in frames)
        result["hiz_reflection_passes"] = sum(f.get("hiz_reflection_count", 0) for f in frames)
        result["validation_errors"] = max((f.get("vulkan_validation_error_count", 0) +
                                            f.get("nri_validation_error_count", 0) for f in frames), default=0)
    result["live_steps"] = (root / "stderr.log").read_text(encoding="utf-8").count("CACAO diagnostic:")
    runtime_log = root / "logs" / "OOT3D Native Game.log"
    result["storage_format_mismatch"] = runtime_log.exists() and "Format operand" in runtime_log.read_text(encoding="utf-8")
    result["framebuffer_captured"] = (root / "framebuffer.bmp").exists()
    result["passed"] = (result.get("exit_code") == 0 and
                        result.get("validation_errors") == 0 and
                        not result["storage_format_mismatch"] and
                        (not args.guide_view or result["framebuffer_captured"]) and
                        (not args.with_sssr or result.get("sssr_passes", 0) > 0 and result["sssr_fallbacks"] == 0) and
                        (not args.with_hiz_taa or result.get("hiz_reflection_passes", 0) > 0) and
                        (not args.live or result["live_steps"] == 6) and
                        (args.quality < 0 and not args.live or result.get("cacao_passes", 0) > 0))
    atomic_write_json(root / "run.json", result)
    print(json.dumps(result, indent=2))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
