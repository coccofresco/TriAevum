"""Revision-bound precompiled title installation, with no compiler fallback."""

from __future__ import annotations

from pathlib import Path
from typing import Any, Callable

try:
    from .common import load_json_object, normalize_relative_path, sha256_file
    from .activation_transaction import activation_transaction
    from .release_platform import catalog_platform, host_platform
except ImportError:
    from common import load_json_object, normalize_relative_path, sha256_file
    from activation_transaction import activation_transaction
    from release_platform import catalog_platform, host_platform


FORMAT = "triaevum_precompiled_titles_v1"
MODEL = "precompiled_title_rom_import_v1"
CATALOG = "recipes/precompiled-titles.json"


def checked_file(root: Path, record: dict) -> Path:
    relative = normalize_relative_path(str(record.get("path", "")))
    root = root.resolve()
    path = root / relative
    for component in (path, *path.parents):
        if component == root:
            break
        if component.is_symlink() or (hasattr(component, "is_junction") and component.is_junction()):
            raise ValueError(f"Linked precompiled artifact is forbidden: {relative}")
    if not path.resolve().is_relative_to(root) or not path.is_file():
        raise ValueError(f"Precompiled artifact is missing: {relative}")
    if path.stat().st_size != record.get("bytes") or sha256_file(path) != record.get("sha256"):
        raise ValueError(f"Precompiled artifact failed integrity validation: {relative}")
    return path


def load_catalog(root: Path) -> dict:
    payload = load_json_object(root / CATALOG)
    if payload.get("format") != FORMAT or payload.get("install_model") != MODEL:
        raise ValueError("Unsupported precompiled-title catalog")
    platform = catalog_platform(payload)
    for field, expected in (("runtime", platform.runtime), ("native_module", platform.native_module)):
        if not isinstance(payload.get(field), dict) or payload[field].get("path") != expected:
            raise ValueError(f"Invalid catalog {field} binding")
    titles = payload.get("titles")
    if not isinstance(titles, list) or not titles:
        raise ValueError("This release contains no precompiled titles; obtain a complete package")
    ids = [item.get("recipe") for item in titles if isinstance(item, dict)]
    if len(ids) != len(titles) or not all(isinstance(item, str) and item for item in ids) or len(set(ids)) != len(ids):
        raise ValueError("Invalid or duplicate precompiled-title revisions")
    return payload


def validate_title(root: Path, recipe: dict, *, catalog: dict | None = None) -> dict:
    """Validate catalog content without executing binaries or requiring its host."""
    catalog = catalog or load_catalog(root)
    platform = catalog_platform(catalog)
    matches = [item for item in catalog["titles"] if item["recipe"] == recipe["id"]]
    if len(matches) != 1:
        raise ValueError("No precompiled module for this ROM revision; obtain a compatible release")
    item = matches[0]
    if (item.get("inputs") != recipe.get("inputs") or item.get("abi_version") != 2
            or item.get("target") != platform.target):
        raise ValueError("Precompiled title revision/ABI does not match the ROM recipe")
    if item.get("input_adapter") != recipe.get("input_adapter"):
        raise ValueError("Precompiled title input adapter differs from the revision recipe")
    if item.get('data_compatibility') != recipe.get('data_compatibility'):
        raise ValueError('Precompiled title content family differs from the recipe')
    if recipe.get('data_compatibility') is not None:
        try:
            from .data_compatibility import expected
        except ImportError:
            from data_compatibility import expected
        expected(recipe, 'execution')
    if recipe.get("input_adapter") is not None:
        try:
            from .input_adapters import validate_adapter
        except ImportError:
            from input_adapters import validate_adapter
        validate_adapter(root, recipe)
    identity = item.get("translator_identity_sha256", "")
    if not isinstance(identity, str) or len(identity) != 64 or any(c not in "0123456789abcdef" for c in identity):
        raise ValueError("Precompiled title has no valid translator identity")
    checked_file(root, catalog["runtime"])
    checked_file(root, catalog["native_module"])
    checked_file(root, item["plugin"])
    return item


def select_title(root: Path, recipe: dict, *, catalog: dict | None = None) -> dict:
    catalog = catalog or load_catalog(root)
    platform = catalog_platform(catalog)
    if platform != host_platform():
        raise ValueError(f"This package targets {platform.target}, not this host")
    return validate_title(root, recipe, catalog=catalog)


def install_precompiled_title(prepared_directory: Path, *, root: Path, recipe: dict,
                              data_root: Path, runtime_plugin: Path,
                              launch_profile: Path, active_title_state: Path,
                              report: Callable[[str, str], None] = lambda *_: None) -> dict[str, Any]:
    try:
        from . import forge
        from .topscreen_assets import prepare_topscreen_assets
    except ImportError:
        import forge
        from topscreen_assets import prepare_topscreen_assets
    catalog = load_catalog(root)
    item = select_title(root, recipe, catalog=catalog)
    prepared = forge.load_prepared_content(prepared_directory, required_inputs=("code", "exheader", "romfs"))
    if prepared.index.get("recipe") != item["recipe"]:
        raise ValueError("Prepared title differs from the precompiled revision")
    mismatch = False
    for kind in ("code", "exheader", "romfs"):
        actual = prepared.inputs[kind]
        expected = item["inputs"][kind]
        if actual.sha256 != expected["sha256"] or actual.bytes != expected["bytes"]:
            mismatch = True
    if mismatch:
        try:
            from .data_compatibility import verify
        except ImportError:
            from data_compatibility import verify
        verify(recipe, {kind: value.path for kind, value in prepared.inputs.items()}, phase='execution')
    plugin = checked_file(root, item["plugin"])
    native_module = checked_file(root, catalog["native_module"])
    texture_pack = prepare_topscreen_assets(
        root=root, data_root=data_root, recipe=recipe,
        romfs=prepared.inputs["romfs"].path, report=report)
    with activation_transaction(root.resolve(), [
        runtime_plugin, launch_profile, active_title_state,
        prepared.directory / "forge-state.json",
        data_root / "config" / "TriAevum.json", data_root / "config" / "topscreen_ui.json",
    ]):
        packaged = forge.package_private_module(
            prepared.directory, native_module, title_aot_image=plugin,
            target_triple=item["target"], translator_identity=item["translator_identity_sha256"])
        runtime = forge.publish_private_runtime(
            prepared.directory, plugin=plugin, runtime_plugin=runtime_plugin,
            launch_profile=launch_profile, data_root=data_root,
            topscreen_texture_pack=texture_pack)
        activation = forge.activate_prepared_title(prepared.directory, active_title_state=active_title_state)
    return {"status": "ready", "install_model": MODEL, "objects_compiled": 0,
            "package": packaged, "runtime": runtime, "active_title": activation["active_title"]}
