"""Bind the current qualified title catalog to a signed Apple Silicon bundle.

This is deliberately a publisher adapter.  It preserves the supported-ROM and
translated-source records from the current qualified release; only host binary
records are replaced after Mach-O and ABI preflight checks.
"""
from __future__ import annotations

import copy
import json
import struct
import zipfile
from pathlib import Path

from .common import atomic_write_json, sha256_file
from .precompiled_title_layout import artifact
from .precompiled_titles import CATALOG, checked_file, load_catalog
from .product_contract import query_product
from .release_platform import MACOS, host_platform
from .shader_release_layout import bind_renderer_compiler
from .shader_corpus_layout import bind_shader_corpus


def require_arm64_macho(path: Path, *, executable: bool = False) -> None:
    """Reject thin foreign binaries before recording them in a Mac catalog."""
    header = path.read_bytes()[:32]
    if len(header) < 28 or header[:4] != b"\xcf\xfa\xed\xfe":
        raise ValueError(f"Expected arm64 Mach-O artifact: {path}")
    cpu_type = struct.unpack_from("<I", header, 4)[0]
    file_type = struct.unpack_from("<I", header, 12)[0]
    if cpu_type != 0x0100000C or (executable and file_type != 2) or (not executable and file_type not in (2, 6, 8)):
        raise ValueError(f"Expected {'executable ' if executable else ''}arm64 Mach-O artifact: {path}")


def rebind_translated_source(source: Path, plugin: Path) -> None:
    """Keep the source inventory while binding its build receipt to the Mac dylib."""
    temporary = source.with_suffix(".tmp")
    with zipfile.ZipFile(source) as original, zipfile.ZipFile(temporary, "w", zipfile.ZIP_DEFLATED) as replacement:
        for entry in original.infolist():
            payload = original.read(entry.filename)
            if entry.filename == "TITLE_SOURCE_MANIFEST.json":
                metadata = json.loads(payload)
                metadata["build"]["plugin_sha256"] = sha256_file(plugin)
                metadata["build"]["plugin_bytes"] = plugin.stat().st_size
                payload = (json.dumps(metadata, indent=2) + "\n").encode()
            replacement.writestr(entry, payload)
    temporary.replace(source)


def create_catalog(reference_root: Path, installation: Path, plugin: Path, *,
                   source_commit: str, shader_compiler: Path,
                   pipeline_helper: Path, pipeline_manifests: list[Path]) -> dict:
    if host_platform() != MACOS:
        raise ValueError("macOS catalog qualification must run on Apple Silicon macOS")
    if len(source_commit) != 40 or any(c not in "0123456789abcdef" for c in source_commit):
        raise ValueError("Platform source commit must be a full Git commit ID")
    reference = load_catalog(reference_root)
    runtime = installation / "TriAevum"
    native = installation / "forge/oot3d_game_module.dylib"
    if not 1 <= len(pipeline_manifests) <= 64:
        raise ValueError("Mac NRI pipeline preparation requires 1 to 64 manifests")
    for path, executable in ((runtime, True), (native, False), (plugin, False),
                             (shader_compiler, True), (pipeline_helper, True)):
        require_arm64_macho(path, executable=executable)
    relative_plugin = plugin.resolve().relative_to(installation.resolve()).as_posix()
    if not relative_plugin.startswith("titles/") or plugin.name != MACOS.title_module:
        raise ValueError("The Mac title must be staged under titles/ with its native filename")
    result = copy.deepcopy(reference)
    result.update(target=MACOS.target, runtime=artifact(runtime, "TriAevum"),
                  native_module=artifact(native, "forge/oot3d_game_module.dylib"))
    for item in result["titles"]:
        sources = [source for source in item.get("sources", []) if source["path"].endswith("-translated.zip")]
        if len(sources) != 1:
            raise ValueError("Every title requires one bound translated-source archive")
        archive_path = checked_file(reference_root, sources[0])
        with zipfile.ZipFile(archive_path) as archive:
            metadata = json.loads(archive.read("TITLE_SOURCE_MANIFEST.json"))
        build, files = metadata.get("build", {}), metadata.get("files")
        if (metadata.get("format") != "triaevum_translated_title_source_v1" or not isinstance(files, dict)
                or not files or build.get("code_sha256") != item["inputs"]["code"]["sha256"]
                or build.get("translator_identity_sha256") != item["translator_identity_sha256"]):
            raise ValueError("Reference title has no matching translated-source inventory")
        staged_source = installation / sources[0]["path"]
        rebind_translated_source(staged_source, plugin)
        item["sources"] = [
            artifact(staged_source, sources[0]["path"]),
            *[source for source in item["sources"] if source is not sources[0]],
        ]
        item.update(target=MACOS.target, plugin=artifact(plugin, relative_plugin),
                    platform_build={"source_commit": source_commit,
                                    "translated_source_sha256": sha256_file(archive_path),
                                    "build_target": "tools/triaevum_release/macos_title"})
    result, _ = bind_renderer_compiler(result, shader_compiler, ())
    portable_pack = installation / "forge/shader-corpus/portable.o3ps"
    result, _ = bind_shader_corpus(result, portable_pack, pipeline_manifests,
                                   pipeline_helper)
    for item in result["titles"]:
        for record in [item["renderer_shader_preparation"]["compiler"],
                       item["device_pipeline_preparation"]["helper"],
                       *item["device_pipeline_preparation"]["manifests"]]:
            checked_file(installation, record)
    query_product(runtime, plugin=plugin, renderer="nri")
    atomic_write_json(installation / CATALOG, result)
    return result
