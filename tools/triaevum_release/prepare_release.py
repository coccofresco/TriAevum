"""Build and package the actual mature runtime, never an exe selected by name alone."""

from __future__ import annotations

import argparse
import json
import os
import subprocess
from pathlib import Path

try:
    from .rust_forge_build import build as build_rust_forge
    from .common import atomic_write_json, load_json_object
    from .release_platform import WINDOWS, WINDOWS_GNU
    from .package_release import package_release
    from .archive_release import create_release_archive
    from .product_contract import EXE_RUNNER_VARIABLE, query_product
    from .source_archive import create_source_archive
    from .toolchain_setup_layout import setup_layout
    from .precompiled_title_layout import title_layout
    from .precompiled_variants import add_verified_variants
    from .qualified_input_coverage import bind as bind_input_coverage
    from .shader_release_layout import bind_renderer_compiler
    from .shader_corpus_layout import bind_shader_corpus
    from .precomputed_tables import bind_precomputed_tables
    from .forge import load_recipe
    from .title_release import compose_title_layout, restoration_members, retarget_recipe, title_release
    from . import release_signature
except ImportError:
    from rust_forge_build import build as build_rust_forge
    from common import atomic_write_json, load_json_object
    from release_platform import WINDOWS, WINDOWS_GNU
    from package_release import package_release
    from archive_release import create_release_archive
    from product_contract import EXE_RUNNER_VARIABLE, query_product
    from source_archive import create_source_archive
    from toolchain_setup_layout import setup_layout
    from precompiled_title_layout import title_layout
    from precompiled_variants import add_verified_variants
    from qualified_input_coverage import bind as bind_input_coverage
    from shader_release_layout import bind_renderer_compiler
    from shader_corpus_layout import bind_shader_corpus
    from precomputed_tables import bind_precomputed_tables
    import release_signature
    from forge import load_recipe
    from title_release import compose_title_layout, restoration_members, retarget_recipe, title_release


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


# The llvm-mingw Windows product (XBUILD Q2, x86_64-w64-windows-gnu): Windows
# resolves a PE's DLL imports from the directory of the process executable, not
# from a parent folder, so every directory that holds an executable or a module
# with non-system imports carries its own copies: the root (the installed game
# executable, role root_runtime_library), runtime/ (runtime/TriAevum.exe, which
# Forge's preflight starts before installing, and iMMERSE) and forge/ (the game
# module). The loader maps one module per base name, so a process still shares
# one C++ runtime. Forge executables link libunwind statically and need neither.
MINGW_RUNTIME_LIBRARIES = ("libc++.dll", "libunwind.dll")
# Directory -> (DLL, role) copies beside the root ones (audit_release checks the
# same rule offline: release_policy.json "pe_imports").
MINGW_DIRECTORY_COPIES = (("runtime/", ("libc++.dll", "libunwind.dll", "shaderc_shared.dll"), "runtime_library"),
                          ("forge/", ("libc++.dll", "libunwind.dll"), "forge_runtime_module"))
MINGW_LICENSES = (("LICENSES/LLVM-Apache-2.0-WITH-LLVM-exception.txt",
                   "LICENSES/LLVM-Apache-2.0-WITH-LLVM-exception.txt"),
                  ("LICENSES/mingw-w64.txt", "LICENSES/mingw-w64.txt"))
MSVC_ONLY_SOURCES = {"build-release/msvcp140.dll", "build-release/vcruntime140.dll",
                     "build-release/vcruntime140_1.dll", "LICENSES/Microsoft-Visual-Cpp-Runtime.md"}


def mingw_runtime_artifacts(directory: Path) -> dict[str, Path]:
    """libc++.dll and libunwind.dll of the pinned llvm-mingw toolchain
    (<toolchain>/x86_64-w64-mingw32/bin), never copies found elsewhere."""
    artifacts = {name: directory / name for name in MINGW_RUNTIME_LIBRARIES}
    for name, source in artifacts.items():
        if not source.is_file() or source.is_symlink():
            raise ValueError(f"Missing llvm-mingw runtime library {name} in {directory}; "
                             "supply --mingw-runtime-dir pointing to <llvm-mingw>/x86_64-w64-mingw32/bin")
    return artifacts


