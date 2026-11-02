"""Publisher-only, allowlisted Apple Silicon Forge app candidate.

Reuses the Android publisher's reference-data and translated-source contracts.
Every executable is supplied prebuilt. This tool does not translate, compile,
download a toolchain, notarize or publish. Run it on macOS with a fresh output.
Signing and install-name changes precede every catalog/receipt hash.
"""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
import os
from pathlib import Path
import plistlib
import shutil
import stat
import subprocess
import sys
import zipfile

try:
    from .android_forge_package import DATA_ROLES, EXCLUDED_ROLES, _records, _translated_source
    from .audit_release import audit_release
    from .common import atomic_write_json, load_json_object, normalize_relative_path, release_manifest_path, sha256_file
    from .macos_macho_audit import EXTERNAL_FILES, FORMAT, LAUNCHER, MINIMUM_MACOS, RECEIPT, SYSTEM_PREFIXES, launcher_identity, require_macho, version
    from .macos_private_content import archive_errors, PRIVATE_DIRECTORY
    from .macos_notices import verified_notices
    from .precompiled_title_layout import artifact
    from .precompiled_titles import CATALOG, MODEL, checked_file, load_catalog
    from .product_contract import query_product
    from .release_platform import MACOS, host_platform
except ImportError:
    from android_forge_package import DATA_ROLES, EXCLUDED_ROLES, _records, _translated_source
    from audit_release import audit_release
    from common import atomic_write_json, load_json_object, normalize_relative_path, release_manifest_path, sha256_file
    from macos_macho_audit import EXTERNAL_FILES, FORMAT, LAUNCHER, MINIMUM_MACOS, RECEIPT, SYSTEM_PREFIXES, launcher_identity, require_macho, version
    from macos_private_content import archive_errors, PRIVATE_DIRECTORY
    from macos_notices import verified_notices
    from precompiled_title_layout import artifact
    from precompiled_titles import CATALOG, MODEL, checked_file, load_catalog
    from product_contract import query_product
    from release_platform import MACOS, host_platform


COMPILER = "forge/oot3d_native_pica_aot_compiler"
PIPELINE = "forge/oot3d_native_nri_pipeline_prepare"
CLI = "forge/triaevum-forge"
FORGE_RECEIPT = "forge/rust-forge.json"
FRAMEWORKS = ("libSDL2-2.0.0.dylib", "libvulkan.1.dylib", "libMoltenVK.dylib")
SOURCE_DOCUMENTS = ("LICENSE_SCOPE.md", "THIRD_PARTY_NOTICES.md", "SOURCE_OFFER.md",
                    "docs/TRIAEVUM_MACOS_INSTALL.md", "docs/TRIAEVUM_CONTRIBUTIONS.md",
                    "docs/AZAHAR_AUDIO_PROVENANCE.md", "docs/TRIAEVUM_RELEASE_APPROVAL_RECORD.md",
                    "docs/TRIAEVUM_PRECOMPILED_RELEASE.md")
# The Mac build disables this separately licensed module and ships no SDK payload.
EXCLUDED_REFERENCE_LICENSES = frozenset(("LICENSES/NVIDIA-DLSS-SDK-LICENSE.txt",
                                       "LICENSES/TriAevum-DLSS-module.md"))


def verified_source_documents(path: Path, source_commit: str) -> dict[str, bytes]:
    """Verify the exact source inventory, then obtain its notices unchanged."""
    documents = {}
    with zipfile.ZipFile(path) as source:
        metadata = json.loads(source.read("SOURCE_ARCHIVE_MANIFEST.json"))
        if (metadata.get("format") != "triaevum_corresponding_source_v1" or
                metadata.get("source_commit") != source_commit or
                metadata.get("target", MACOS.target) != MACOS.target):
            raise ValueError("Corresponding source does not match the platform build commit/target")
        errors = list(archive_errors(source))
        if errors:
            raise ValueError("\n".join(errors))
        aggregate = hashlib.sha256()
        seen, total = set(), 0
        for item in sorted(source.infolist(), key=lambda item: item.filename):
            relative = normalize_relative_path(item.filename)
            if item.is_dir():
                continue
            if (relative != item.filename or relative.casefold() in seen or
                    stat.S_ISLNK(item.external_attr >> 16)):
                raise ValueError("Unsafe or duplicate corresponding-source member")
            seen.add(relative.casefold())
            # Retain the explicit private-directory guard as well as text checks.
            if PRIVATE_DIRECTORY in Path(relative.casefold()).parts:
                raise ValueError("Private title sources cannot enter the public source archive")
            if relative == "SOURCE_ARCHIVE_MANIFEST.json":
                continue
            contents = source.read(item)
            aggregate.update(relative.encode("utf-8") + b"\0")
            aggregate.update(hashlib.sha256(contents).digest())
            total += len(contents)
            if relative in SOURCE_DOCUMENTS or relative == "LICENSE" or relative.startswith("LICENSES/"):
                documents[relative] = contents
        if (metadata.get("files") != len(seen) - 1 or metadata.get("uncompressed_bytes") != total or
                metadata.get("content_identity_sha256") != aggregate.hexdigest()):
            raise ValueError("Corresponding source inventory mismatch")
    for name in (*SOURCE_DOCUMENTS, "LICENSES/Rust-Forge-crates.md"):
        if name not in documents:
            raise ValueError("Corresponding source is missing publication document: " + name)
    return documents


