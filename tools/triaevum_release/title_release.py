"""Per-title release facts for prepare_release: each title's Forge is its own
release (owner decision 2026-11-08). OoT3D's values reproduce the existing
release exactly; MM3D's Forge is built with the title-mm3d feature, carries its
own recipe file and bundles Project Restoration's layout recipe, patches and
original members as package resources (product content, never a user input;
the game members are rebuilt from the user's dump at install time)."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
MM3D_FINGERPRINTS = ROOT / "titles/mm3d/dekui/setup/setup-fingerprints.json"
# forge-assets mm3d_ui::RESTORATION_RESOURCES: the members' package folder,
# without the `romfs` component the release policy forbids.
RESTORATION_RESOURCES = "resources/mm3d/project-restoration"
# The layout recipe, its patches and the Restoration-original members
# (tools/mm3d/restoration_layout.py, C-DISTRO-1): no game bytes ship.
RESTORATION_RECIPE = ROOT / "titles/mm3d/restoration/package"
RESTORATION_RECIPE_NAME = "layout.recipe.json"
sys.path.insert(0, str(ROOT / "tools/mm3d"))
import restoration_layout  # noqa: E402

TITLES = {
    "oot3d": {
        "recipes": ROOT / "tools/triaevum_release/supported_revisions.json",
        "recipe_path": "recipes/oot3d.json",
        "default_recipe": "oot3d-eur-project-baseline-16a6b0aa",
        "forge_features": (),
        # OoT3D's reviewed extra ROM inputs (USA canonicalised to EUR, catalogue EUR).
        "input_coverage": ROOT / "tools/triaevum_release/input_coverage",
    },
    "mm3d": {
        "recipes": ROOT / "titles/mm3d/forge/supported_revisions.json",
        "recipe_path": "recipes/mm3d.json",
        "default_recipe": "mm3d-eur-local-6eec27b1",
        "forge_features": ("forge-desktop/title-mm3d", "forge-cli/title-mm3d", "forge-update-helper/title-mm3d"),
        # The EUR CTR-P-AJRP dump is MM3D's only input: no qualified extra inputs.
        "input_coverage": None,
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


def title_composition_path(title: str, root: Path = ROOT) -> Path:
    """The title's package composition (titles/<title>/forge/package-composition.json).
    A title without one (OoT3D) ships the shared layout unchanged."""
    return root / "titles" / title / "forge" / "package-composition.json"


def load_title_composition(title: str, root: Path = ROOT) -> dict | None:
    path = title_composition_path(title, root)
    if not path.is_file():
        return None
    composition = json.loads(path.read_text(encoding="utf-8"))
    if composition.get("format") != "triaevum_title_package_composition_v1" or composition.get("title") != title:
        raise ValueError(f"{path}: not a {title} package composition")
    return composition


def compose_title_layout(layout_files: list[dict], title: str, root: Path = ROOT) -> None:
    """Apply the title's composition to the shared layout, in place: drop the
    other title's documents, resources and licences, replace the README and
    add the title's own documents and licences. Every dropped or replaced path
    must be in the layout, so a renamed shared file cannot pass silently."""
    title_release(title)
    composition = load_title_composition(title, root)
    if composition is None:
        return
    paths = {item["path"] for item in layout_files}
    missing = sorted((set(composition["drop"]) | set(composition["replace"])) - paths)
    if missing:
        raise ValueError(f"{title} composition names paths the layout lacks: {', '.join(missing)}")
    layout_files[:] = [item for item in layout_files if item["path"] not in set(composition["drop"])]
    for item in layout_files:
        if item["path"] in composition["replace"]:
            item["source"] = composition["replace"][item["path"]]
    for extra in composition["add"]:
        if extra["path"] in {item["path"] for item in layout_files}:
            raise ValueError(f"{title} composition adds a path the layout already has: {extra['path']}")
        if not (root / extra["source"]).is_file():
            raise ValueError(f"{title} composition source is missing: {extra['source']}")
        layout_files.append({"source": extra["source"], "path": extra["path"], "role": extra["role"]})


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def restoration_members(source: Path, staging: Path, fingerprints: Path = MM3D_FINGERPRINTS,
                        recipe_dir: Path = RESTORATION_RECIPE) -> list[dict]:
    """Layout items of the MM3D package's Restoration folder (C-DISTRO-1): the
    layout recipe and its BPS patches from the tree, and the
    Restoration-original members cut from the release files in `source`
    (`romfs/layout/...`, each checked against its pinned fingerprint) into
    `staging`. No .gar/.mfpj is packaged: the Forge rebuilds the three
    layout files from the user's dump at install time."""
    recipe = json.loads((recipe_dir / RESTORATION_RECIPE_NAME).read_text(encoding="utf-8"))
    expected = restoration_layout.fingerprinted_inputs(fingerprints)
    if recipe.get("format") != restoration_layout.FORMAT or \
            {o["path"]: o["sha256"] for o in recipe["outputs"]} != expected:
        raise ValueError("The Restoration layout recipe does not describe the fingerprinted inputs")
    release = {}
    for relative, digest in expected.items():
        member = source / "romfs" / relative
        if not member.is_file() or member.is_symlink():
            raise ValueError(f"Project Restoration member is missing: {member}")
        if _sha256(member) != digest:
            raise ValueError(f"Project Restoration member {relative} does not match its fingerprint")
        release[relative] = member.read_bytes()
    items = [{"source": str((recipe_dir / RESTORATION_RECIPE_NAME).resolve()),
              "path": f"{RESTORATION_RESOURCES}/{RESTORATION_RECIPE_NAME}", "role": "restoration_layout_recipe"}]
    shipped = restoration_layout.ship_members(recipe, release)
    for relative, digest in sorted(recipe["files"].items()):
        if any(part in ("", ".", "..") for part in relative.split("/")) or not relative.startswith("layout/"):
            raise ValueError(f"Invalid Restoration layout resource {relative}")
        if relative in shipped:
            path = staging / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(shipped[relative])
            role = "restoration_original_member"
        else:
            path = recipe_dir / relative
            role = "restoration_patch"
        if not path.is_file() or _sha256(path) != digest:
            raise ValueError(f"Restoration layout resource {relative} does not match the recipe")
        items.append({"source": str(path.resolve()), "path": f"{RESTORATION_RESOURCES}/{relative}", "role": role})
    for item in items:
        if Path(item["path"]).suffix in (".gar", ".mfpj"):
            raise ValueError(f"A layout archive would be packaged: {item['path']}")
    return items
