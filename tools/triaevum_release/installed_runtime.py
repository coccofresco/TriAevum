"""Validate the installed runtime tuple before Forge launches native code."""

from pathlib import Path
from typing import Any

try:
    from .common import load_json_object, sha256_file
    from .installation_context import resolve_reference, expand_profile_argument
    from .release_platform import for_target, host_platform
except ImportError:
    from common import load_json_object, sha256_file
    from installation_context import resolve_reference, expand_profile_argument
    from release_platform import for_target, host_platform


def validate_installed_runtime(
    executable: Path, title: Path, data_root: Path, runtime: dict[str, Any],
) -> Path:
    if runtime.get("status") != "ready":
        raise ValueError("The active title has no ready playable runtime; run Forge again")
    platform = for_target(runtime["target"]) if "target" in runtime else host_platform()
    if platform != host_platform():
        raise ValueError("Installed runtime belongs to another platform; run this platform's Forge")
    plugin = resolve_reference(str(runtime.get("plugin") or (executable.parent / platform.title_module)), title)
    for path, key in ((executable, "runtime_sha256"), (plugin, "plugin_sha256")):
        if not path.is_file() or sha256_file(path) != runtime.get(key):
            raise ValueError(f"Installed runtime identity mismatch: {path}; run Forge again")
    profile_value = runtime.get("launch_profile")
    if not isinstance(profile_value, str) or not profile_value:
        raise ValueError("The active title has no launch profile")
    profile = resolve_reference(profile_value, title)
    expected_hash = runtime.get("launch_profile_sha256")
    if expected_hash and sha256_file(profile) != expected_hash:
        raise ValueError("The launch profile changed after activation; run Forge again")
    payload = load_json_object(profile)
    arguments = payload.get("arguments")
    if (payload.get("format") != "oot3d_native_game_launch_profile_v1"
            or not isinstance(arguments, list)
            or not all(isinstance(value, str) for value in arguments)):
        raise ValueError("Invalid installed launch profile")
    arguments = [expand_profile_argument(argument, profile) for argument in arguments]
    if "--title-plugin" in arguments:
        index = arguments.index("--title-plugin") + 1
        if (arguments.count("--title-plugin") != 1 or index >= len(arguments)
                or Path(arguments[index]).resolve() != plugin.resolve()):
            raise ValueError("Launch profile selects another title plugin")
    elif plugin.resolve() != (executable.parent / platform.title_module).resolve():
        raise ValueError("Launch profile does not select the installed plugin generation")
    # Resolve profile-relative paths first. Reject ambiguous duplicate
    # routing arguments rather than validating one value and executing another.
    for option, expected in (
        ("--a32-process-manifest", title / "process-manifest.json"),
        ("--config", data_root / "config" / "TriAevum.json"),
        ("--topscreen-config", data_root / "config" / "topscreen_ui.json"),
        ("--save-data", data_root / "savedata"),
    ):
        if arguments.count(option) != 1:
            raise ValueError(f"Missing or duplicate installed profile option: {option}")
        index = arguments.index(option) + 1
        if index >= len(arguments) or Path(arguments[index]).resolve() != expected.resolve():
            raise ValueError(f"Launch profile points to another installation: {option}")
        if option != "--save-data":
            load_json_object(expected)
    for field, option, label in (
        ("topscreen_textures", "--topscreen-texture-overrides", "TopScreen textures"),
        ("pica_shader_pack", "--pica-aot-shader-pack", "PICA shader pack"),
    ):
        resource = runtime.get(field)
        if resource is None:
            continue
        pack = resolve_reference(resource["path"], title)
        if arguments.count(option) != 1:
            raise ValueError(f"Installed {label} routing is missing or duplicated")
        index = arguments.index(option) + 1
        if (index >= len(arguments) or Path(arguments[index]).resolve() != pack.resolve()
                or not pack.is_file() or sha256_file(pack) != resource["sha256"]):
            raise ValueError(f"Installed {label} changed or are missing; run Forge again")
    return profile
