"""Per-title release facts for prepare_release: each title's Forge is its own
release (owner decision 2026-11-08). OoT3D's values reproduce the existing
release exactly; MM3D's Forge is built with the title-mm3d feature, carries its
own recipe file and bundles Project Restoration's members as package
resources (they are product content, never a user input)."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MM3D_FINGERPRINTS = ROOT / "titles/mm3d/dekui/setup/setup-fingerprints.json"
# forge-assets mm3d_ui::RESTORATION_RESOURCES: the members' package folder,
# without the `romfs` component the release policy forbids.
RESTORATION_RESOURCES = "resources/mm3d/project-restoration"
RESTORATION_ROLES = {".gar": "restoration_mm3d_asset"}

TITLES = {
    "oot3d": {
        "recipes": ROOT / "tools/triaevum_release/supported_revisions.json",
        "recipe_path": "recipes/oot3d.json",
        "default_recipe": "oot3d-eur-project-baseline-16a6b0aa",
        "forge_features": (),
    },
    "mm3d": {
        "recipes": ROOT / "titles/mm3d/forge/supported_revisions.json",
        "recipe_path": "recipes/mm3d.json",
        "default_recipe": "mm3d-eur-local-6eec27b1",
        "forge_features": ("forge-desktop/title-mm3d", "forge-cli/title-mm3d", "forge-update-helper/title-mm3d"),
    },
}


def title_release(title: str) -> dict:
    if title not in TITLES:
        raise ValueError(f"Unknown release title {title!r} ({', '.join(TITLES)})")
    return TITLES[title]


def retarget_recipe(layout_files: list[dict], title: str) -> None:
    """Point the layout's single revision recipe at the title's own file."""
    facts = title_release(title)
    recipes = [item for item in layout_files if item.get("role") == "revision_recipe"]
    if len(recipes) != 1:
        raise ValueError("The release layout must hold exactly one revision recipe")
    recipes[0]["source"] = str(facts["recipes"])
    recipes[0]["path"] = facts["recipe_path"]


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def restoration_members(source: Path, fingerprints: Path = MM3D_FINGERPRINTS) -> list[dict]:
    """Layout items for every Restoration romfs member MM3D's preparation
    reads, each verified against its pinned fingerprint. `source` is the
    member tree in its own layout (`romfs/...`)."""
    manifest = json.loads(fingerprints.read_text(encoding="utf-8"))
    items = []
    for entry in manifest["files"]:
        output = entry["output"]
        if entry.get("input_root") != "restoration" or not output.startswith("romfs/") or ".mm3d-" in output:
            continue
        relative = entry["input"].removeprefix("romfs/")
        if relative == entry["input"] or not relative or any(
                part in ("", ".", "..") for part in relative.split("/")):
            raise ValueError(f"Invalid Restoration member {entry['input']}")
        member = source / "romfs" / relative
        if not member.is_file() or member.is_symlink():
            raise ValueError(f"Project Restoration member is missing: {member}")
        if _sha256(member) != entry["input_sha256"]:
            raise ValueError(f"Project Restoration member {relative} does not match its fingerprint")
        path = f"{RESTORATION_RESOURCES}/{relative}"
        if any(item["path"] == path for item in items):
            continue  # one member can feed several outputs
        items.append({"source": str(member.resolve()), "path": path,
                      "role": RESTORATION_ROLES.get(member.suffix, "title_neutral_resource")})
    if not items:
        raise ValueError("The MM3D fingerprints name no Project Restoration member")
    return items
