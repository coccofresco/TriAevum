"""Build and package the actual mature runtime, never an exe selected by name alone."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

try:
    from .build_forge_binary import build as build_forge
    from .common import atomic_write_json, load_json_object
    from .package_release import package_release
    from .product_contract import query_product
    from .source_archive import create_source_archive
    from .toolchain_setup_layout import setup_layout
    from .precompiled_title_layout import title_layout
    from .precompiled_variants import add_verified_variants
    from .forge import load_recipe, DEFAULT_RECIPES
except ImportError:
    from build_forge_binary import build as build_forge
    from common import atomic_write_json, load_json_object
    from package_release import package_release
    from product_contract import query_product
    from source_archive import create_source_archive
    from toolchain_setup_layout import setup_layout
    from precompiled_title_layout import title_layout
    from precompiled_variants import add_verified_variants
    from forge import load_recipe, DEFAULT_RECIPES


ROOT = Path(__file__).resolve().parents[2]


def visual_cpp_runtime_artifacts(directory: Path) -> dict[str, Path]:
    """Use the licensed publisher's Redist directory, never its System32 DLLs."""
    artifacts = {name: directory / name for name in (
        "msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll")}
    for name, source in artifacts.items():
        if not source.is_file() or source.is_symlink():
            raise ValueError(f"Missing Visual C++ redistributable {name} in {directory}; "
                             "supply --vc-redist-dir pointing to x64/Microsoft.VC143.CRT")
    return artifacts


def clang_header_layout(llvm: Path) -> list[dict]:
    root = llvm / "lib/clang/22/include"
    if root.is_symlink() or not (root / "stddef.h").is_file():
        raise ValueError("LLVM Clang resource headers are missing")
    items = []
    for source in sorted(root.rglob("*")):
        if source.is_symlink():
            raise ValueError("Linked LLVM resource headers are unsupported")
        if source.is_file():
            items.append({"source": str(source.resolve()),
                          "path": "forge/clang/include/" + source.relative_to(root).as_posix(),
                          "role": "forge_clang_header"})
    return items


def prepare(args) -> dict:
    crt_artifacts = visual_cpp_runtime_artifacts(args.vc_redist_dir.resolve())
    build_dir = args.build_dir.resolve()
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    # Reconfigure even on a no-op code build so the embedded commit and target
    # receipt cannot retain the previous Git HEAD.
    subprocess.run([str(args.cmake), "-S", str(ROOT), "-B", str(build_dir)], check=True)
    subprocess.run([str(args.cmake), "--build", str(build_dir), "--config", "Release",
                    "--target", "triaevum_public_runtime", "oot3d_game_module",
                    "--parallel", "2"], check=True)
    targets = load_json_object(build_dir / "triaevum-runtime-targets-Release.json")
    if targets.get("format") != "triaevum_runtime_targets_v1":
        raise ValueError("CMake did not publish the product targets")
    commit = targets["source_commit"]
    receipt = query_product(Path(targets["runtime"]), commit)
    if receipt["product"].get("private_title_loaded") is not False:
        raise ValueError("The build directory must contain the public stub, not a title")
    source_zip = work / "TriAevum-source.zip"
    create_source_archive(ROOT, source_zip, source_commit=commit)
    forge_exe = build_forge(work / "forge-binary", work / "forge-build",
                            python=Path(sys.executable), nlohmann_include=args.include)
    layout = load_json_object(ROOT / "tools/triaevum_release/runtime_release_layout.example.json")
    artifacts = {
        "TriAevum.exe": Path(targets["runtime"]),
        "triaevum_title_aot.dll": Path(targets["stub"]),
        "TriAevumForge.exe": forge_exe,
        "TriAevum-source.zip": source_zip,
        "shaderc_shared.dll": Path(targets["runtime"]).parent / "shaderc_shared.dll",
        "oot3d_game_module.dll": build_dir / "oot3d_game_module.dll",
        **crt_artifacts,
    }
    for item in layout["files"]:
        if item["source"].startswith("build-release/"):
            item["source"] = str(artifacts[Path(item["source"]).name].resolve())
    layout_path = work / "release-layout.json"
    layout["files"].extend(title_layout(
        runtime=Path(targets["runtime"]), native_module=artifacts["oot3d_game_module.dll"],
        plugin_manifest=args.title_build, generated_manifest=args.title_sources,
        build_source=args.title_build_source, recipe=load_recipe(DEFAULT_RECIPES, args.recipe),
        work=work / "precompiled"))
    catalog_path = work / "precompiled/precompiled-titles.json"
    definitions = load_json_object(DEFAULT_RECIPES)
    catalog, _ = add_verified_variants(load_json_object(catalog_path), definitions, definitions)
    atomic_write_json(catalog_path, catalog)
    atomic_write_json(layout_path, layout)
    return package_release(layout_path, args.output, source_root=ROOT,
                           version=args.version, source_commit=commit,
                           enforce_readiness=not args.candidate)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--cmake", type=Path, required=True)
    parser.add_argument("--include", type=Path, required=True)
    parser.add_argument("--vc-redist-dir", type=Path, required=True,
                        help="Licensed Visual Studio x64/Microsoft.VC143.CRT redistributable directory")
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--candidate", action="store_true")
    parser.add_argument("--title-build", type=Path, required=True, help="Verified whole-aot-plugin.json")
    parser.add_argument("--title-sources", type=Path, required=True, help="Corresponding generated C++ manifest")
    parser.add_argument("--title-build-source", type=Path, required=True, help="Source ZIP used to build the title")
    parser.add_argument("--recipe", default="oot3d-eur-project-baseline-16a6b0aa")
    args = parser.parse_args()
    print(json.dumps(prepare(args), indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