def reference_files(reference: Path) -> dict:
    manifest = load_json_object(release_manifest_path(reference))
    if manifest.get("format") != "triaevum_public_release_manifest_v1":
        raise ValueError("Reference package has no public release manifest")
    selected = {}
    seen = set()
    for record in manifest.get("files", []):
        relative = normalize_relative_path(record["path"])
        if relative.casefold() in seen:
            raise ValueError("Duplicate reference package path")
        seen.add(relative.casefold())
        role = record["role"]
        if role not in DATA_ROLES | EXCLUDED_ROLES:
            raise ValueError(f"Unclassified reference release role: {role}")
        if role in DATA_ROLES or role == "license":
            selected[relative] = (role, checked_file(reference, record))
    return selected


def reference_license_documents(selected: dict, documents: dict[str, bytes]) -> dict[str, bytes]:
    """Resolve every retained reference notice against the verified build source."""
    required = {name for name, (role, _) in selected.items()
                if role == "license" and name not in EXCLUDED_REFERENCE_LICENSES}
    missing = required - documents.keys()
    if missing:
        raise ValueError("Corresponding source is missing reference licenses: " + ", ".join(sorted(missing)))
    return {name: documents[name] for name in sorted(required)}


def rebind_translated(reference: Path, title: dict, destination: Path, plugin: dict,
                      source_commit: str) -> tuple:
    """Reuse only manifest-verified translated C++ and identify this rebuild.

    A Windows plugin hash or its profile cannot describe the new signed dylib.
    The translation identity remains; current support/wrapper source is bound
    separately, and every generated source byte is checked before copying.
    """
    source, identity = _translated_source(reference, title)
    destination.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(source) as original, zipfile.ZipFile(destination, "w", zipfile.ZIP_DEFLATED) as output:
        metadata = json.loads(original.read("TITLE_SOURCE_MANIFEST.json"))
        files = metadata["files"]
        if len(original.namelist()) != len(set(original.namelist())):
            raise ValueError("Duplicate translated-source archive member")
        for name, digest in sorted(files.items()):
            relative = normalize_relative_path(name)
            if "/" in relative or Path(relative).suffix not in (".cpp", ".h"):
                raise ValueError("Only declared translated C++ sources may be promoted")
            contents = original.read(relative)
            if hashlib.sha256(contents).hexdigest() != digest:
                raise ValueError("Translated source inventory mismatch")
            output.writestr(relative, contents)
        old_build = metadata["build"]
        build = {key: old_build[key] for key in ("code_sha256", "program_sha256", "translator_identity_sha256",
                                                "shards", "functions") if key in old_build}
        build.update(format="triaevum_generated_cpp_whole_aot_plugin_v2", target=MACOS.target,
                     profile=MACOS.profile, plugin_sha256=plugin["sha256"], plugin_bytes=plugin["bytes"],
                     optimization="-O1 -ffp-model=strict -ffp-contract=off", build_target="ports/macos/title")
        metadata = {"format": "triaevum_translated_title_source_v1", "recipe": title["recipe"],
                    "build_source_commit": source_commit, "build": build, "files": files,
                    "reference_translated_source_sha256": sha256_file(source)}
        output.writestr("TITLE_SOURCE_MANIFEST.json", json.dumps(metadata, indent=2) + "\n")
        output.writestr("README.md", "# Translated title logic\n\n"
                        "These are game-derived translated sources, not title-neutral code.\n"
                        "Rebuild with ports/macos/title from the paired build source archive,\n"
                        "arm64, the recorded strict floating-point flags and macOS deployment target.\n"
                        "Original-game rights are not relicensed by the TriAevum project license.\n")
    return identity


