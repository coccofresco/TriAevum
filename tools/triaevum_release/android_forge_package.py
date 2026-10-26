"""Publisher-only Android Forge package: the catalog and data assets of the APK.

It binds the staged ARM64 libraries (tools/android/stage_native_app.py) to a
reference desktop release's revision recipes and data. It does not translate,
compile or run a title: the libraries cannot run on the publisher, so the
Android Forge verifies the runtime/title pair on the device before it installs.
The output folder holds `forge-package/`; pass the folder to Gradle as
-PtriaevumForgePackage, with the same stage as -PtriaevumNativeStage.
"""

import argparse
import copy
import json
import os
from pathlib import Path
import shutil
import struct
import zipfile

try:
    from .audit_release import DEFAULT_POLICY, build_machine_path
    from .common import atomic_write_json, load_json_object, normalize_relative_path, sha256_file
    from .precompiled_titles import load_catalog, checked_file, CATALOG
    from .precompiled_title_layout import artifact
    from .release_platform import ANDROID
except ImportError:
    from audit_release import DEFAULT_POLICY, build_machine_path
    from common import atomic_write_json, load_json_object, normalize_relative_path, sha256_file
    from precompiled_titles import load_catalog, checked_file, CATALOG
    from precompiled_title_layout import artifact
    from release_platform import ANDROID


ASSET_ROOT = "forge-package"
INVENTORY = "forge-package.json"
INVENTORY_FORMAT = "triaevum_android_forge_package_v1"
RELEASE_MANIFEST = "release-manifest.json"
LIBRARIES = "lib/arm64-v8a/"
TITLE_BUILD_TARGET = "ports/android/native"
# Reference release roles the Android Forge reads as package files.
DATA_ROLES = frozenset({
    "revision_recipe", "input_copy_adapter", "title_neutral_resource", "default_environment_profile",
    "precomputed_interior_sun", "precomputed_scene_lights", "portable_pipeline_recipes",
    "portable_shader_corpus", "portable_shader_qualification", "topscreen_singlescreen_asset",
})
# Desktop binaries, notices, documents and corresponding source stay out of the
# APK's assets; the catalog is rewritten. An unclassified role fails closed.
EXCLUDED_ROLES = frozenset({
    "license", "documentation", "runtime_executable", "runtime_library", "forge_executable",
    "forge_runtime_module", "shader_preparation_tool", "separately_licensed_module",
    "optional_vulkan_runtime", "precompiled_title", "precompiled_catalog",
    "corresponding_source", "translated_title_source", "title_build_source", "configuration",
    "forge_tool", "forge_tool_resource", "forge_toolchain_setup", "forge_link_library", "forge_clang_header",
    "forge_frozen_resource", "forge_command_line", "forge_build_receipt",
})


def require_arm64_library(path: Path):
    with path.open("rb") as stream:
        header = stream.read(20)
    if len(header) != 20 or header[:6] != b"\x7fELF\x02\x01":
        raise ValueError(f"Expected ELF64 little-endian artifact: {path}")
    kind, machine = struct.unpack_from("<HH", header, 16)
    if kind != 3 or machine != 183:
        raise ValueError(f"Expected an AArch64 shared library: {path}")


def _records(value):
    """Every file record of a catalog value: an object with a path and a hash."""
    if isinstance(value, dict):
        if isinstance(value.get("path"), str) and "sha256" in value:
            yield value
            return
        for item in value.values():
            yield from _records(item)
    elif isinstance(value, list):
        for item in value:
            yield from _records(item)


def _translated_source(reference_root: Path, title: dict) -> tuple[Path, tuple]:
    sources = [source for source in title.get("sources", []) if source["path"].endswith("-translated.zip")]
    if len(sources) != 1:
        raise ValueError("Every title must have one bound translated-source archive")
    archive_path = checked_file(reference_root, sources[0])
    with zipfile.ZipFile(archive_path) as archive:
        metadata = json.loads(archive.read("TITLE_SOURCE_MANIFEST.json"))
    build = metadata.get("build", {})
    files = metadata.get("files")
    if not isinstance(files, dict) or not files or not build.get("program_sha256"):
        raise ValueError("Reference title has no bound translated source inventory")
    if (metadata.get("format") != "triaevum_translated_title_source_v1" or
            build.get("plugin_sha256") != title["plugin"]["sha256"] or
            build.get("code_sha256") != title["inputs"]["code"]["sha256"] or
            build.get("translator_identity_sha256") != title["translator_identity_sha256"]):
        raise ValueError("Reference catalog does not match the rebuilt translated title")
    identity = (build["code_sha256"], build["program_sha256"], build["translator_identity_sha256"], files)
    return archive_path, identity


