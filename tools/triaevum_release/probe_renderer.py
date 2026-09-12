"""Bounded renderer probe using a private installation, never its live config."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess


def seed_save_data(source: Path, destination: Path):
    """Copy regular files only; never give the guest access to the fixture itself."""
    source = source.resolve(strict=True)
    if not source.is_dir():
        raise ValueError("save-data seed must be a directory")
    if destination.resolve().is_relative_to(source):
        raise ValueError("private save output must be outside the seed directory")
    files = sorted(source.rglob("*"))
    if any(path.is_symlink() or (not path.is_file() and not path.is_dir()) for path in files):
        raise ValueError("save-data seed must not contain links or special files")
    destination.mkdir(parents=True, exist_ok=False)
    manifest = []
    for path in files:
        if not path.is_file():
            continue
        relative = path.relative_to(source)
        data = path.read_bytes()
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        manifest.append({"path": relative.as_posix(), "bytes": len(data),
                         "sha256": hashlib.sha256(data).hexdigest()})
    return {"source": str(source), "files": manifest}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("installation", type=Path)
    parser.add_argument("executable", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--profile", default="TriAevum.launch.json")
    parser.add_argument("--seconds", type=int, default=45)
    parser.add_argument("--reflections", choices=["Off", "HiZ", "FidelityFXSSSR"])
    parser.add_argument("--material-hash", help="Diagnostic-only explicit reflective texture (16 hex digits)")
    parser.add_argument("--debug-view", type=int, choices=range(5), default=0)
    parser.add_argument("--capture-interval", type=int, default=300)
    parser.add_argument("--no-captures", action="store_true",
                        help="Measure pacing without synchronous framebuffer readback and image writes")
    parser.add_argument("--throughput", action="store_true",
                        help="Bounded native-step benchmark; disables interpolation, requires no captures and a frame count")
    parser.add_argument("--warmup-frames", type=int, default=120,
                        help="Frames excluded from throughput measurement (default: 120)")
    parser.add_argument("--extended-diagnostics", action="store_true")
    parser.add_argument("--native-fidelity", action="store_true",
                        help="Authentic rendering, fixed native ticks, no interpolation")
    parser.add_argument("--frames", type=int, default=0,
                        help="Optional presentation count; seconds remains a safety bound")
    parser.add_argument("--cache-directory", type=Path,
                        help="Isolated renderer cache (defaults to OUTPUT/cache)")
    parser.add_argument("--shader-pack", type=Path)
    parser.add_argument("--load-state", type=Path, help="Read-only checkpoint for a gameplay probe")
    parser.add_argument("--game-language", help="Private game language preference; requires a cold boot to apply")
    parser.add_argument("--input-timeline", type=Path, help="Repeatable native input sequence")
    parser.add_argument("--scenario-catalog", type=Path, help="Existing native-transition scenario catalog")
    parser.add_argument("--scenario", help="Scenario loaded by the original game's transition helper")
    parser.add_argument("--save-data-seed", type=Path,
                        help="Copy original persistent saves into private OUTPUT/savedata")
    parser.add_argument("--save-state-frame", type=int,
                        help="Capture OUTPUT/checkpoint.oot3dsav at this run frame")
    args = parser.parse_args()
    if args.seconds <= 0 or args.capture_interval <= 0 or args.frames < 0:
        parser.error("seconds and capture interval must be positive; frames cannot be negative")
    if args.save_state_frame is not None and args.save_state_frame < 0:
        parser.error("save-state frame cannot be negative")
    if args.throughput and (not args.no_captures or args.frames <= 0):
        parser.error("throughput requires --no-captures and positive --frames")
    if args.throughput and not 0 <= args.warmup_frames < args.frames:
        parser.error("throughput warmup must be nonnegative and smaller than the frame count")
    if bool(args.scenario_catalog) != bool(args.scenario):
        parser.error("scenario and scenario-catalog must be supplied together")
    if args.native_fidelity and (args.reflections not in (None, "Off") or args.material_hash or args.debug_view):
        parser.error("native fidelity cannot enable reflection diagnostics")
    installation = args.installation.resolve(strict=True)
    executable = args.executable.resolve(strict=True)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    profile = json.loads((installation / args.profile).read_text(encoding="utf-8-sig"))
    arguments = [value.replace("${profile_dir}", installation.as_posix())
                 for value in profile["arguments"]]
    # Diagnostic state and automation must be explicit, not inherited from a
    # user's launcher (especially a checkpoint destination outside this probe).
    for option in ("--load-state", "--save-state", "--save-state-frame", "--input-timeline",
                   "--scenario-catalog", "--scenario",
                   "--screenshot", "--screenshot-start-frame", "--screenshot-interval"):
        while option in arguments:
            index = arguments.index(option)
            del arguments[index:index + 2]
    arguments = [value for value in arguments if value not in (
        "--screenshot-sequence", "--throughput-benchmark")]
    if args.throughput:
        arguments.append("--throughput-benchmark")
    def set_option(option, value):
        if option in arguments:
            arguments[arguments.index(option) + 1] = str(value)
        else:
            arguments.extend([option, str(value)])
    if args.throughput:
        set_option("--benchmark-warmup-frames", args.warmup_frames)
    config_index = arguments.index("--config") + 1
    language_source = Path(arguments[config_index]).parent / "game_language.json"
    language = json.loads(language_source.read_text(encoding="utf-8-sig")) if language_source.exists() else None
    if args.game_language:
        language = dict(language or {}, format="triaevum_game_language_v1", selected=args.game_language)
    if language is not None:
        (output / "game_language.json").write_text(json.dumps(language), encoding="utf-8")
    config = json.loads(Path(arguments[config_index]).read_text(encoding="utf-8-sig"))
    if args.native_fidelity:
        config.setdefault("Graphics", {})["Preset"] = "Authentic"
    if args.native_fidelity or args.throughput:
        config.setdefault("Graphics", {}).setdefault("FrameRate", {})["Mode"] = "Original30"
        set_option("--gameplay-timing", "native30_no_interpolation")
        set_option("--presentation-rate", "30")
        set_option("--fixed-delta-seconds", "0.033333333333333333")
    if args.throughput:
        # The renderer reapplies this setting after the host disables its limiter.
        config["Graphics"].setdefault("Presentation", {})["VSync"] = False
    if args.shader_pack:
        set_option("--pica-aot-shader-pack", args.shader_pack.resolve(strict=True).as_posix())
    if args.cache_directory or "--renderer-cache-directory" in arguments:
        set_option("--renderer-cache-directory", (args.cache_directory or output / "cache").resolve().as_posix())
    for option, source in (("--load-state", args.load_state),
                           ("--input-timeline", args.input_timeline),
                           ("--scenario-catalog", args.scenario_catalog)):
        if source:
            set_option(option, source.resolve(strict=True).as_posix())
    if args.scenario:
        set_option("--scenario", args.scenario)
    if args.save_state_frame is not None:
        set_option("--save-state", (output / "checkpoint.oot3dsav").as_posix())
        set_option("--save-state-frame", args.save_state_frame)
    if args.save_data_seed:
        manifest = seed_save_data(args.save_data_seed, output / "savedata")
        (output / "save-data-seed.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    reflections = config.setdefault("Graphics", {}).setdefault("Effects", {}).setdefault("Reflections", {})
    if args.reflections is not None:
        reflections["Mode"] = args.reflections
    reflections["DebugView"] = args.debug_view
    if args.material_hash:
        if len(args.material_hash) != 16 or any(c not in "0123456789abcdefABCDEF" for c in args.material_hash):
            parser.error("material hash must contain exactly 16 hexadecimal digits")
        reflections["Materials"] = [{"RuleId": 1, "Target": {
            "ContentHash": args.material_hash, "Width": 0, "Height": 0,
            "MapperSlotMask": 7}, "Profile": "Polished", "Reflectivity": 0.8,
            "Roughness": 0.15}]
    config_path = output / "config.json"
    config_path.write_text(json.dumps(config, indent=2), encoding="utf-8")
    arguments[config_index] = config_path.as_posix()
    # A probe may persist settings or save data: isolate both, including TopScreen.
    for option in ("--topscreen-config",):
        if option in arguments:
            index = arguments.index(option) + 1
            source = Path(arguments[index])
            target = output / source.name
            target.write_bytes(source.read_bytes())
            arguments[index] = target.as_posix()
    overrides = {"--save-data": output / "savedata", "--output": output / "runtime.json"}
    for option, value in overrides.items():
        if option in arguments:
            arguments[arguments.index(option) + 1] = value.as_posix()
        else:
            arguments.extend([option, value.as_posix()])
    for option, value in {"--frames": args.frames, "--max-seconds": args.seconds}.items():
        set_option(option, value)
    if not args.no_captures:
        for option, value in {
            "--screenshot": (output / "framebuffer.bmp").as_posix(),
            "--screenshot-start-frame": 120, "--screenshot-interval": args.capture_interval,
        }.items():
            set_option(option, value)
        arguments.append("--screenshot-sequence")
    if args.extended_diagnostics:
        arguments.append("--extended-diagnostics")
    environment = os.environ.copy()
    cache = (args.cache_directory or output / "cache").resolve()
    environment["TRIAEVUM_RENDERER_CACHE_DIR"] = str(cache)
    environment["OOT3D_VULKAN_DIAGNOSTICS_PATH"] = str(output / "gpu.json")
    environment["OOT3D_VULKAN_DIAGNOSTICS_MAX_FRAMES"] = str(args.seconds * 120)
    (output / "invocation.json").write_text(json.dumps(
        {"executable": str(executable), "arguments": arguments,
         "executable_sha256": hashlib.sha256(executable.read_bytes()).hexdigest(),
         "renderer_cache_directory": str(cache), "native_fidelity": args.native_fidelity,
         "throughput": args.throughput,
         "synchronous_captures": not args.no_captures},
        indent=2), encoding="utf-8")
    with (output / "launch.log").open("wb") as log:
        process = subprocess.Popen([str(executable), *arguments], cwd=installation,
                                   env=environment, stdout=log, stderr=subprocess.STDOUT)
        try:
            code = process.wait(timeout=args.seconds + 30)
        except subprocess.TimeoutExpired:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
            raise SystemExit("Renderer probe exceeded its bounded runtime")
    print(f"Renderer exit={code}; evidence={output}")
    raise SystemExit(code)


if __name__ == "__main__":
    main()
