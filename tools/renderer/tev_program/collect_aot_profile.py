"""Record an LLVM instrumentation profile for the title module from real workloads.

Developer-only. Each workload runs the instrumented title module from one
checkpoint with its own private copy of configuration and save data, writes
raw counters through LLVM_PROFILE_FILE, and the raw files are merged into one
.profdata. Keep the measured benchmark scene out of the training workloads so
a profile cannot be tuned to the scene that later judges it.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(TOOLS / "oot3d" / "native_a32_runtime"))
from merge_llvm_profiles import merge_profiles  # noqa: E402

FORMAT = "triaevum_aot_profile_collection_v1"
REPLACED = {"--title-plugin", "--load-state", "--input-timeline", "--frames",
            "--max-seconds", "--output", "--renderer-cache-directory"}
PRIVATE_COPIES = {"--config", "--topscreen-config", "--save-data"}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def parse_workload(text: str) -> tuple[Path, Path | None]:
    state, _, timeline = text.partition("::")
    return Path(state), Path(timeline) if timeline else None


def workload_command(template: list[str], executable: Path, plugin: Path,
                     state: Path, timeline: Path | None, frames: int,
                     seconds: int, root: Path, cache: Path) -> list[str]:
    command = [str(executable)]
    default_timeline = None
    index = 1
    while index < len(template):
        key = template[index]
        if key in REPLACED:
            if key == "--input-timeline":
                default_timeline = template[index + 1]
            index += 2
            continue
        if key in PRIVATE_COPIES:
            source = Path(template[index + 1])
            target = root / key.removeprefix("--")
            if source.is_dir():
                shutil.copytree(source, target)
            elif source.is_file():
                shutil.copy2(source, target)
            else:
                raise FileNotFoundError(source)
            command.extend((key, str(target)))
            index += 2
            continue
        command.append(key)
        index += 1
    selected_timeline = timeline or (Path(default_timeline) if default_timeline else None)
    command.extend(("--title-plugin", str(plugin), "--load-state", str(state)))
    if selected_timeline is not None:
        command.extend(("--input-timeline", str(selected_timeline)))
    command.extend(("--frames", str(frames), "--max-seconds", str(seconds),
                    "--output", str(root / "runtime.json"),
                    "--renderer-cache-directory", str(cache)))
    return command


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--invocation", type=Path, required=True,
                        help="benchmark command template (JSON argv)")
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--plugin", type=Path, required=True,
                        help="title module built with --pgo-generate")
    parser.add_argument("--workload", action="append", required=True,
                        help="checkpoint[::input-timeline]; repeatable")
    parser.add_argument("--llvm-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--frames", type=int, default=900)
    parser.add_argument("--timeout-seconds", type=int, default=300)
    args = parser.parse_args()
    # The Linux title build (TRIAEVUM_TITLE_PGO=GENERATE) records the title
    # source and compiler beside the module; a USE build refuses a profile
    # whose record does not match it (tools/triaevum_release/linux_title).
    build_record = args.plugin.parent / "triaevum_title_aot.pgo.json"
    instrumented_build = (json.loads(build_record.read_text(encoding="utf-8"))
                          if build_record.is_file() else None)
    template = json.loads(args.invocation.read_text(encoding="utf-8"))
    if not isinstance(template, list):
        template = [str(args.executable), *template["arguments"]]
    args.output.mkdir(parents=True, exist_ok=False)
    cache = args.output / "renderer-cache"
    runs = []
    for number, text in enumerate(args.workload):
        state, timeline = parse_workload(text)
        root = args.output / f"{number:02d}"
        root.mkdir()
        command = workload_command(template, args.executable, args.plugin, state,
                                   timeline, args.frames, args.timeout_seconds - 30,
                                   root, cache)
        (root / "command.json").write_text(json.dumps(command, indent=2), encoding="utf-8")
        env = dict(os.environ, DISABLE_VULKAN_OBS_CAPTURE="1",
                   LLVM_PROFILE_FILE=str(root / "title-%p.profraw"))
        with (root / "stdout.log").open("w") as out, (root / "stderr.log").open("w") as err:
            process = subprocess.Popen(command, cwd=root, env=env, stdout=out, stderr=err)
            try:
                code = process.wait(timeout=args.timeout_seconds)
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait()
        raw = sorted(root.glob("*.profraw"))
        runtime = root / "runtime.json"
        frames = json.loads(runtime.read_text())["run_frames"] if runtime.is_file() else None
        runs.append({"workload": text, "exit_code": code, "frames": frames,
                     "profraw": [str(path) for path in raw]})
        print(json.dumps(runs[-1]), flush=True)
        if code or not raw or frames != args.frames:
            raise RuntimeError(f"workload did not complete with counters: {root}")
    merged = merge_profiles(args.llvm_root, [Path(p) for run in runs for p in run["profraw"]],
                            args.output / "title.profdata")
    receipt = {
        "format": FORMAT,
        "plugin": str(args.plugin), "plugin_sha256": sha256(args.plugin),
        "executable": str(args.executable), "executable_sha256": sha256(args.executable),
        "instrumented_build": instrumented_build,
        "frames_per_workload": args.frames,
        "runs": runs,
        "merge": merged,
        "profdata_sha256": sha256(args.output / "title.profdata"),
    }
    (args.output / "receipt.json").write_text(json.dumps(receipt, indent=2), encoding="utf-8")
    print(json.dumps({"profdata": str(args.output / "title.profdata"),
                      "sha256": receipt["profdata_sha256"]}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