def _reference_data(reference_root: Path) -> dict[str, tuple[dict, Path]]:
    manifest = load_json_object(reference_root / RELEASE_MANIFEST)
    if manifest.get("format") != "triaevum_public_release_manifest_v1" or not isinstance(manifest.get("files"), list):
        raise ValueError("Reference package has no public release manifest")
    data = {}
    for item in manifest["files"]:
        role = item.get("role")
        if role in EXCLUDED_ROLES:
            continue
        if role not in DATA_ROLES:
            raise ValueError(f"Unclassified reference release role: {role}")
        relative = normalize_relative_path(item["path"])
        if relative.casefold() in (path.casefold() for path in data):
            raise ValueError(f"Duplicate reference release file: {relative}")
        record = {"path": relative, "role": role, "sha256": item["sha256"], "bytes": item["bytes"]}
        data[relative] = (record, checked_file(reference_root, record))
    return data


def create_package(reference_root: Path, native_stage: Path, output: Path, *, source_commit: str) -> dict:
    if len(source_commit) != 40 or any(c not in "0123456789abcdef" for c in source_commit):
        raise ValueError("Platform source commit must be a full Git commit ID")
    libraries = native_stage / "arm64-v8a"
    runtime = libraries / Path(ANDROID.runtime).name
    plugin = libraries / ANDROID.title_module
    # The Rust Forge library the installer activity loads (crates/forge-android).
    forge = libraries / Path(ANDROID.forge).name
    for path in (runtime, plugin, forge):
        require_arm64_library(path)
    # The Forge library ships in the APK: no home or registry paths of the build machine.
    leaked = build_machine_path(forge, load_json_object(DEFAULT_POLICY))
    if leaked is not None:
        raise ValueError(f"{forge.name} embeds a build-machine path ({leaked[:80]}); build it with "
                         "rust_forge_build.rustflags (CARGO_ENCODED_RUSTFLAGS) path remapping")
    reference = load_catalog(reference_root)
    data = _reference_data(reference_root)

    runtime_record = artifact(runtime, ANDROID.runtime)
    result = copy.deepcopy(reference)
    result.pop("native_module", None)
    result.update(target=ANDROID.target, runtime=runtime_record)
    execution_identity = None
    for title in result["titles"]:
        archive_path, identity = _translated_source(reference_root, title)
        if execution_identity is not None and identity != execution_identity:
            raise ValueError("This publisher operation accepts one shared execution image, not different title builds")
        execution_identity = identity
        title.update(target=ANDROID.target, plugin=artifact(plugin, LIBRARIES + ANDROID.title_module),
                     platform_build={"source_commit": source_commit,
                                     "translated_source_sha256": sha256_file(archive_path),
                                     "build_target": TITLE_BUILD_TARGET})
        # The runtime library carries the pass compiler and the device helper
        # as in-process tools: the shader compiler is the gameplay build's own.
        for key in ("renderer_shader_preparation", "shader_preparation"):
            if key in title:
                title[key].update(compiler=runtime_record, dependencies=[])
        if "device_pipeline_preparation" in title:
            title["device_pipeline_preparation"].update(helper=runtime_record, dependencies=[])

    # Every file the catalog names is either an APK library, corresponding
    # source published beside the APK, or a verified data file copied here.
    sources = {source["path"] for title in result["titles"] for source in title.get("sources", [])}
    named = [{key: value for key, value in title.items() if key != "sources"} for title in result["titles"]]
    for record in _records([result["runtime"], *named]):
        relative = normalize_relative_path(record["path"])
        if relative.startswith(LIBRARIES):
            if relative not in (ANDROID.runtime, LIBRARIES + ANDROID.title_module):
                raise ValueError(f"Catalog names an unstaged Android library: {relative}")
            continue
        entry = data.get(relative)
        if entry is None or entry[0]["sha256"] != record["sha256"] or entry[0]["bytes"] != record.get("bytes"):
            raise ValueError(f"Catalog data file is not in the reference release: {relative}")

    output = output.resolve()
    if output.exists() and any(output.iterdir()):
        raise ValueError(f"Output folder is not empty: {output}")
    staging = output.parent / f".{output.name}.staging"
    if staging.exists():
        shutil.rmtree(staging)
    package = staging / ASSET_ROOT
    for relative, (_, source) in sorted(data.items()):
        destination = package / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)
    atomic_write_json(package / CATALOG, result)
    inventory = {
        "format": INVENTORY_FORMAT,
        "target": ANDROID.target,
        "source_commit": source_commit,
        # The APK must carry these exact libraries (Gradle keeps them unstripped).
        "libraries": [runtime_record, result["titles"][0]["plugin"], artifact(forge, ANDROID.forge)],
        "files": [record for record, _ in (data[path] for path in sorted(data))],
        "corresponding_source": sorted(sources),
    }
    atomic_write_json(package / INVENTORY, inventory)
    if output.exists():
        output.rmdir()
    os.replace(staging, output)
    return inventory


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference-package", type=Path, required=True,
                        help="desktop release folder with release-manifest.json and its corresponding source")
    parser.add_argument("--native-stage", type=Path, required=True,
                        help="stage_native_app.py output (arm64-v8a/)")
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    inventory = create_package(args.reference_package, args.native_stage, args.output,
                               source_commit=args.source_commit)
    print(f"{args.output / ASSET_ROOT}: {len(inventory['files'])} data files")


if __name__ == "__main__":
    main()
