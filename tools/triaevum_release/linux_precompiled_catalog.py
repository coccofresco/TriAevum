"""Publisher-only Linux catalog binding; does not translate or compile a title."""

import argparse
import copy
import json
from pathlib import Path
import struct
import zipfile

try:
    from .common import atomic_write_json, sha256_file
    from .precompiled_titles import load_catalog, checked_file, CATALOG
    from .precompiled_title_layout import artifact
    from .product_contract import query_product
    from .release_platform import LINUX, host_platform
except ImportError:
    from common import atomic_write_json, sha256_file
    from precompiled_titles import load_catalog, checked_file, CATALOG
    from precompiled_title_layout import artifact
    from product_contract import query_product
    from release_platform import LINUX, host_platform


def require_elf(path: Path, *, shared: bool):
    with path.open("rb") as stream:
        header = stream.read(20)
    if len(header) != 20 or header[:6] != b"\x7fELF\x02\x01":
        raise ValueError(f"Expected ELF64 little-endian artifact: {path}")
    kind, machine = struct.unpack_from("<HH", header, 16)
    if machine != 62 or kind not in ((3,) if shared else (2, 3)):
        raise ValueError(f"Expected x86-64 Linux artifact: {path}")


def create_catalog(reference_root: Path, installation: Path, plugin: Path, *, source_commit: str):
    if host_platform() != LINUX:
        raise ValueError("Linux catalog qualification must run on Linux")
    if len(source_commit) != 40 or any(c not in "0123456789abcdef" for c in source_commit):
        raise ValueError("Platform source commit must be a full Git commit ID")
    reference = load_catalog(reference_root)
    runtime = installation / LINUX.runtime
    native = installation / LINUX.native_module
    for path, shared in ((runtime, False), (native, True), (plugin, True)):
        require_elf(path, shared=shared)
    relative_plugin = plugin.resolve().relative_to(installation.resolve()).as_posix()
    if not relative_plugin.startswith("titles/") or plugin.name != LINUX.title_module:
        raise ValueError("The Linux title must be staged under titles/ with its native filename")
    result = copy.deepcopy(reference)
    result.update(target=LINUX.target, runtime=artifact(runtime, LINUX.runtime),
                  native_module=artifact(native, LINUX.native_module))
    execution_identity = None
    for item in result["titles"]:
        sources = [source for source in item.get("sources", []) if source["path"].endswith("-translated.zip")]
        if len(sources) != 1:
            raise ValueError("Every title must have one bound translated-source archive")
        archive_path = checked_file(reference_root, sources[0])
        with zipfile.ZipFile(archive_path) as archive:
            metadata = json.loads(archive.read("TITLE_SOURCE_MANIFEST.json"))
        build = metadata["build"]
        files = metadata.get("files")
        if not isinstance(files, dict) or not files or not build.get("program_sha256"):
            raise ValueError("Reference title has no bound translated source inventory")
        if (metadata.get("format") != "triaevum_translated_title_source_v1" or
                build.get("plugin_sha256") != item["plugin"]["sha256"] or
                build.get("code_sha256") != item["inputs"]["code"]["sha256"] or
                build.get("translator_identity_sha256") != item["translator_identity_sha256"]):
            raise ValueError("Reference catalog does not match the rebuilt translated title")
        identity = (build.get("code_sha256"), build.get("program_sha256"),
                    build.get("translator_identity_sha256"), files)
        if execution_identity is not None and identity != execution_identity:
            raise ValueError("This publisher operation accepts one shared execution image, not different title builds")
        execution_identity = identity
        # Preserve the original translation's provenance, separately from the
        # platform rebuild. Native ABI preflight below checks the actual .so.
        item.update(target=LINUX.target, plugin=artifact(plugin, relative_plugin),
                    platform_build={"source_commit": source_commit,
                                    "translated_source_sha256": sha256_file(archive_path),
                                    "build_target": "tools/triaevum_release/linux_title"})
    query_product(runtime, plugin=plugin)
    atomic_write_json(installation / CATALOG, result)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference-package", type=Path, required=True)
    parser.add_argument("--installation", type=Path, required=True)
    parser.add_argument("--plugin", type=Path, required=True)
    parser.add_argument("--source-commit", required=True)
    args = parser.parse_args()
    create_catalog(args.reference_package, args.installation, args.plugin, source_commit=args.source_commit)
    print(args.installation / CATALOG)


if __name__ == "__main__":
    main()
