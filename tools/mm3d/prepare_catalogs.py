"""Prepare local MM3D renderer catalogs from the owner's extracted EUR assets.

Developer tool, not a replacement for the precompiled Forge installation flow.
The water_catalog member of recipe-fragment.json is consumed by both Forge
implementations' existing water preparation step. Scene emitters still require
this offline title generator until native Forge gains the corresponding reader.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/triaevum_release"))
from common import atomic_write_bytes, atomic_write_json
from water_textures import _romfs_image_files, prepare_water_textures


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def scene_generator():
    spec = importlib.util.spec_from_file_location("mm3d_scene_emitters", ROOT / "titles/mm3d/tools/scene_emitters.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def prepare(code: Path, romfs: Path, data_root: Path) -> dict:
    if not code.is_file() or not romfs.is_file():
        raise ValueError("code and RomFS must be extracted files (not a ROM or a directory)")
    profile = json.loads((ROOT / "titles/mm3d/catalogs/water.json").read_text(encoding="utf-8"))
    recipe = {"inputs": {"romfs": {"sha256": digest(romfs)}}, "water_catalog": profile}
    water = prepare_water_textures(data_root=data_root, recipe=recipe, romfs=romfs)
    # Bind the full source identity and every generator dependency; never reuse
    # a catalog merely because its file exists or the summary count agrees.
    inputs = {"code_sha256": digest(code), "romfs_sha256": recipe["inputs"]["romfs"]["sha256"],
              "generator_sha256": {str(path.relative_to(ROOT)).replace("\\", "/"): digest(path) for path in (
                  Path(__file__), ROOT / "titles/mm3d/tools/scene_emitters.py",
                  ROOT / "tools/triaevum_release/scene_lights.py", ROOT / "tools/triaevum_release/water_textures.py")}}
    key = hashlib.sha256(json.dumps(inputs, sort_keys=True).encode()).hexdigest()
    directory = data_root / "cache" / "scene-lights" / "mm3d" / key
    catalog, receipt = directory / "scene_lights.json", directory / "receipt.json"
    reuse = False
    if catalog.is_file() and receipt.is_file():
        try:
            prior = json.loads(receipt.read_text(encoding="utf-8"))
            reuse = prior.get("inputs") == inputs and prior.get("catalog_sha256") == digest(catalog)
        except (ValueError, OSError):
            pass
    if not reuse:
        value = scene_generator().generate(code.read_bytes(), _romfs_image_files(romfs, ("scenes",)))
        if value["rejected"] or not value["rooms"]:
            raise ValueError(f"room catalog incomplete: {len(value['rooms'])} rooms, {value['rejected']}")
        payload = (json.dumps(value, indent=2) + "\n").encode("utf-8")
        directory.mkdir(parents=True, exist_ok=True)
        atomic_write_bytes(catalog, payload)
        atomic_write_json(receipt, {"inputs": inputs, "catalog_sha256": digest(catalog),
                                   "rooms": len(value["rooms"]),
                                   "emitters": sum(len(r["emitters"]) for r in value["rooms"])})
    output = data_root / "catalogs" / "mm3d"
    output.mkdir(parents=True, exist_ok=True)
    atomic_write_json(output / "recipe-fragment.json", recipe)
    result = {"schema": "triaevum.local-catalogs", "version": 1, "title": "mm3d",
              "inputs": {"code_sha256": inputs["code_sha256"], "romfs_sha256": inputs["romfs_sha256"]},
              "water": {"path": str(water.resolve()), "sha256": digest(water)},
              "scene_lights": {"path": str(catalog.resolve()), "sha256": digest(catalog)},
              "runtime_arguments": ["--water-texture-catalog", str(water.resolve()),
                                    "--scene-light-catalog", str(catalog.resolve())],
              "not_generated": ["static-shadow-actors", "PBR-material-pack", "shader-corpus", "interior-sun"]}
    atomic_write_json(output / "manifest.json", result)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--code-bin", type=Path, required=True)
    parser.add_argument("--romfs", type=Path, required=True)
    parser.add_argument("--data-root", type=Path, required=True)
    args = parser.parse_args()
    try:
        result = prepare(args.code_bin, args.romfs, args.data_root)
    except (ValueError, OSError) as error:
        parser.exit(1, f"catalog preparation failed: {error}\n")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
