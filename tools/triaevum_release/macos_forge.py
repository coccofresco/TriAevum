"""macOS ROM import and launch using the shared Forge verification pipeline."""
from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import tempfile
from pathlib import Path
import certifi

from tools.triaevum_release import ctr_rom, forge
from tools.triaevum_release.bundle_paths import distribution_path
from tools.triaevum_release.common import atomic_write_json
from tools.triaevum_release.forge_gui import match_extracted_recipe
from tools.triaevum_release.input_adapters import adapt_extracted_inputs
from tools.triaevum_release.product_contract import ensure_runtime_config
from tools.triaevum_release import game_language
from tools.triaevum_release.shader_preparation import prepare_shader_seed, prepare_renderer_shader_cache
from tools.triaevum_release.common import sha256_file
from tools.triaevum_release.topscreen_assets import prepare_topscreen_assets


def report(stage: str, message: str) -> None:
    print(f"{stage}: {message}", flush=True)


def install(rom: Path, root: Path, data: Path) -> Path:
    os.environ.setdefault("SSL_CERT_FILE", certifi.where())
    root, data = root.resolve(), data.expanduser().resolve()
    runtime = root / "TriAevum"
    plugin = root / "triaevum_title_aot.dylib"
    result = subprocess.run([str(runtime), "--verify-title-plugin", str(plugin)],
                            check=True, capture_output=True, text=True)
    product = json.loads(result.stdout)
    data.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".import-", dir=data) as temporary:
        report("extract", "Extracting your decrypted ROM")
        extracted = ctr_rom.extract_decrypted_rom(rom, Path(temporary) / "extracted")
        recipe = match_extracted_recipe(extracted, root / "recipes/oot3d.json")
        report("verify", f"Matched {recipe['id']}")
        extracted, _ = adapt_extracted_inputs(
            extracted, recipe, root=root, output=Path(temporary) / "normalized")
        languages = game_language.discover(runtime, extracted.romfs.path)
        extracted = ctr_rom.publish_extracted_inputs(extracted, data / "sources")
        cache = forge.HashCache(data / ".hash-cache.json")
        verified, _ = forge.verify_sources(
            recipe, code_path=extracted.code.path, exheader_path=extracted.exheader.path,
            romfs_path=extracted.romfs.path, cache=cache)
        prepared = forge.prepare_content(recipe, verified, output_root=data / "titles")
    texture_pack = prepare_topscreen_assets(
        root=root, data_root=data, recipe=recipe, romfs=extracted.romfs.path, report=report)
    settings = data / "settings"
    settings.mkdir(exist_ok=True)
    config = settings / "runtime.json"
    topscreen = settings / "topscreen.json"
    ensure_runtime_config(config, product, renderer="vulkan")
    # The runtime discovers this beside --config. Mac installations retain
    # their existing settings/ layout rather than desktop Forge's config/.
    language_path = settings / "game_language.json"
    if language_path.exists():
        previous = game_language.validate(json.loads(language_path.read_text()))["selected"]
        if previous in {entry["code"] for entry in languages["available"]}:
            languages["selected"] = previous
    atomic_write_json(language_path, languages)
    pack = root / "forge/shader-corpus/portable.o3ps"
    compiler = root / "oot3d_native_pica_aot_compiler"
    def artifact(path: Path) -> dict:
        return {"path": path.relative_to(root).as_posix(), "bytes": path.stat().st_size,
                "sha256": sha256_file(path)}
    shader_pack = prepare_shader_seed(root=root, data_root=data, title={
        "recipe": recipe["id"],
        "shader_preparation": {"format": "triaevum_shader_preparation_v1",
            "mode": "portable_pack", "descriptor_schema_version": 3, "pack": artifact(pack)}}, report=report)
    renderer_cache = data / "cache/renderer"
    prepare_renderer_shader_cache(root=root, data_root=data, cache_directory=renderer_cache,
        title={"renderer_shader_preparation": {"format": "triaevum_renderer_shader_compiler_v1",
            "compiler": artifact(compiler)}}, report=report)
    if not topscreen.exists():
        shutil.copyfile(distribution_path("config/topscreen_ui.example.json"), topscreen)
    savedata = data / "savedata"
    savedata.mkdir(exist_ok=True)
    arguments = [
        "--title-plugin", str(plugin),
        "--a32-process-manifest", prepared["process_manifest"],
        "--resource-root", str(root / "resources"),
        "--renderer", "vulkan", "--ui-profile", "topscreen",
        "--config", str(config), "--topscreen-config", str(topscreen),
        "--gameplay-timing", "native30_interpolated", "--presentation-rate", "60",
        "--save-data", str(savedata), "--output", str(data / "runtime-state.json"),
        "--width", "1280", "--height", "720",
        "--pica-aot-shader-pack", str(shader_pack),
        "--renderer-cache-directory", str(renderer_cache),
    ]
    if texture_pack:
        arguments += ["--topscreen-texture-overrides", str(texture_pack)]
    profile = data / "TriAevum.launch.json"
    atomic_write_json(profile, {"format": "oot3d_native_game_launch_profile_v1",
                                "arguments": arguments})
    report("ready", str(profile))
    return profile


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rom", type=Path)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--data", type=Path,
                        default=Path.home() / "Library/Application Support/TriAevum")
    args = parser.parse_args()
    install(args.rom, args.root, args.data)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
