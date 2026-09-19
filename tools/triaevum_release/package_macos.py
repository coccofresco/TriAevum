"""Assemble a relocatable, locally signed Apple Silicon application."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import plistlib
import shutil
import subprocess
from pathlib import Path

from .audit_release import audit_release
from .common import atomic_write_json
from .macos_precompiled_catalog import create_catalog
from .source_archive import create_source_archive


def run(*args: str) -> str:
    return subprocess.check_output(args, text=True).strip()


def dependencies(path: Path) -> list[str]:
    return [line.strip().split(" (compatibility", 1)[0]
            for line in run("otool", "-L", str(path)).splitlines()[1:]]


def bundle_libraries(runtime: Path) -> list[dict]:
    library_dir = runtime / "lib"
    library_dir.mkdir()
    prefix = Path(run("brew", "--prefix"))
    molten = Path(run("brew", "--prefix", "molten-vk")) / "lib/libMoltenVK.dylib"
    pending = [(runtime / "TriAevum", runtime / "TriAevum"),
               (runtime / "triaevum_title_aot.dylib", runtime / "triaevum_title_aot.dylib")]
    for helper_name in ("oot3d_native_pica_aot_compiler",
                        "oot3d_native_nri_pipeline_prepare"):
        helper = runtime / "forge" / helper_name
        if helper.exists():
            pending.append((helper, helper))
    copied: dict[str, Path] = {}

    def copy(source: Path) -> Path:
        name = source.name
        if name in copied:
            if copied[name].resolve() != source.resolve():
                raise ValueError(f"Conflicting bundled library: {name}")
            return library_dir / name
        target = library_dir / name
        shutil.copy2(source, target)
        target.chmod(0o755)
        copied[name] = source
        pending.append((source, target))
        return target

    copy(molten)
    visited = set()
    while pending:
        source, target = pending.pop()
        if target in visited:
            continue
        visited.add(target)
        for dependency in dependencies(source):
            if dependency.startswith(("/usr/lib/", "/System/Library/")):
                continue
            if dependency.startswith("@rpath/"):
                candidate = source.parent / dependency.removeprefix("@rpath/")
                if not candidate.exists():
                    candidate = prefix / "lib" / Path(dependency).name
            elif dependency.startswith("@loader_path/"):
                candidate = source.parent / dependency.removeprefix("@loader_path/")
            else:
                candidate = Path(dependency)
            if not candidate.is_file():
                raise ValueError(f"Cannot resolve {dependency} for {source}")
            # A dylib's own install name is listed by otool as well.
            if candidate.resolve() == source.resolve():
                continue
            child = copy(candidate)
            relative = os.path.relpath(child, target.parent)
            subprocess.run(["install_name_tool", "-change", dependency,
                            "@loader_path/" + relative, str(target)], check=True)
        if target.suffix == ".dylib":
            subprocess.run(["install_name_tool", "-id", "@rpath/" + target.name, str(target)], check=True)
        subprocess.run(["codesign", "--force", "--sign", "-", str(target)], check=True)
    (library_dir / "MoltenVK_icd.json").write_text(json.dumps({
        "file_format_version": "1.0.0",
        "ICD": {"library_path": "./libMoltenVK.dylib", "api_version": "1.2.0",
                "is_portability_driver": True}}, indent=2) + "\n")
    return [{"name": name, "source": str(source),
             "sha256": hashlib.sha256((library_dir / name).read_bytes()).hexdigest()}
            for name, source in sorted(copied.items())]


def _copy(source: Path, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)


def _release_role(relative: str) -> str:
    runtime = "Contents/Resources/runtime/"
    path = relative.removeprefix(runtime)
    if relative == "Contents/MacOS/TriAevum": return "macos_launcher"
    if relative == "Contents/Resources/forge/TriAevumForge": return "forge_executable"
    if relative in {"Contents/Info.plist", "Contents/Resources/macos-build.json", "Contents/_CodeSignature/CodeResources"}: return "macos_metadata"
    if relative in {"Contents/Resources/LICENSE", "Contents/Resources/LICENSE_SCOPE.md", "Contents/Resources/THIRD_PARTY_NOTICES.md"} or relative.startswith("Contents/Resources/LICENSES/"): return "macos_notice"
    if relative == runtime + "lib/MoltenVK_icd.json": return "macos_icd"
    if path == "TriAevum": return "runtime_executable"
    if path == "forge/oot3d_game_module.dylib": return "forge_runtime_module"
    if path == "forge/oot3d_native_pica_aot_compiler": return "shader_preparation_tool"
    if path == "forge/oot3d_native_nri_pipeline_prepare": return "shader_preparation_tool"
    if path == "recipes/precompiled-titles.json": return "precompiled_catalog"
    if path.startswith("recipes/adapters/"): return "input_copy_adapter"
    if path.startswith("recipes/"): return "revision_recipe"
    if path == "forge/shader-corpus/portable.o3ps": return "portable_shader_corpus"
    if path.startswith("forge/shader-corpus/pipelines-"): return "portable_pipeline_recipes"
    if path.startswith("titles/") and path.endswith(".dylib"): return "precompiled_title"
    if path.startswith("source/titles/") and path.endswith("-translated.zip"): return "translated_title_source"
    if path.startswith("source/titles/") and path.endswith("-build.zip"): return "title_build_source"
    if path.startswith("source/") and path.endswith(".zip"): return "corresponding_source"
    if path.startswith("forge/") and "/_internal/" not in path: return "forge_runtime_module"
    if relative.startswith("Contents/Resources/forge/_internal/"): return "forge_frozen_resource"
    if path.endswith(".dylib"): return "runtime_library"
    if path.startswith("resources/"): return "title_neutral_resource"
    if path.startswith("LICENSES/") or path == "LICENSE": return "license"
    return "documentation"


def _write_release_manifest(app: Path, *, version: str, source_commit: str) -> None:
    files = []
    for path in sorted((item for item in app.rglob("*") if item.is_file()), key=lambda item: item.as_posix()):
        relative = path.relative_to(app).as_posix()
        if relative == "Contents/_CodeSignature/CodeResources":
            continue
        files.append({"path": relative, "role": _release_role(relative),
                      "bytes": path.stat().st_size, "sha256": hashlib.sha256(path.read_bytes()).hexdigest()})
    atomic_write_json(app / "Contents/Resources/release-manifest.json", {
        "format": "triaevum_public_release_manifest_v1",
        "release": {"name": "TriAevum", "version": version,
                    "target": "aarch64-apple-darwin", "source_commit": source_commit,
                    "distribution_model": "precompiled_title_rom_import_v1",
                    "contains_title_code": True, "contains_title_content": False,
                    "proprietary_sdk_included": False, "redistributable": True,
                    "qualification": "candidate"},
        "files": files})


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--title", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    build = args.build.resolve()
    app = args.output.resolve()
    if app.exists():
        raise ValueError(f"Output exists; use a new application path: {app}")
    contents = app / "Contents"
    resources = contents / "Resources"
    executable_dir = contents / "MacOS"
    executable_dir.mkdir(parents=True)
    runtime = resources / "runtime"
    runtime.mkdir(parents=True)
    shutil.copy2(build / "TriAevum", runtime / "TriAevum")
    shutil.copy2(args.title, runtime / "triaevum_title_aot.dylib")
    _copy(build / "oot3d_native_pica_aot_compiler", runtime / "forge/oot3d_native_pica_aot_compiler")
    _copy(build / "nri-pipeline/triaevum_nri_pipeline_prepare",
          runtime / "forge/oot3d_native_nri_pipeline_prepare")
    shutil.copytree(build / "installation/recipes", runtime / "recipes")
    shutil.copytree(build / "installation/resources", runtime / "resources")
    shutil.copytree(build / "installation/forge/shader-corpus", runtime / "forge/shader-corpus")
    shutil.copytree(build / "forge-dist/TriAevumForge", resources / "forge")
    reference = build / "qualified-inputs"
    if not reference.is_dir():
        raise ValueError("Current qualified release inputs are missing; rerun prepare_macos_inputs")
    shutil.copytree(reference / "source", runtime / "source")
    reference_catalog = json.loads((reference / "recipes/precompiled-titles.json").read_text())
    plugin_path = runtime / "titles" / str(reference_catalog["titles"][0]["plugin"]["path"]).split("titles/", 1)[1].replace(".dll", ".dylib")
    _copy(args.title, plugin_path)
    _copy(args.title, runtime / "forge/oot3d_game_module.dylib")
    for name in ("README.md", "LICENSE_SCOPE.md", "THIRD_PARTY_NOTICES.md", "SOURCE_OFFER.md", "LICENSE"):
        _copy(root / name, runtime / name)
    shutil.copytree(root / "LICENSES", runtime / "LICENSES")
    _copy(root / "tools/oot3d/third_party/azahar_audio/LICENSE.txt", runtime / "LICENSES/GPL-2.0-or-later.txt")
    for document in ("TRIAEVUM_PRECOMPILED_RELEASE.md", "OOT3D_MACOS_PORT.md", "TRIAEVUM_CONTRIBUTIONS.md"):
        _copy(root / "docs" / document, runtime / "docs" / document)
    inventory = bundle_libraries(runtime)
    source_commit = run("git", "rev-parse", "HEAD")
    create_source_archive(root, build / "macos-runtime-source.zip", source_commit=source_commit)
    _copy(build / "macos-runtime-source.zip", runtime / "source/TriAevum-source.zip")
    create_catalog(reference, runtime, plugin_path, source_commit=source_commit,
                   shader_compiler=runtime / "forge/oot3d_native_pica_aot_compiler",
                   pipeline_helper=runtime / "forge/oot3d_native_nri_pipeline_prepare",
                   pipeline_manifests=sorted((runtime / "forge/shader-corpus").glob("pipelines-*.json")))
    minimum = run("sw_vers", "-productVersion")
    with (contents / "Info.plist").open("wb") as stream:
        plistlib.dump({"CFBundleName": "TriAevum", "CFBundleDisplayName": "TriAevum",
                       "CFBundleIdentifier": "org.triaevum.macos", "CFBundleVersion": "1",
                       "CFBundleShortVersionString": "0.6.0-alpha.2-macos-dev",
                       "CFBundleExecutable": "TriAevum", "CFBundlePackageType": "APPL",
                       "NSHighResolutionCapable": True, "LSMinimumSystemVersion": minimum}, stream)
    subprocess.run(["swiftc", "-O", "-swift-version", "5", "-framework", "AppKit",
                    str(root / "ports/macos/Launcher.swift"), "-o", str(executable_dir / "TriAevum")], check=True)
    for name in ("LICENSE", "LICENSE_SCOPE.md", "THIRD_PARTY_NOTICES.md"):
        shutil.copy2(root / name, resources / name)
    shutil.copytree(root / "LICENSES", resources / "LICENSES")
    (resources / "macos-build.json").write_text(json.dumps({
        "format": "triaevum_macos_development_bundle_v1", "architecture": "arm64",
                       "upstream_release": "v0.6.0-alpha.2b", "upstream_commit": "57c9cca",
        "title_manifest_sha256": hashlib.sha256(
            (build / "translated-title-alpha2/TITLE_SOURCE_MANIFEST.json").read_bytes()
        ).hexdigest(),
        "minimum_macos": minimum, "source_commit": source_commit,
        "source_dirty": bool(run("git", "status", "--porcelain")),
        "libraries": inventory}, indent=2) + "\n")
    subprocess.run(["codesign", "--force", "--deep", "--sign", "-", str(app)], check=True)
    # Sign nested executables before recording their hashes. The final root-only
    # signature seals the manifest without rewriting those inventory entries.
    _write_release_manifest(app, version="0.6.0-alpha.2b-macos-candidate", source_commit=source_commit)
    subprocess.run(["codesign", "--force", "--sign", "-", str(app)], check=True)
    subprocess.run(["codesign", "--verify", "--deep", "--strict", str(app)], check=True)
    audit = audit_release(app)
    if not audit.ok:
        raise ValueError("macOS application failed shared release audit:\n" + "\n".join(audit.errors))
    print(app)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