def mingw_layout(files: list[dict]) -> list[dict]:
    """The MSVC layout turned into the llvm-mingw one: no Visual C++ runtime or
    its terms; shaderc_shared.dll moves to the root; libc++/libunwind join it,
    with the LLVM and mingw-w64 runtime licences."""
    result = []
    for item in files:
        if item["source"] in MSVC_ONLY_SOURCES:
            continue
        if item["source"] == "build-release/shaderc_shared.dll":
            item = {**item, "path": "shaderc_shared.dll", "role": "root_runtime_library"}
        result.append(item)
    result += [{"source": f"build-release/{name}", "path": name, "role": "root_runtime_library"}
               for name in MINGW_RUNTIME_LIBRARIES]
    result += [{"source": f"build-release/{name}", "path": directory + name, "role": role}
               for directory, names, role in MINGW_DIRECTORY_COPIES for name in names]
    result += [{"source": source, "path": path, "role": "license"} for source, path in MINGW_LICENSES]
    return result


def windows_layout(layout: dict, target: str) -> dict:
    """The release layout of the selected Windows target. The MSVC layout is
    returned as it is (its package keeps the implicit msvc target); the
    llvm-mingw one gets its own files and names its target, so that
    package_release and audit_release apply the gnu platform policy."""
    if target != WINDOWS_GNU.target:
        return layout
    return {**layout, "target": target, "files": mingw_layout(layout["files"])}


def explicit_dlss_targets(args) -> dict:
    """--dlss-module/--dlss-runtime/--dlss-license: the separately built DLSS
    module (the llvm-mingw cross-build builds it as its own MSVC-ABI DLL) with
    NVIDIA's runtime and licence, given together or not at all."""
    given = {key: getattr(args, key, None) for key in ("dlss_module", "dlss_runtime", "dlss_license")}
    if not any(given.values()):
        return {}
    if not all(given.values()):
        raise ValueError("--dlss-module, --dlss-runtime and --dlss-license are given together")
    return {"dlss": str(given["dlss_module"]), "dlss_runtime": str(given["dlss_runtime"]),
            "dlss_license": str(given["dlss_license"])}


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


def dlss_layout(targets: dict) -> list[dict]:
    """The DLSS module, NVIDIA's DLSS runtime and NVIDIA's DLSS SDK licence,
    together or not at all: the module without the runtime cannot offer DLSS,
    and neither is distributed without the licence."""
    keys = ("dlss", "dlss_runtime", "dlss_license")
    present = [key for key in keys if key in targets]
    if not present:
        return []
    if len(present) != len(keys):
        raise ValueError("The DLSS module, its NVIDIA runtime and licence must be published together")
    module, runtime, license_text = (Path(targets[key]).resolve() for key in keys)
    for path in (module, runtime, license_text):
        if not path.is_file():
            raise ValueError(f"DLSS release input is missing: {path}")
    return [
        {"source": str(module), "path": f"runtime/{module.name}", "role": "separately_licensed_module"},
        {"source": str(runtime), "path": f"runtime/{runtime.name}", "role": "separately_licensed_module"},
        {"source": str(license_text), "path": "LICENSES/NVIDIA-DLSS-SDK-LICENSE.txt", "role": "license"},
    ]


