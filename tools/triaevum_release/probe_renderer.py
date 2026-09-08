"""Bounded renderer probe using a private installation, never its live config."""

import argparse
import json
import os
from pathlib import Path
import subprocess


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
    parser.add_argument("--extended-diagnostics", action="store_true")
    args = parser.parse_args()
    if args.seconds <= 0 or args.capture_interval <= 0:
        parser.error("seconds and capture interval must be positive")
    installation = args.installation.resolve(strict=True)
    executable = args.executable.resolve(strict=True)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    profile = json.loads((installation / args.profile).read_text(encoding="utf-8-sig"))
    arguments = [value.replace("${profile_dir}", installation.as_posix())
                 for value in profile["arguments"]]
    config_index = arguments.index("--config") + 1
    config = json.loads(Path(arguments[config_index]).read_text(encoding="utf-8-sig"))
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
    arguments.extend(["--frames", "0", "--max-seconds", str(args.seconds),
                      "--screenshot", (output / "framebuffer.bmp").as_posix(),
                      "--screenshot-start-frame", "120", "--screenshot-sequence",
                      "--screenshot-interval", str(args.capture_interval)])
    if args.extended_diagnostics:
        arguments.append("--extended-diagnostics")
    environment = os.environ.copy()
    environment["OOT3D_VULKAN_DIAGNOSTICS_PATH"] = str(output / "gpu.json")
    environment["OOT3D_VULKAN_DIAGNOSTICS_MAX_FRAMES"] = str(args.seconds * 120)
    (output / "invocation.json").write_text(json.dumps(
        {"executable": str(executable), "arguments": arguments}, indent=2), encoding="utf-8")
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
