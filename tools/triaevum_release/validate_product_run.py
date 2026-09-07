"""Local, bounded real-clock run of an immutable candidate with a private plugin.

This qualifies host integration, not a cold ROM-to-plugin build or visual parity.
The package and the caller's title/save data are never modified.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import struct
import time
from pathlib import Path

try:
    from .audit_release import audit_release
    from .bundle_paths import distribution_path
    from .common import atomic_write_json, load_json_object, sha256_file
    from .product_contract import ensure_runtime_config, query_product
    from .installation_context import InstallationContext, resolve_reference
except ImportError:
    from audit_release import audit_release
    from bundle_paths import distribution_path
    from common import atomic_write_json, load_json_object, sha256_file
    from product_contract import ensure_runtime_config, query_product
    from installation_context import InstallationContext, resolve_reference


def framebuffer_color_count(path: Path) -> int:
    """Sample native BMP pixels, excluding headers, padding and alpha."""
    data = path.read_bytes()
    if data[:2] != b"BM":
        raise ValueError("Capture is not a BMP framebuffer")
    offset = struct.unpack_from("<I", data, 10)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    bits = struct.unpack_from("<H", data, 28)[0]
    if width <= 0 or height == 0 or bits not in (24, 32):
        raise ValueError("Unsupported framebuffer layout")
    height = abs(height)
    stride = ((width * bits + 31) // 32) * 4
    if offset + stride * height > len(data):
        raise ValueError("Truncated framebuffer")
    return len({data[offset + y * stride + x * (bits // 8):
                     offset + y * stride + x * (bits // 8) + 3]
                for y in range(0, height, max(1, height // 64))
                for x in range(0, width, max(1, width // 64))})


def clone_validation_content(process_manifest: Path, title: Path, *, copy_inputs: bool) -> None:
    """Clone private inputs by verified identity, never rewrite the original title."""
    index = load_json_object(process_manifest.parent / "content.tap")
    manifest = load_json_object(process_manifest)
    context = InstallationContext(title)
    fields = {"code": "code_bin_path", "exheader": "exheader_path", "romfs": "romfs_image_path"}
    for group in ("inputs", "optional_inputs"):
        for kind, descriptor in index.get(group, {}).items():
            source = resolve_reference(descriptor["path"], process_manifest.parent)
            if copy_inputs:
                # Use the identity rather than untrusted labels as filesystem names.
                identity = descriptor["sha256"]
                if len(identity) != 64 or any(c not in "0123456789abcdef" for c in identity):
                    raise ValueError("Invalid private input identity")
                target = title / "inputs" / identity
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, target)
                if target.stat().st_size != descriptor["bytes"] or sha256_file(target) != identity:
                    raise ValueError("Private input copy failed identity verification")
            else:
                target = source
            descriptor["path"] = context.reference(target, title)
            descriptor["path_scope"] = context.scope(target)
            if group == "inputs" and kind in fields:
                manifest["source"][fields[kind]] = descriptor["path"]
    manifest_path = title / "process-manifest.json"
    atomic_write_json(manifest_path, manifest)
    index["path_mode"] = "manifest_relative_v1"
    index["process_manifest"] = {
        "path": manifest_path.name, "bytes": manifest_path.stat().st_size,
        "sha256": sha256_file(manifest_path),
    }
    atomic_write_json(title / "content.tap", index)
    atomic_write_json(title / "forge-state.json",
                      load_json_object(process_manifest.parent / "forge-state.json"))


def validate_run(package: Path, plugin: Path, process_manifest: Path,
                 output: Path, seconds: int = 30, relocate: bool = False,
                 unicode_path: bool = False, multiplier: int = 2,
                 throughput: bool = False) -> dict:
    if multiplier not in (1, 2, 3) or (throughput and multiplier != 1):
        raise ValueError("Throughput requires native x1; visual multiplier must be 1, 2 or 3")
    if unicode_path and not relocate:
        raise ValueError("Unicode path validation requires --relocate")
    if not 15 <= seconds <= 120:
        raise ValueError("Validation duration must be between 15 and 120 seconds")
    audit = audit_release(package)
    if not audit.ok:
        raise ValueError("Candidate failed audit: " + "; ".join(audit.errors[:10]))
    output = output.resolve()
    if output.exists():
        raise ValueError("Use a new validation directory; existing data is never replaced")
    installation = output / "private-installation"
    shutil.copytree(package, installation)
    # Reuse original private inputs read-only, but clone all mutable publication
    # metadata so this test cannot touch the user's installed title or saves.
    try:
        from . import forge
        from .activation_transaction import activation_transaction
        from .installed_runtime import validate_installed_runtime
    except ImportError:
        import forge
        from activation_transaction import activation_transaction
        from installed_runtime import validate_installed_runtime
    title = installation / "data/titles/validation"
    title.mkdir(parents=True)
    clone_validation_content(process_manifest, title, copy_inputs=relocate)
    profile_path = installation / "TriAevum.launch.json"
    active_path = installation / "data/active-title.json"
    with activation_transaction(installation, [
        profile_path, active_path, title / "forge-state.json",
        installation / "data/config/TriAevum.json",
        installation / "data/config/topscreen_ui.json",
    ]):
        published = forge.publish_private_runtime(
            title, plugin=plugin, runtime_plugin=installation / "triaevum_title_aot.dll",
            launch_profile=profile_path, data_root=installation / "data")
        forge.activate_prepared_title(title, active_title_state=active_path)
    generation = Path(published["plugin"])
    if relocate:
        # Rename only this newly-created private fixture, leaving no old path to
        # accidentally satisfy absolute references. No user installation is moved.
        moved = output / ("relocated installation \u00e8 \u65e5\u672c" if unicode_path else "relocated installation")
        if not installation.resolve().is_relative_to(output) or moved.exists():
            raise ValueError("Unsafe validation relocation")
        title_relative = title.relative_to(installation)
        generation_relative = generation.relative_to(installation)
        installation.rename(moved)
        installation = moved
        title = installation / title_relative
        generation = installation / generation_relative
        profile_path = installation / profile_path.name
        forge.load_prepared_content(title, required_inputs=("code", "exheader", "romfs"))
    validate_installed_runtime(installation / "TriAevum.exe", title, installation / "data",
                              load_json_object(title / "forge-state.json")["runtime"])
    receipt = query_product(installation / "TriAevum.exe", plugin=generation)
    if receipt["product"].get("private_title_loaded") is not True:
        raise ValueError("Validation requires the private whole-AOT v2 plugin")
    config = installation / "data/config/TriAevum.json"
    ensure_runtime_config(config, receipt["product"])
    settings = load_json_object(config)
    graphics = settings.setdefault("Graphics", {})
    graphics.setdefault("FrameRate", {})["Mode"] = {1: "Original30", 2: "Interpolated2x", 3: "Interpolated3x"}[multiplier]
    graphics.setdefault("Presentation", {})["VSync"] = not (throughput or multiplier == 3)
    atomic_write_json(config, settings)
    topscreen = config.with_name("topscreen_ui.json")
    shutil.copy2(distribution_path("config/topscreen_ui.example.json"), topscreen)
    report_path = output / "runtime-state.json"
    screenshot = output / "framebuffer.bmp"
    arguments = [
        str(installation / "TriAevum.exe"), "--launch-profile", str(profile_path),
        "--frames", "0", "--max-seconds", str(seconds),
        "--output", str(report_path), "--screenshot", str(screenshot),
        "--screenshot-start-frame", str(min(600, seconds * 30)),
        "--benchmark-warmup-frames", "60",
    ]
    startup = None
    if throughput:
        arguments.append("--throughput-benchmark")
    if os.name == "nt":
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = 0
    started = time.perf_counter()
    with (output / "run.log").open("w", encoding="utf-8") as log:
        result = subprocess.run(arguments, cwd=installation, stdout=log,
                                stderr=subprocess.STDOUT, timeout=seconds + 45,
                                startupinfo=startup, check=False)
    if result.returncode != 0 or not report_path.is_file():
        raise ValueError(f"Candidate run failed ({result.returncode}); see {output / 'run.log'}")
    state = load_json_object(report_path)
    frame_rate = state.get("frame_rate", {})
    interpolation = state.get("visual_interpolation", {})
    summary = {
        "format": "triaevum_product_run_v1", "runtime": receipt,
        "plugin_sha256": sha256_file(plugin),
        "process_manifest_sha256": sha256_file(process_manifest),
        "scope": ("native_refresh_throughput_existing_private_plugin" if throughput
                  else "real_clock_existing_private_plugin_not_cold_forge"),
        "requested_multiplier": multiplier,
        "requested_throughput": throughput,
        "installation_relocated_with_inputs": relocate,
        "unicode_installation_path": unicode_path,
        "wall_seconds": time.perf_counter() - started,
        "benchmark_window": state.get("benchmark_window"),
        "ui_profile": state.get("ui_profile"),
        "visual_interpolation_active": frame_rate.get("visual_interpolation_active"),
        "visual_sample_multiplier": frame_rate.get("visual_sample_multiplier"),
        "interpolated_draws": interpolation.get("interpolated_draws", 0),
        "framebuffer_written": screenshot.is_file(),
        "sampled_framebuffer_colors": framebuffer_color_count(screenshot) if screenshot.is_file() else 0,
    }
    atomic_write_json(output / "validation.json", summary)
    if (summary["visual_interpolation_active"] is not (multiplier > 1)
            or summary["visual_sample_multiplier"] != multiplier
            or (multiplier > 1 and summary["interpolated_draws"] == 0)
            or summary["sampled_framebuffer_colors"] < 8):
        raise ValueError("Run did not demonstrate the requested cadence and framebuffer; inspect validation.json")
    if throughput:
        window = summary["benchmark_window"] or {}
        if (window.get("throughput_mode") is not True or window.get("vsync") is not False
                or window.get("pacing_enabled") is not False or window.get("sdl_frame_limiter_enabled") is not False):
            raise ValueError("Throughput run retained a limiter; inspect validation.json")
    return summary


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, required=True)
    parser.add_argument("--plugin", type=Path, required=True)
    parser.add_argument("--process-manifest", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--seconds", type=int, default=30)
    parser.add_argument("--multiplier", type=int, choices=(1, 2, 3), default=2)
    parser.add_argument("--throughput", action="store_true")
    parser.add_argument("--relocate", action="store_true",
                        help="Copy private inputs and move the test installation before launch")
    parser.add_argument("--unicode-path", action="store_true",
                        help="Use Latin and non-Latin characters in the relocated path")
    args = parser.parse_args()
    summary = validate_run(args.package, args.plugin, args.process_manifest,
                           args.output, args.seconds, args.relocate, args.unicode_path,
                           args.multiplier, args.throughput)
    print(json.dumps({key: value for key, value in summary.items() if key != "runtime"}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