def prepare(args) -> dict:
    title = title_release(args.title)
    recipe_file = title["recipes"]
    recipe_id = args.recipe or title["default_recipe"]
    if args.title == "mm3d" and args.restoration_resources is None:
        raise ValueError("The MM3D release bundles Project Restoration: pass --restoration-resources")
    if args.title != "mm3d" and args.restoration_resources is not None:
        raise ValueError("--restoration-resources belongs to the MM3D release only")
    target = getattr(args, "target", None) or WINDOWS.target
    if target == WINDOWS_GNU.target:
        if args.mingw_runtime_dir is None or args.vc_redist_dir is not None:
            raise ValueError(f"{target} takes --mingw-runtime-dir, not --vc-redist-dir")
        crt_artifacts = mingw_runtime_artifacts(args.mingw_runtime_dir.resolve())
    elif target == WINDOWS.target:
        if args.vc_redist_dir is None or getattr(args, "mingw_runtime_dir", None) is not None:
            raise ValueError(f"{target} takes --vc-redist-dir")
        crt_artifacts = visual_cpp_runtime_artifacts(args.vc_redist_dir.resolve())
    else:
        raise ValueError(f"No Windows release for {target}")
    if getattr(args, "exe_runner", None) is not None:
        # The executables this script queries (the runtime's --product-info and
        # plugin preflight) run through it on a non-Windows build host.
        os.environ[EXE_RUNNER_VARIABLE] = str(args.exe_runner.resolve(strict=True))
    build_dir = args.build_dir.resolve()
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    # Reconfigure even on a no-op code build so the embedded commit and target
    # receipt cannot retain the previous Git HEAD.
    subprocess.run([str(args.cmake), "-S", str(ROOT), "-B", str(build_dir)], check=True)
    subprocess.run([str(args.cmake), "--build", str(build_dir), "--config", "Release",
                    "--target", "triaevum_public_runtime", "oot3d_game_module",
                    "oot3d_native_pica_aot_compiler",
                    "--parallel", "2"], check=True)
    targets = load_json_object(build_dir / "triaevum-runtime-targets-Release.json")
    if targets.get("format") != "triaevum_runtime_targets_v1":
        raise ValueError("CMake did not publish the product targets")
    dlss_inputs = explicit_dlss_targets(args)
    if dlss_inputs and any(key in targets for key in dlss_inputs):
        raise ValueError("The build already publishes a DLSS module; do not pass --dlss-* as well")
    targets.update(dlss_inputs)
    commit = targets["source_commit"]
    receipt = query_product(Path(targets["runtime"]), commit)
    if receipt["product"].get("private_title_loaded") is not False:
        raise ValueError("The build directory must contain the public stub, not a title")
    if receipt["product"]["capabilities"].get("experimental_global_illumination") is not False:
        raise ValueError("The release runtime must not offer the experimental global illumination "
                         "(configure with THREE_DS_RECOMP_OFFER_EXPERIMENTAL_GI=OFF)")
    if receipt["product"]["capabilities"].get("developer_tools") is not False:
        raise ValueError("The release runtime must not contain the developer tools "
                         "(configure with TRIAEVUM_DEVELOPER_TOOLS=OFF)")
    layout = load_json_object(ROOT / "tools/triaevum_release/runtime_release_layout.example.json")
    source_zip = work / "TriAevum-source.zip"
    create_source_archive(ROOT, source_zip, source_commit=commit, forge_profile=layout["forge_profile"])
    # The Rust Forge (window, launcher and command line) with its build receipt
    # and crate notices; the public import path runs no Python.
    layout = windows_layout(layout, target)
    forge = build_rust_forge(work / "forge-rust", target=target, source_commit=commit,
                             cargo=args.cargo, target_dir=args.cargo_target_dir,
                             features=title["forge_features"])
    layout["files"].extend(forge["files"])
    retarget_recipe(layout["files"], args.title)
    # The title's own README, documents and licences (C-DISTRO-2, 5..11); none for OoT3D.
    compose_title_layout(layout["files"], args.title)
    if args.restoration_resources is not None:
        layout["files"].extend(restoration_members(args.restoration_resources, work / "restoration-members"))
    artifacts = {
        "TriAevum.exe": Path(targets["runtime"]),
        "triaevum_title_aot.dll": Path(targets["stub"]),
        "TriAevum-source.zip": source_zip,
        "shaderc_shared.dll": Path(targets["runtime"]).parent / "shaderc_shared.dll",
        "oot3d_game_module.dll": build_dir / "oot3d_game_module.dll",
        **crt_artifacts,
    }
    for item in layout["files"]:
        if item["source"].startswith("build-release/"):
            item["source"] = str(artifacts[Path(item["source"]).name].resolve())
    # The separately licensed iMMERSE module (MXAO), its own library beside the
    # runtime in runtime/; its licences are already in the layout.
    if "immerse" in targets:
        layout["files"].append({"source": str(Path(targets["immerse"]).resolve()),
                                "path": "runtime/" + Path(targets["immerse"]).name,
                                "role": "separately_licensed_module"})
    # The separately licensed DLSS module, the only binary that links NVIDIA's
    # NGX SDK, beside NVIDIA's own DLSS runtime and the SDK licence they are
    # distributed under (runtime/three_ds_recomp/src/fast/dlss/LICENSE.md).
    layout["files"].extend(dlss_layout(targets))
    # FidelityFX's FSR runtime (MIT), which NRI loads by name beside the
    # runtime, on demand: FSR is offered only when it loads, so an older
    # Vulkan loader that lacks its imports only withdraws FSR.
    if "fidelityfx_runtime" in targets:
        layout["files"].append({"source": str(Path(targets["fidelityfx_runtime"]).resolve()),
                                "path": "runtime/" + Path(targets["fidelityfx_runtime"]).name,
                                "role": "optional_vulkan_runtime"})
    # The release's default scene environment profiles (read-only; the user's
    # own profiles in the data directory take precedence).
    layout["files"].extend(environment_profile_layout())
    layout_path = work / "release-layout.json"
    layout["files"].extend(title_layout(
        runtime=Path(targets["runtime"]), native_module=artifacts["oot3d_game_module.dll"],
        plugin_manifest=args.title_build, generated_manifest=args.title_sources,
        build_source=args.title_build_source, recipe=load_recipe(recipe_file, recipe_id),
        work=work / "precompiled", profile_data=args.title_profile))
    catalog_path = work / "precompiled/precompiled-titles.json"
    definitions = load_json_object(recipe_file)
    catalog, definitions = add_verified_variants(load_json_object(catalog_path), definitions, definitions)
    # Each title's reviewed extra inputs (OoT3D's input_coverage; none for MM3D).
    if title["input_coverage"] is not None:
        catalog, definitions, input_files = bind_input_coverage(catalog, definitions,
                                                                root=title["input_coverage"])
    else:
        input_files = []
    qualified_recipes = work / "qualified-recipes.json"
    atomic_write_json(qualified_recipes, definitions)
    for item in layout["files"]:
        if item["path"] == title["recipe_path"]:
            item["source"] = str(qualified_recipes.resolve())
    layout["files"].extend(input_files)
    catalog, shader_files = bind_renderer_compiler(catalog,
        Path(targets["runtime"]).parent / "oot3d_native_pica_aot_compiler.exe",
        [artifacts["shaderc_shared.dll"]])
    layout["files"].extend(shader_files)
    catalog, corpus_files = bind_shader_corpus(catalog, args.shader_pack,
        args.pipeline_manifest, args.pipeline_helper, qualification=args.shader_qualification,
        source_root=ROOT)
    layout["files"].extend(corpus_files)
    # The release's own interior-sun and scene-light results for the supported
    # rooms, keyed to their content (precomputed_tables).
    if args.precomputed is not None:
        catalog, table_files = bind_precomputed_tables(catalog, args.precomputed / "interior-sun.masks",
                                                       args.precomputed / "scene-lights.json")
        layout["files"].extend(table_files)
    atomic_write_json(catalog_path, catalog)
    atomic_write_json(layout_path, layout)
    signing_key = args.signing_key or (Path(os.environ[release_signature.KEY_VARIABLE])
                                       if os.environ.get(release_signature.KEY_VARIABLE) else None)
    if signing_key is None and not args.candidate:
        # Owner decision 10: Forge installs update packages only when signed.
        raise ValueError("a release must be signed: pass --signing-key or set "
                         f"{release_signature.KEY_VARIABLE} (docs/TRIAEVUM_UPDATE_SIGNING.md)")
    result = package_release(layout_path, args.output, source_root=ROOT,
                             version=args.version, source_commit=commit,
                             enforce_readiness=not args.candidate)
    if signing_key is not None:
        result["signature"] = str(release_signature.sign_package(args.output, signing_key))
    if args.archive is not None:
        result["archive"] = create_release_archive(args.output, args.archive)
    return result


