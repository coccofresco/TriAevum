"""Build and smoke-test the self-contained TriAevum Forge executable."""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
A32_ROOT = REPO_ROOT / "tools/oot3d/native_a32_runtime"
GAME_RUNTIME_ROOT = REPO_ROOT / "tools/oot3d/native_game_runtime"


def _find_nlohmann_include(explicit: Path | None = None) -> Path:
    candidates = [
        explicit,
        REPO_ROOT / "tools/triaevum_release/vendor/include",
        Path(os.environ["TRIAEVUM_NLOHMANN_INCLUDE"])
        if os.environ.get("TRIAEVUM_NLOHMANN_INCLUDE")
        else None,
        Path(os.environ["VCPKG_ROOT"]) / "installed/x64-windows-static/include"
        if os.environ.get("VCPKG_ROOT")
        else None,
        Path("C:/vcpkg/installed/x64-windows-static/include"),
    ]
    for candidate in candidates:
        if candidate is not None and (
            candidate / "nlohmann/json_fwd.hpp"
        ).is_file():
            return candidate.resolve()
    raise FileNotFoundError(
        "nlohmann headers were not found; pass --nlohmann-include"
    )


def required_data(
    nlohmann_include: Path | None = None,
) -> tuple[tuple[Path, str], ...]:
    items: list[tuple[Path, str]] = [
        (
            REPO_ROOT / "tools/triaevum_release/supported_revisions.json",
            "tools/triaevum_release",
        ),
        (
            REPO_ROOT / "tools/triaevum_release/whole_aot_source_backend.py",
            "tools/triaevum_release",
        ),
        (
            REPO_ROOT / "tools/triaevum_release/direct_aot_backend.py",
            "tools/triaevum_release",
        ),
        (
            REPO_ROOT / "config/topscreen_ui.example.json",
            "config",
        ),
        (REPO_ROOT / "tools/triaevum_release/toolchain_probe.cpp", "tools/triaevum_release"),
        (REPO_ROOT / "tools/triaevum_release/whole_aot_abi_probe.cpp", "tools/triaevum_release"),
        (REPO_ROOT / "tools/triaevum_release/whole_aot_plugin_backend.py", "tools/triaevum_release"),
    ]
    for name in (
        "audit_whole_aot_product.py",
        "build_process_manifest.py",
        "generate_aot.py",
        "whole_aot_program.py",
        "whole_aot_cpp.py",
        "whole_aot_optimization_ir.py",
        "runtime_discovered_entries.csv",
        "whole_aot_product_manifest.json",
        "whole_aot_functions.json",
    ):
        items.append((A32_ROOT / name, "tools/oot3d/native_a32_runtime"))
    for name in (
        "codebin_function_inventory.csv",
        "codebin_callable_boundary_residue_audit_166.csv",
    ):
        items.append(
            (
                A32_ROOT / "upstream/analysis" / name,
                "tools/oot3d/native_a32_runtime/upstream/analysis",
            )
        )
    for source in sorted((A32_ROOT / "upstream/src/oot3d_pack").glob("*.py")):
        items.append(
            (
                source,
                "tools/oot3d/native_a32_runtime/upstream/src/oot3d_pack",
            )
        )
    for source in sorted((A32_ROOT / "upstream/recomp").glob("*.h")):
        items.append(
            (
                source,
                "tools/oot3d/native_a32_runtime/upstream/recomp",
            )
        )
    for name in (
        "oot3d_aot_architectural_state.h",
        "oot3d_native_a32_vfp_ops.h",
    ):
        items.append((A32_ROOT / name, "tools/oot3d/native_a32_runtime"))
    for name in (
        "oot3d_native_a32_memory.h",
        "oot3d_native_whole_aot_runtime.h",
        "triaevum_title_aot_abi.h",
        "triaevum_title_whole_aot_abi.h",
        "triaevum_title_whole_aot_plugin.cpp",
    ):
        items.append((GAME_RUNTIME_ROOT / name, "tools/oot3d/native_game_runtime"))
    nlohmann = _find_nlohmann_include(nlohmann_include) / "nlohmann"
    for source in sorted(path for path in nlohmann.rglob("*") if path.is_file()):
        relative_parent = source.relative_to(nlohmann).parent.as_posix()
        destination = "forge/include/nlohmann"
        if relative_parent != ".":
            destination += "/" + relative_parent
        items.append((source, destination))
    missing = [str(source) for source, _ in items if not source.is_file()]
    if missing:
        raise FileNotFoundError("Forge bundle input is missing: " + ", ".join(missing))
    return tuple(items)


def build(
    output: Path,
    work: Path,
    *,
    python: Path,
    nlohmann_include: Path | None = None,
    bundle_mode: str | None = None,
) -> Path:
    # Linux directory bundles start without unpacking into /tmp (which can be
    # noexec) and keep the GUI/worker interpreter beside their shared libraries.
    bundle_mode = bundle_mode or ("onefile" if sys.platform == "win32" else "onedir")
    if bundle_mode not in ("onefile", "onedir"):
        raise ValueError("Forge bundle mode must be onefile or onedir")
    output = output.resolve()
    work = work.resolve()
    output.mkdir(parents=True, exist_ok=True)
    work.mkdir(parents=True, exist_ok=True)
    command = [
        str(python),
        "-m",
        "PyInstaller",
        "--noconfirm",
        "--clean",
        f"--{bundle_mode}",
        "--name",
        "TriAevumForge",
        "--distpath",
        str(output),
        "--workpath",
        str(work / "work"),
        "--specpath",
        str(work / "spec"),
        "--paths",
        str(REPO_ROOT),
        "--hidden-import",
        "capstone",
        "--hidden-import",
        "tools.triaevum_release.forge_gui",
        "--hidden-import",
        "tkinter",
    ]
    if bundle_mode == "onefile" and sys.platform == "win32":
        command.extend(("--runtime-tmpdir", ".triaevum-forge-runtime"))
    separator = ";" if sys.platform == "win32" else ":"
    for source, destination in required_data(nlohmann_include):
        command.extend(("--add-data", f"{source}{separator}{destination}"))
    command.append(str(REPO_ROOT / "tools/triaevum_release/forge_entry.py"))
    subprocess.run(command, cwd=REPO_ROOT, check=True)
    executable_root = output / "TriAevumForge" if bundle_mode == "onedir" else output
    executable = executable_root / (
        "TriAevumForge.exe" if sys.platform == "win32" else "TriAevumForge"
    )
    completed = subprocess.run(
        [str(executable), "doctor", "--inventory"],
        cwd=executable_root,
        check=True,
        capture_output=True,
        text=True,
    )
    report = json.loads(completed.stdout)
    if report.get("status") != "inventory" or not report.get("recipes"):
        raise RuntimeError("frozen Forge doctor did not report a usable recipe")
    if report["direct_aot_tools"].get("whole_aot_builder_available") is not True:
        raise RuntimeError("frozen Forge lost its whole-AOT builder identity")
    return executable


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--python", type=Path, default=Path(sys.executable))
    parser.add_argument("--nlohmann-include", type=Path)
    parser.add_argument("--bundle-mode", choices=("onefile", "onedir"))
    args = parser.parse_args()
    executable = build(
        args.output,
        args.work,
        python=args.python,
        nlohmann_include=args.nlohmann_include,
        bundle_mode=args.bundle_mode,
    )
    print(executable)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
