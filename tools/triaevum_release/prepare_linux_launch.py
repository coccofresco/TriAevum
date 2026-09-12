"""Developer-only Linux launch profile for an existing private installation."""
from __future__ import annotations

import argparse
from pathlib import Path

try:
    from .common import atomic_write_json, load_json_object
except ImportError:
    from common import atomic_write_json, load_json_object


def prepare(installation: Path, plugin: Path) -> Path:
    installation = installation.resolve()
    plugin = plugin.resolve(strict=True)
    if not plugin.is_file() or plugin.suffix != ".so":
        raise ValueError("Expected a Linux title shared library")
    profile = load_json_object(installation / "TriAevum.launch.json")
    if profile.get("format") != "oot3d_native_game_launch_profile_v1":
        raise ValueError("Unsupported launch profile")
    arguments = profile.get("arguments")
    if not isinstance(arguments, list) or arguments.count("--title-plugin") != 1:
        raise ValueError("Expected exactly one title plugin in the launch profile")
    index = arguments.index("--title-plugin") + 1
    if index >= len(arguments):
        raise ValueError("Missing title plugin argument")
    arguments[index] = str(plugin)
    profile.setdefault("path_scopes", {})["--title-plugin"] = "absolute"
    destination = installation / "TriAevum.linux.launch.json"
    atomic_write_json(destination, profile)
    return destination


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("installation", type=Path)
    parser.add_argument("plugin", type=Path)
    args = parser.parse_args()
    print(prepare(args.installation, args.plugin))