ENVIRONMENTS = ROOT / "runtime/triaevum_resources/environments"


def environment_profile_layout(environments: Path = ENVIRONMENTS) -> list[dict]:
    """The release's default scene environment profiles: OoT3D's at the top
    level and every other title's beneath its adapter key (environments/mm3d),
    as TitleReleaseSceneEnvironmentDirectory reads them."""
    files = []
    for profile in sorted(environments.rglob("*.environment.json")):
        relative = profile.relative_to(environments).as_posix()
        files.append({"source": str(profile.resolve()),
                      "path": f"resources/environments/{relative}",
                      "role": "default_environment_profile"})
    return files


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--cmake", type=Path, required=True)
    parser.add_argument("--cargo", default="cargo", help="Cargo of the pinned Rust toolchain")
    parser.add_argument("--cargo-target-dir", type=Path, help="Cargo target folder for the Rust Forge build")
    parser.add_argument("--target", choices=(WINDOWS.target, WINDOWS_GNU.target), default=WINDOWS.target,
                        help="Windows product target (default MSVC; x86_64-w64-windows-gnu = llvm-mingw)")
    parser.add_argument("--vc-redist-dir", type=Path,
                        help="Licensed Visual Studio x64/Microsoft.VC143.CRT redistributable directory "
                             "(required for the MSVC target)")
    parser.add_argument("--mingw-runtime-dir", type=Path,
                        help="llvm-mingw <toolchain>/x86_64-w64-mingw32/bin (libc++.dll, libunwind.dll); "
                             "required for x86_64-w64-windows-gnu")
    parser.add_argument("--exe-runner", type=Path,
                        help="Runner for the Windows executables queried on a non-Windows build host "
                             "(e.g. a wine wrapper script)")
    parser.add_argument("--dlss-module", type=Path, help="Separately built triaevum_dlss.dll")
    parser.add_argument("--dlss-runtime", type=Path, help="NVIDIA nvngx_dlss.dll (verified at its build)")
    parser.add_argument("--dlss-license", type=Path, help="NVIDIA DLSS SDK licence text")
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--archive", type=Path, help="Optional audited portable ZIP outside the output directory")
    parser.add_argument("--signing-key", type=Path,
                        help="The publisher's private key, outside the repository (default: "
                             "$TRIAEVUM_RELEASE_SIGNING_KEY); required unless --candidate")
    parser.add_argument("--version", required=True)
    parser.add_argument("--candidate", action="store_true")
    parser.add_argument("--title-build", type=Path, required=True, help="Verified whole-aot-plugin.json")
    parser.add_argument("--title-sources", type=Path, required=True, help="Corresponding generated C++ manifest")
    parser.add_argument("--title-build-source", type=Path, required=True, help="Source ZIP used to build the title")
    parser.add_argument("--title-profile", type=Path,
                        help="Profile the title was built with (build-title --profile-data); required for a "
                             "profile-guided title, shipped with its translated source")
    parser.add_argument("--title", choices=("oot3d", "mm3d"), default="oot3d",
                        help="The title whose own Forge release is prepared (one title per release)")
    parser.add_argument("--recipe", help="Recipe id (default: the title's own recipe)")
    parser.add_argument("--restoration-resources", type=Path,
                        help="MM3D only: Project Restoration release tree (romfs/...), fingerprint-verified; "
                             "only its original members are packaged")
    parser.add_argument("--shader-pack", type=Path, required=True,
                        help="Publisher input: bundled portable corpus, never a user choice")
    parser.add_argument("--pipeline-manifest", type=Path, action="append", required=True)
    parser.add_argument("--shader-qualification", type=Path, required=True,
                        help="qualification.json written by regenerate_shader_corpus.py with the pack")
    parser.add_argument("--pipeline-helper", type=Path, required=True)
    parser.add_argument("--precomputed", type=Path,
                        help="Publisher directory from precomputed_tables (interior-sun.masks, scene-lights.json)")
    args = parser.parse_args()
    print(json.dumps(prepare(args), indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
