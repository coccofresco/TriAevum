#!/usr/bin/env python3
"""Private developer staging from a Forge installation, not an APK/release payload."""
import argparse
import json
from pathlib import Path
import shutil


def contained(root: Path, path: Path) -> Path:
    resolved = path.resolve()
    if not resolved.is_relative_to(root.resolve()):
        raise ValueError(f"Input escapes the prepared installation: {path}")
    return resolved


def prepare(installation: Path, launch_profile: Path, output: Path) -> None:
    root = installation.resolve()
    output = output.resolve()
    repository = Path(__file__).resolve().parents[2]
    if output == repository or output.is_relative_to(repository):
        raise ValueError("Private game data must remain outside the source repository")
    if output == root or output.is_relative_to(root) or root.is_relative_to(output):
        raise ValueError("Use a separate staging directory outside the installation")
    profile = json.loads(launch_profile.read_text(encoding="utf-8"))
    args = profile["arguments"]
    if len(args) % 2:
        raise ValueError("This developer staging tool expects key/value launch arguments")
    options = dict(zip(args[::2], args[1::2]))

    def input_path(option: str) -> Path:
        value = options[option].replace("${profile_dir}", str(launch_profile.parent.resolve()))
        return contained(root, Path(value))

    manifest_path = input_path("--a32-process-manifest")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    inputs = [manifest_path, input_path("--resource-root")]
    for key in ("code_bin_path", "exheader_path", "romfs_image_path"):
        inputs.append(contained(root, manifest_path.parent / manifest["source"][key]))
    for key in ("--topscreen-config", "--topscreen-texture-overrides"):
        if key in options:
            inputs.append(input_path(key))
    if any(not path.exists() for path in inputs):
        raise FileNotFoundError("Prepared installation is missing a required input")
    output.mkdir(parents=True, exist_ok=False)
    for source in inputs:
        destination = output / source.relative_to(root)
        destination.parent.mkdir(parents=True, exist_ok=True)
        if source.is_dir():
            shutil.copytree(source, destination)
        else:
            shutil.copy2(source, destination)

    def relocated(key: str) -> str:
        return "${profile_dir}/" + input_path(key).relative_to(root).as_posix()

    target = {
        "--a32-process-manifest": relocated("--a32-process-manifest"),
        "--resource-root": relocated("--resource-root"),
        "--renderer": "nri",
        "--ui-profile": options.get("--ui-profile", "topscreen"),
        "--config": "${profile_dir}/data/config/TriAevum.android.json",
        "--save-data": "${profile_dir}/data/savedata",
        "--output": "${profile_dir}/data/runtime-state.json",
        "--gameplay-timing": "native30_no_interpolation",
        "--presentation-rate": "30",
        "--width": "640", "--height": "360",
        "--max-seconds": "120",
        "--screenshot": "${profile_dir}/data/intro-frame.bmp",
        "--screenshot-start-frame": "30", "--screenshot-interval": "150",
    }
    for key in ("--topscreen-config", "--topscreen-texture-overrides"):
        if key in options:
            target[key] = relocated(key)
    config = output / "data/config/TriAevum.android.json"
    config.parent.mkdir(parents=True, exist_ok=True)
    config.write_text("{}\n", encoding="utf-8")
    (output / "TriAevum.android.launch.json").write_text(json.dumps({
        "format": "oot3d_native_game_launch_profile_v1",
        "arguments": [item for pair in target.items() for item in pair] + ["--screenshot-sequence"],
        "path_scopes": {key: "relative" for key, value in target.items()
                        if value.startswith("${profile_dir}/")},
    }, indent=2) + "\n", encoding="utf-8")
    print(f"Private intro inputs staged at {output}; do not publish or put in APK assets.")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--installation", type=Path, required=True)
    parser.add_argument("--launch-profile", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    prepare(args.installation, args.launch_profile, args.output)


if __name__ == "__main__":
    main()
