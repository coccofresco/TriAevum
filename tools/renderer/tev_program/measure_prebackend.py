"""Bounded native-update benchmark; all writable inputs are privately copied."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--invocation", type=Path, required=True)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--frames", type=int, default=900)
    parser.add_argument("--allow-legacy", action="store_true", help="Control run without the new phase accounting")
    parser.add_argument("--profile-runtime", action="store_true", help="Diagnostic attribution run, not a clean performance comparison")
    args = parser.parse_args()
    original = json.loads(args.invocation.read_text(encoding="utf-8"))
    if not isinstance(original, list):
        original = [str(args.executable), *original["arguments"]]
    args.output.mkdir(parents=True, exist_ok=True)
    results = []
    for run in range(args.runs):
        root = args.output / str(run)
        root.mkdir(exist_ok=False)
        command = [str(args.executable)]
        skip_values = {"--frames", "--max-seconds", "--output", "--renderer-cache-directory",
                       "--screenshot", "--screenshot-start-frame", "--screenshot-interval"}
        i = 1
        while i < len(original):
            key = original[i]
            if key in skip_values:
                i += 2
                continue
            if key == "--screenshot-sequence":
                i += 1
                continue
            if key in {"--config", "--topscreen-config", "--save-data"}:
                source = Path(original[i+1])
                target = root / key.removeprefix("--")
                if source.is_dir():
                    shutil.copytree(source, target)
                elif source.is_file():
                    shutil.copy2(source, target)
                else:
                    raise FileNotFoundError(source)
                command.extend((key, str(target)))
                i += 2
                continue
            command.append(key)
            i += 1
        command.extend(("--frames", str(args.frames), "--max-seconds", "90",
                        "--output", str(root / "runtime.json"),
                        "--renderer-cache-directory", str(args.output / "cache")))
        if args.profile_runtime and "--profile-a32-runtime" not in command:
            command.append("--profile-a32-runtime")
        (root / "command.json").write_text(json.dumps(command, indent=2))
        env = dict(os.environ, DISABLE_VULKAN_OBS_CAPTURE="1")
        env.pop("TRIAEVUM_DIAGNOSTIC_MENU", None)
        with (root / "stdout.log").open("w") as out, (root / "stderr.log").open("w") as err:
            completed = subprocess.run(command, cwd=root, env=env, stdout=out, stderr=err, timeout=120)
        if completed.returncode:
            raise RuntimeError(f"Runtime failed: {completed.returncode}, {root}")
        data = json.loads((root / "runtime.json").read_text())
        window = data["benchmark_window"]
        budget = window.get("pre_backend_cpu")
        if budget is None and not args.allow_legacy:
            raise RuntimeError("Executable does not expose pre-backend timing")
        if (window["vsync"] or window["pacing_enabled"] or window["sdl_frame_limiter_enabled"]
                or not window["throughput_mode"] or (budget and budget["invalid_intervals"])
                or data["frame_rate"]["visual_interpolation_active"]
                or data["run_frames"] != args.frames or (budget and budget["samples"] != window["measured_frames"])):
            raise RuntimeError(f"Invalid timing conditions: {root}")
        results.append({"run": run, "benchmark": window,
                        "memory_fingerprint": data["memory_content_fingerprint"]})
        (args.output / "measurements.json").write_text(json.dumps(results, indent=2))
        print(json.dumps({"run": run, "whole_fps": window["frames_per_second"], "pre_backend": budget}), flush=True)


if __name__ == "__main__":
    main()
