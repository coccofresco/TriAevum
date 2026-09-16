"""Bounded native-update benchmark; all writable inputs are privately copied."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--invocation", type=Path, required=True)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--frames", type=int, default=900)
    parser.add_argument("--allow-legacy", action="store_true", help="Control run without the new phase accounting")
    parser.add_argument("--profile-runtime", action="store_true", help="Diagnostic attribution run, not a clean performance comparison")
    parser.add_argument("--diagnostic-plugin", type=Path, help="Symbol-bearing diagnostic module; not a performance baseline")
    parser.add_argument("--sample-map", type=Path, help="Link map belonging exactly to diagnostic-plugin")
    parser.add_argument("--sample-delay", type=float, default=10, help="Seconds before diagnostic sampling")
    args = parser.parse_args()
    if args.sample_map and (not args.diagnostic_plugin or not 0 <= args.sample_delay <= 60):
        parser.error("Sampling requires --diagnostic-plugin and a delay between 0 and 60 seconds")
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
            if key == "--title-plugin" and args.diagnostic_plugin:
                command.extend((key, str(args.diagnostic_plugin)))
                i += 2
                continue
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
            process = subprocess.Popen(command, cwd=root, env=env, stdout=out, stderr=err)
            try:
                if args.sample_map:
                    time.sleep(args.sample_delay)
                    subprocess.run([sys.executable, str(Path(__file__).with_name("sample_aot_windows.py")),
                                    "--pid", str(process.pid), "--seconds", "25",
                                    "--output", str(root / "native-samples.json"),
                                    "--module", str(args.diagnostic_plugin), "--map", str(args.sample_map)],
                                   timeout=40, check=True)
                code = process.wait(timeout=120)
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait()
        if code:
            raise RuntimeError(f"Runtime failed: {code}, {root}")
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
                        "diagnostic_plugin": str(args.diagnostic_plugin) if args.diagnostic_plugin else None,
                        "intrusive_native_sampling": args.sample_map is not None,
                        "memory_fingerprint": data["memory_content_fingerprint"]})
        (args.output / "measurements.json").write_text(json.dumps(results, indent=2))
        print(json.dumps({"run": run, "whole_fps": window["frames_per_second"], "pre_backend": budget}), flush=True)


if __name__ == "__main__":
    main()
