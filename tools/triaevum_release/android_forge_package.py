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
from pathlib import Path, PurePosixPath
import shutil
import struct
import zipfile

try:
    from .audit_release import DEFAULT_POLICY, build_machine_path
    from .common import atomic_write_json, load_json_object, normalize_relative_path, release_manifest_path, sha256_file
    from .precompiled_titles import load_catalog, checked_file, CATALOG
    from .precompiled_title_layout import artifact
    from .release_platform import ANDROID
    from . import release_signature
except ImportError:
    from audit_release import DEFAULT_POLICY, build_machine_path
    from common import atomic_write_json, load_json_object, normalize_relative_path, release_manifest_path, sha256_file
    from precompiled_titles import load_catalog, checked_file, CATALOG
    from precompiled_title_layout import artifact
    from release_platform import ANDROID
    import release_signature


ASSET_ROOT = "forge-package"
INVENTORY = "forge-package.json"
INVENTORY_FORMAT = "triaevum_android_forge_package_v1"
RELEASE_MANIFEST = "release-manifest.json"
# Publisher signature of the inventory's exact bytes, beside it in the package
# folder; forge-android verifies it (package_signature.rs) before Forge reads a file.
SIGNATURE = "release-signature.json"
LIBRARIES = "lib/arm64-v8a/"
TITLE_BUILD_TARGET = "ports/android/native"
# Reference release roles the Android Forge reads as package files.
DATA_ROLES = frozenset({
    "revision_recipe", "input_copy_adapter", "title_neutral_resource", "default_environment_profile",
    "precomputed_interior_sun", "precomputed_scene_lights", "portable_pipeline_recipes",
    "portable_shader_corpus", "portable_shader_qualification", "topscreen_singlescreen_asset",
    "restoration_layout_recipe", "restoration_patch", "restoration_original_member",
})
# Desktop binaries, notices, documents and corresponding source stay out of the
# APK's assets; the catalog is rewritten. An unclassified role fails closed.
EXCLUDED_ROLES = frozenset({
    "license", "documentation", "runtime_executable", "runtime_library", "forge_executable",
    "forge_runtime_module", "shader_preparation_tool", "separately_licensed_module",
    "optional_vulkan_runtime", "precompiled_title", "precompiled_catalog",
    "corresponding_source", "translated_title_source", "title_build_source", "configuration",
    "forge_tool", "forge_tool_resource", "forge_toolchain_setup", "forge_link_library", "forge_clang_header",
    "forge_frozen_resource", "forge_command_line", "forge_build_receipt", "forge_update_helper",
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


# One APK is one product: the reference release must be the desktop release of
# that title (titles/<title>/product.json), named by its revision recipe.
TITLES = ("oot3d", "mm3d")
NOTICES = "notices"
# Game data never enters the APK: the Android Forge produces it on the device.
FORBIDDEN_DATA_SUFFIXES = (".gar", ".mfpj", ".3ds", ".cci", ".cia", ".cxi", ".cfa", ".romfs", ".zar")
FORBIDDEN_DATA_COMPONENTS = ("romfs", "exefs")


def package_title(data: dict, title: str | None) -> str:
    """The title of the reference release (its revision recipe recipes/<title>.json)."""
    named = sorted({PurePosixPath(path).stem for path, (record, _) in data.items()
                    if record["role"] == "revision_recipe" and PurePosixPath(path).stem in TITLES})
    if len(named) != 1:
        raise ValueError(f"Reference release must hold the revision recipe of exactly one title, not {named}")
    if title is not None and named[0] != title:
        raise ValueError(f"Reference release is the {named[0]} release, not {title}")
    return named[0]


def forbidden_data(data: dict) -> list[str]:
    hits = []
    for path in data:
        parts = PurePosixPath(path.casefold()).parts
        if parts[-1].endswith(FORBIDDEN_DATA_SUFFIXES) or any(part in FORBIDDEN_DATA_COMPONENTS for part in parts[:-1]):
            hits.append(path)
    return hits


def _reference_notices(reference_root: Path) -> dict[str, tuple[dict, Path]]:
    """The licences and notices the desktop package carries (copied beside forge-package/)."""
    manifest = load_json_object(release_manifest_path(reference_root))
    notices = {}
    for item in manifest["files"]:
        relative = normalize_relative_path(item["path"])
        name = PurePosixPath(relative).name
        if item.get("role") == "license" or (item.get("role") == "documentation" and name in (
                "THIRD_PARTY_NOTICES.md", "LICENSE_SCOPE.md", "SOURCE_OFFER.md")):
            record = {"path": relative, "role": item["role"], "sha256": item["sha256"], "bytes": item["bytes"]}
            notices[relative] = (record, checked_file(reference_root, record))
    if not any(record["role"] == "license" for record, _ in notices.values()):
        raise ValueError("Reference release carries no licence files")
    return notices


def _reference_data(reference_root: Path) -> dict[str, tuple[dict, Path]]:
    manifest = load_json_object(release_manifest_path(reference_root))
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


def create_package(reference_root: Path, native_stage: Path, output: Path, *, source_commit: str,
                   title: str | None = None) -> dict:
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
    product = package_title(data, title)
    refused = forbidden_data(data)
    if refused:
        raise ValueError(f"Reference release data holds game content: {refused[:5]}")
    notices = _reference_notices(reference_root)

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
        "title": product,
        "notices": [record for record, _ in (notices[path] for path in sorted(notices))],
    }
    atomic_write_json(package / INVENTORY, inventory)
    for relative, (_, source) in sorted(notices.items()):
        destination = staging / NOTICES / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)
    if output.exists():
        output.rmdir()
    os.replace(staging, output)
    return inventory