def normalize_native(path: Path, frameworks: Path, minimum: str):
    with path.open("rb") as stream:
        magic = stream.read(4)
    if magic in (b"\xca\xfe\xba\xbe", b"\xca\xfe\xba\xbf", b"\xbe\xba\xfe\xca", b"\xbf\xba\xfe\xca"):
        # MoltenVK's upstream distribution is universal. Ship only its arm64
        # slice, then rewrite/sign it and hash those actual packaged bytes.
        temporary = path.with_name(path.name + ".arm64")
        subprocess.run(["/usr/bin/lipo", str(path), "-thin", "arm64", "-output", str(temporary)], check=True)
        temporary.replace(path)
    info = require_macho(path, minimum_macos=minimum)
    changes = []
    for dependency in info["dependencies"]:
        if dependency.startswith(SYSTEM_PREFIXES):
            continue
        name = Path(dependency).name
        if name not in FRAMEWORKS:
            raise ValueError(f"Unstaged native dependency: {path.name}: {dependency}")
        replacement = "@rpath/" + name
        if dependency != replacement:
            changes += ["-change", dependency, replacement]
    for rpath in dict.fromkeys(info["rpaths"]):
        changes += ["-delete_rpath", rpath]
    if any(not dependency.startswith(SYSTEM_PREFIXES) for dependency in info["dependencies"]):
        relative = os.path.relpath(frameworks, path.parent).replace(os.sep, "/")
        changes += ["-add_rpath", "@loader_path/" + relative]
    if info["kind"] == 6:
        changes += ["-id", "@rpath/" + path.name]
    # The launcher and Rust frontends use only system libraries. Avoid adding
    # an unused load command (small launchers may have no spare header space).
    if changes:
        subprocess.run(["/usr/bin/install_name_tool", *changes, str(path)], check=True)
    subprocess.run(["/usr/bin/codesign", "--force", "--sign", "-", "--timestamp=none", str(path)], check=True)


