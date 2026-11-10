"""Publisher-only promotion of a verified native title build into a release."""

from pathlib import Path
import json
import zipfile

try:
    from .release_platform import WINDOWS, for_target
    from .common import atomic_write_json, load_json_object, normalize_relative_path, sha256_file
    from .precompiled_titles import FORMAT, MODEL, CATALOG
    from .product_contract import query_product
except ImportError:
    from release_platform import WINDOWS, for_target
    from common import atomic_write_json, load_json_object, normalize_relative_path, sha256_file
    from precompiled_titles import FORMAT, MODEL, CATALOG
    from product_contract import query_product


def artifact(path: Path, destination: str) -> dict:
    if path.is_symlink() or not path.is_file():
        raise ValueError(f"Invalid release artifact: {path}")
    return {"path": destination, "sha256": sha256_file(path), "bytes": path.stat().st_size}


PROFILE_MEMBER = "title.profdata"


TRANSLATED_SOURCE_FORMAT = "triaevum_translated_title_source_v1"


def generated_identity(generated: dict) -> tuple | None:
    """(code, program, shards, functions) of the translated sources: the
    generator's own manifest, or the TITLE_SOURCE_MANIFEST.json of a published
    translated-source archive (unpacked, its files beside it) that a title
    module was rebuilt from on another toolchain (llvm-mingw, XBUILD)."""
    if generated.get("format") == "oot3d_whole_aot_cpp_v1":
        return (generated.get("code_sha256"), generated.get("program_sha256"),
                generated.get("shard_count"), len(generated.get("functions", [])))
    if generated.get("format") == TRANSLATED_SOURCE_FORMAT and isinstance(generated.get("build"), dict):
        origin = generated["build"]
        return (origin.get("code_sha256"), origin.get("program_sha256"),
                origin.get("shards"), origin.get("functions"))
    return None


def title_layout(*, runtime: Path, native_module: Path, plugin_manifest: Path,
                 generated_manifest: Path, build_source: Path, recipe: dict,
                 work: Path, profile_data: Path | None = None) -> list[dict]:
    build = load_json_object(plugin_manifest)
    generated = load_json_object(generated_manifest)
    # A profile-guided module is rebuilt from its translated sources and the
    # profile it was optimized with, so the profile ships beside the sources.
    profile_sha256 = build.get("profile_data_sha256")
    if profile_sha256 is not None and profile_data is None:
        raise ValueError("Profile-guided title build requires its profile data (--title-profile)")
    if profile_sha256 is None and profile_data is not None:
        raise ValueError("Profile data given for a title built without a profile")
    profile = None
    if profile_data is not None:
        profile = artifact(profile_data, PROFILE_MEMBER)
        if profile["sha256"] != profile_sha256:
            raise ValueError("Profile data differs from the one the title was built with")
    platform = for_target(build.get("target", WINDOWS.target))
    if (build.get("format") != "triaevum_generated_cpp_whole_aot_plugin_v2"
            or build.get("profile") != platform.profile
            or build.get("code_sha256") != recipe["inputs"]["code"]["sha256"]
            or generated_identity(generated) != (build["code_sha256"], build.get("program_sha256"),
                                                 build.get("shards"), build.get("functions"))):
        raise ValueError("Title build, translated sources and ROM revision do not correspond")
    name = normalize_relative_path(recipe["id"])
    if "/" in name:
        raise ValueError("Recipe ID must be a single path component")
    plugin = plugin_manifest.parent / platform.title_module
    record = artifact(plugin, f"titles/{name}/{platform.title_module}")
    if record["sha256"] != build.get("plugin_sha256") or record["bytes"] != build.get("plugin_bytes"):
        raise ValueError("Precompiled title differs from its build receipt")
    query_product(runtime, plugin=plugin)
    with zipfile.ZipFile(build_source) as source:
        build_commit = json.loads(source.read("SOURCE_ARCHIVE_MANIFEST.json"))["source_commit"]
    work.mkdir(parents=True, exist_ok=True)
    translated = work / f"{name}-translated.zip"
    files = generated.get("files")
    if not isinstance(files, dict) or not files:
        raise ValueError("Translated-source manifest has no files")
    with zipfile.ZipFile(translated, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for relative, expected in sorted(files.items()):
            relative = normalize_relative_path(relative)
            if "/" in relative or Path(relative).suffix not in (".h", ".cpp"):
                raise ValueError("Only declared translated C++ sources may be promoted")
            path = generated_manifest.parent / relative
            if path.is_symlink() or sha256_file(path) != expected:
                raise ValueError(f"Translated source changed since generation: {relative}")
            archive.write(path, relative)
        manifest = {"format": "triaevum_translated_title_source_v1", "recipe": name,
                    "build_source_commit": build_commit, "build": build, "files": files}
        if profile is not None:
            archive.write(profile_data, PROFILE_MEMBER)
            manifest["profile"] = profile
        archive.writestr("TITLE_SOURCE_MANIFEST.json", json.dumps(manifest, indent=2))
        archive.writestr("README.md", "# Translated title sources\n\n"
            "This archive contains game-derived translated logic, not game assets.\n"
            "Use the paired title-build source archive for the ABI, support library,\n"
            "plugin wrapper and build scripts, and the release source for the current runtime.\n"
            "See docs/TRIAEVUM_PRECOMPILED_RELEASE.md for developer rebuild instructions.\n"
            "Original game rights are not relicensed by the TriAevum project license.\n"
            + ("\n`title.profdata` is the LLVM instrumentation profile the module was\n"
               "optimized with (`-fprofile-instr-use`). It holds execution counters of the\n"
               "translated functions, not game data.\n" if profile is not None else ""))
    sources = [artifact(translated, f"source/titles/{name}-translated.zip"),
               artifact(build_source, f"source/titles/{name}-build.zip")]
    catalog = {"format": FORMAT, "install_model": MODEL,
               "target": platform.target,
               "runtime": artifact(runtime, platform.runtime),
               "native_module": artifact(native_module, platform.native_module),
               "titles": [{"recipe": name, "inputs": recipe["inputs"], "abi_version": 2,
                           "target": platform.target, "plugin": record,
                           "translator_identity_sha256": build["translator_identity_sha256"],
                           "build_source_commit": build_commit, "sources": sources}]}
    catalog_path = work / "precompiled-titles.json"
    atomic_write_json(catalog_path, catalog)
    return [
        {"source": str(plugin.resolve()), "path": record["path"], "role": "precompiled_title"},
        {"source": str(catalog_path.resolve()), "path": CATALOG, "role": "precompiled_catalog"},
        {"source": str(translated.resolve()), "path": sources[0]["path"], "role": "translated_title_source"},
        {"source": str(build_source.resolve()), "path": sources[1]["path"], "role": "title_build_source"},
    ]