def sign_android_package(package_dir: Path, private_key: Path,
                         keys_path: Path = release_signature.KEYS) -> Path:
    """Signs forge-package.json in `package_dir` with a key Forge trusts."""
    package_dir = package_dir.resolve()
    release_signature._outside(private_key, release_signature.REPO_ROOT, package_dir)
    inventory = (package_dir / INVENTORY).read_bytes()
    document = release_signature.sign_manifest(inventory, private_key)
    keys = release_signature.load_keys(keys_path)
    if document["key_id"] not in keys:
        raise release_signature.SigningError(f"key {document['key_id']} is not a key Forge trusts")
    payload = json.dumps(document, indent=2).encode("utf-8") + b"\n"
    errors = release_signature.signature_errors(inventory, payload, keys)
    if errors:
        raise release_signature.SigningError("; ".join(errors))
    target = package_dir / SIGNATURE
    target.write_bytes(payload)
    return target


def verify_android_package(package_dir: Path, keys_path: Path = release_signature.KEYS) -> list[str]:
    """The checks forge-android makes on the device: signature, then every file."""
    signature = package_dir / SIGNATURE
    if not signature.is_file():
        return [f"the Android Forge package is not signed ({SIGNATURE} is missing)"]
    inventory = (package_dir / INVENTORY).read_bytes()
    errors = release_signature.signature_errors(inventory, signature.read_bytes(),
                                                release_signature.load_keys(keys_path))
    if errors:
        return errors
    for record in json.loads(inventory)["files"]:
        relative = normalize_relative_path(record["path"])
        path = package_dir / relative
        if not path.is_file() or path.stat().st_size != record["bytes"] or sha256_file(path) != record["sha256"]:
            errors.append(f"{relative} does not match the signed inventory")
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference-package", type=Path, required=True,
                        help="desktop release folder with release-manifest.json and its corresponding source")
    parser.add_argument("--native-stage", type=Path, required=True,
                        help="stage_native_app.py output (arm64-v8a/)")
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--title", choices=TITLES,
                        help="the product of the APK; the reference release must be that title's")
    parser.add_argument("--sign", action="store_true",
                        help=f"sign the inventory with the key named by ${release_signature.KEY_VARIABLE}")
    args = parser.parse_args()
    key = os.environ.get(release_signature.KEY_VARIABLE)
    if args.sign and not key:
        parser.error(f"--sign needs ${release_signature.KEY_VARIABLE}")
    inventory = create_package(args.reference_package, args.native_stage, args.output,
                               source_commit=args.source_commit, title=args.title)
    print(f"{args.output / ASSET_ROOT}: {len(inventory['files'])} data files")
    if args.sign:
        print(sign_android_package(args.output / ASSET_ROOT, Path(key)))


if __name__ == "__main__":
    main()