def create_package(*, reference: Path, native: dict[str, Path], forge_bundle: Path, source_archive: Path,
                   source_commit: str, output: Path, release_version: str,
                   moltenvk_license: Path, vulkan_license: Path, minimum_macos: str = MINIMUM_MACOS) -> dict:
    if host_platform() != MACOS:
        raise ValueError("macOS signing and qualification must run on Apple Silicon macOS")
    if len(source_commit) != 40 or any(c not in "0123456789abcdef" for c in source_commit):
        raise ValueError("Source commit must be a full Git commit ID")
    if version(minimum_macos) < version(MINIMUM_MACOS):
        raise ValueError("The macOS package cannot claim a floor below " + MINIMUM_MACOS)
    required = {MACOS.runtime, MACOS.native_module, MACOS.title_module, LAUNCHER, COMPILER, PIPELINE, *FRAMEWORKS}
    if set(native) != required:
        raise ValueError("Incomplete or unclassified macOS native inputs")
    source_documents = verified_source_documents(source_archive, source_commit)
    donor_notices = verified_notices(source_documents, moltenvk_license, vulkan_license)
    reference_catalog = load_catalog(reference)
    selected = reference_files(reference)
    reference_notices = reference_license_documents(selected, source_documents)
    receipt = load_json_object(forge_bundle / FORGE_RECEIPT)
    if receipt.get("target") != MACOS.target or receipt.get("source_commit") != source_commit:
        raise ValueError("Rust Forge build receipt does not match the platform source")
    forge_records = receipt.get("binaries", [])
    if {item["path"] for item in forge_records} != {MACOS.forge, CLI} or len(forge_records) != 2:
        raise ValueError("Rust Forge receipt must bind the desktop and command line binaries")
    for item in forge_records:
        checked_file(forge_bundle, item)
    output = output.resolve()
    if output.exists():
        raise ValueError("Output must be a new folder; existing output is not overwritten")
    output.mkdir(parents=True)
    # Leave failed staging available for diagnosis, never promotion. The
    # publisher retires its disposable payload after recording the failure;
    # see the development artifact retention procedure in the release guide.
    app = output / "TriAevum.app"
    root = app / "Contents/Resources"
    frameworks = app / "Contents/Frameworks"
    root.mkdir(parents=True)
    frameworks.mkdir()
    (app / "Contents/MacOS").mkdir()
    roles = {}

    def copy_file(source, relative, role):
        relative = normalize_relative_path(relative)
        if relative.casefold() in {path.casefold() for path in roles}:
            raise ValueError(f"Duplicate staged package file: {relative}")
        destination = root / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        if isinstance(source, bytes):
            destination.write_bytes(source)
        else:
            shutil.copyfile(source, destination)
        roles[relative] = role
        return destination

    for relative, (role, source) in sorted(selected.items()):
        # These are replaced with the actual platform build's license inputs.
        if relative not in ("LICENSES/Rust-Forge-crates.md", *donor_notices, *EXCLUDED_REFERENCE_LICENSES):
            copy_file(reference_notices[relative] if role == "license" else source, relative, role)
    copy_file(source_documents["LICENSES/Rust-Forge-crates.md"], "LICENSES/Rust-Forge-crates.md", "license")
    for name, contents in donor_notices.items():
        copy_file(contents, name, "license")
    for name in SOURCE_DOCUMENTS:
        copy_file(source_documents[name], name if name.startswith("docs/") else "docs/" + name, "documentation")
    copy_file(source_documents["docs/TRIAEVUM_MACOS_INSTALL.md"], "README.md", "documentation")
    copy_file(source_archive, "source/TriAevum-source.zip", "corresponding_source")

    execution = reference_catalog["titles"][0]["inputs"]["code"]["sha256"]
    if len(execution) != 64 or any(c not in "0123456789abcdef" for c in execution):
        raise ValueError("Invalid execution identity")
    title_path = f"titles/{execution}/{MACOS.title_module}"
    binaries = []
    for name, source in native.items():
        if name == LAUNCHER:
            destination = app / "Contents/MacOS" / LAUNCHER
            shutil.copyfile(source, destination)
        elif name in FRAMEWORKS:
            destination = frameworks / name
            shutil.copyfile(source, destination)
        else:
            relative = title_path if name == MACOS.title_module else name
            role = ("precompiled_title" if name == MACOS.title_module else
                    "runtime_executable" if name == MACOS.runtime else
                    "forge_runtime_module" if name == MACOS.native_module else "shader_preparation_tool")
            destination = copy_file(source, relative, role)
        destination.chmod(0o755)
        binaries.append(destination)
    for item in forge_records:
        destination = copy_file(forge_bundle / item["path"], item["path"], item["role"])
        destination.chmod(0o755)
        binaries.append(destination)
    for path in binaries:
        normalize_native(path, frameworks, minimum_macos)

    plist = {"CFBundleExecutable": LAUNCHER, "CFBundleIdentifier": "io.github.coccofresco.TriAevum",
             "CFBundleName": "TriAevum", "CFBundleDisplayName": "TriAevum", "CFBundlePackageType": "APPL",
             "CFBundleShortVersionString": release_version, "CFBundleVersion": release_version,
             "LSMinimumSystemVersion": minimum_macos, "LSApplicationCategoryType": "public.app-category.games",
             "NSHighResolutionCapable": True, "NSQuitAlwaysKeepsWindows": False}
    (app / "Contents/Info.plist").write_bytes(plistlib.dumps(plist))
    atomic_write_json(root / "config/vulkan/MoltenVK_icd.json", {
        "file_format_version": "1.0.0", "ICD": {"library_path": "../../../Frameworks/libMoltenVK.dylib",
                                                  "api_version": "1.4.0", "is_portability_driver": True}})
    roles["config/vulkan/MoltenVK_icd.json"] = "configuration"
    receipt = copy.deepcopy(receipt)
    receipt["binaries"] = [dict(artifact(root / item["path"], item["path"]), role=item["role"])
                           for item in forge_records]
    receipt["packaging"] = "macos-install-names-and-ad-hoc-signing"
    atomic_write_json(root / FORGE_RECEIPT, receipt)
    roles[FORGE_RECEIPT] = "forge_build_receipt"
    plugin = artifact(root / title_path, title_path)
    catalog = copy.deepcopy(reference_catalog)
    catalog.update(target=MACOS.target, runtime=artifact(root / MACOS.runtime, MACOS.runtime),
                   native_module=artifact(root / MACOS.native_module, MACOS.native_module))
    identity = None
    for title in catalog["titles"]:
        name = normalize_relative_path(title["recipe"])
        if "/" in name:
            raise ValueError("Recipe ID must be a single path component")
        translated = f"source/titles/{name}-translated.zip"
        rebuilt_identity = rebind_translated(reference, title, root / translated, plugin, source_commit)
        if identity is not None and identity != rebuilt_identity:
            raise ValueError("One shared module cannot represent different translated execution images")
        identity = rebuilt_identity
        build_source = f"source/titles/{name}-build.zip"
        copy_file(source_archive, build_source, "title_build_source")
        roles[translated] = "translated_title_source"
        title.update(target=MACOS.target, plugin=plugin, build_source_commit=source_commit,
                     sources=[artifact(root / translated, translated), artifact(root / build_source, build_source)],
                     platform_build={"source_commit": source_commit, "build_target": "ports/macos/title",
                                     "minimum_macos": minimum_macos})
        for key in ("renderer_shader_preparation", "shader_preparation"):
            if key in title:
                title[key].update(compiler=artifact(root / COMPILER, COMPILER), dependencies=[])
        if "device_pipeline_preparation" in title:
            title["device_pipeline_preparation"].update(helper=artifact(root / PIPELINE, PIPELINE), dependencies=[])
    # No stale foreign executable record may survive in a copied binding.
    for record in _records(catalog):
        checked_file(root, record)
    atomic_write_json(root / CATALOG, catalog)
    roles[CATALOG] = "precompiled_catalog"
    contract = query_product(root / MACOS.runtime, source_commit)
    query_product(root / MACOS.runtime, source_commit, plugin=root / title_path)
    outer = [artifact(app / name, name) for name in sorted(EXTERNAL_FILES)]
    for item in outer:
        if item["path"] == "Contents/MacOS/" + LAUNCHER:
            item.update(launcher_identity(app / item["path"]))
    atomic_write_json(root / RECEIPT, {"format": FORMAT, "minimum_macos": minimum_macos,
                                      "signing": "ad-hoc", "notarized": False,
                                      "files": outer})
    roles[RECEIPT] = "documentation"
    inventory = [dict(artifact(root / name, name), role=role) for name, role in sorted(roles.items())]
    atomic_write_json(root / "release-manifest.json", {
        "format": "triaevum_public_release_manifest_v1",
        "release": {"name": "TriAevum", "version": release_version, "target": MACOS.target,
                    "source_commit": source_commit, "minimum_macos": minimum_macos,
                    "distribution_model": MODEL, "contains_title_code": True, "contains_title_content": False,
                    "proprietary_sdk_included": False, "redistributable": True, "qualification": "candidate",
                    "forge_profile": "rust", "runtime_contract": contract},
        "files": inventory})
    subprocess.run(["/usr/bin/codesign", "--force", "--sign", "-", "--timestamp=none", str(app)], check=True)
    result = audit_release(root)
    if not result.ok:
        raise ValueError("macOS candidate failed release audit:\n" + "\n".join(result.errors))
    archive = output / "TriAevum-macos-arm64.zip"
    subprocess.run(["/usr/bin/ditto", "-c", "-k", "--norsrc", "--noextattr", "--keepParent", str(app), str(archive)],
                   check=True)
    return {"status": "candidate", "app": str(app), "archive": str(archive), "sha256": sha256_file(archive),
            "audit": result.summary}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("reference", "forge-bundle", "source-archive", "output", "moltenvk-license", "vulkan-license"):
        parser.add_argument("--" + name, type=Path, required=True)
    mapping = {"runtime": MACOS.runtime, "native-module": MACOS.native_module, "title": MACOS.title_module, "launcher": LAUNCHER,
               "shader-compiler": COMPILER, "pipeline-helper": PIPELINE, "sdl": FRAMEWORKS[0],
               "vulkan-loader": FRAMEWORKS[1], "moltenvk": FRAMEWORKS[2]}
    for name in mapping:
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--version", dest="release_version", required=True)
    parser.add_argument("--minimum-macos", default=MINIMUM_MACOS)
    args = vars(parser.parse_args())
    native = {relative: args.pop(name.replace("-", "_")) for name, relative in mapping.items()}
    print(json.dumps(create_package(native=native, **args), indent=2))


if __name__ == "__main__":
    main()
