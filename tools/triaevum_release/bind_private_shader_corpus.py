"""Attach retained, trusted shader evidence to a PRIVATE Forge package.

No driver caches are imported. Forge compiles inventories or imports portable
SPIR-V packs and prepares pipelines
for the current adapter using its existing installation services. This is not a
public release builder: game-derived evidence remains private.
"""

import argparse
import copy
import hashlib
import json
import shutil
from pathlib import Path

from common import atomic_write_json, load_json_object
from precompiled_titles import CATALOG, checked_file
from shader_preparation import FORMAT as SEED_FORMAT
from device_pipeline_preparation import FORMAT as DEVICE_FORMAT


def bind(package: Path, evidence: Path, recipe: str) -> dict:
    package = package.resolve(strict=True)
    evidence = evidence.resolve(strict=True)
    catalog = load_json_object(package / CATALOG)
    matches = [title for title in catalog["titles"] if title["recipe"] == recipe]
    if len(matches) != 1:
        raise ValueError("Select exactly one existing title recipe")
    title = matches[0]
    renderer = title.get("renderer_shader_preparation")
    if renderer:
        for item in [renderer["compiler"], *renderer.get("dependencies", [])]:
            checked_file(package, item)
    record = load_json_object(evidence / "title-record.json")
    seed = copy.deepcopy(record["shader_preparation"])
    device = copy.deepcopy(record["device_pipeline_preparation"])
    if seed.get("format") != SEED_FORMAT or seed.get("mode") not in ("source_inventories", "portable_pack"):
        raise ValueError("Expected a retained inventory or portable-pack corpus")
    if device.get("format") != DEVICE_FORMAT or not device.get("manifests"):
        raise ValueError("Expected observed pipeline recipes")
    if seed["mode"] == "source_inventories" and not seed.get("inventories"):
        raise ValueError("Empty shader corpus")
    sources = evidence / "preparation-inputs"
    seed_files = ([seed["compiler"]] if "compiler" in seed else [])
    seed_files += seed["inventories"] if seed["mode"] == "source_inventories" else [seed["pack"]]
    records = [*seed_files, *seed.get("dependencies", []),
               device["helper"], *device.get("dependencies", []), *device["manifests"]]
    # Validate every input before touching the destination catalog. The evidence
    # contains executable tools, so callers must trust it like a developer build.
    files = [(item, checked_file(sources, item)) for item in records]
    identity = hashlib.sha256(json.dumps(record, sort_keys=True).encode()).hexdigest()
    prefix = "private-shader-corpus/" + identity
    for item, source in files:
        relative = prefix + "/" + item["path"]
        destination = package / relative
        for component in (destination, *destination.parents):
            if component == package:
                break
            if component.is_symlink() or (hasattr(component, "is_junction") and component.is_junction()):
                raise ValueError("Linked corpus destination is forbidden")
        destination.parent.mkdir(parents=True, exist_ok=True)
        if not destination.exists():
            shutil.copy2(source, destination)
        item["path"] = relative
        checked_file(package, item)
    title["shader_preparation"] = seed
    if renderer:
        # Reuse the compiler shipped alongside this runtime, not yesterday's
        # evidence compiler. Its identity participates in Forge invalidation.
        seed["compiler"] = copy.deepcopy(renderer["compiler"])
        seed["dependencies"] = copy.deepcopy(renderer.get("dependencies", []))
    title["device_pipeline_preparation"] = device
    receipt = {"format": "triaevum_private_shader_corpus_binding_v1",
               "recipe": recipe, "evidence_sha256": identity,
               "public_distribution": False, "full_game_coverage": False,
               "mode": seed["mode"], "inventories": len(seed.get("inventories", [])),
               "manifests": len(device["manifests"])}
    atomic_write_json(package / prefix / "binding.json", receipt)
    atomic_write_json(package / CATALOG, catalog)
    return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--private-package", type=Path, required=True)
    parser.add_argument("--trusted-evidence", type=Path, required=True)
    parser.add_argument("--recipe", required=True)
    args = parser.parse_args()
    print(json.dumps(bind(args.private_package, args.trusted_evidence, args.recipe), indent=2))


if __name__ == "__main__":
    main()
