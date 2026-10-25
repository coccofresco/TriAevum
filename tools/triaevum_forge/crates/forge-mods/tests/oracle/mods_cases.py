# Copyright TriAevum contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Runs the Python Forge's UI texture pack and RomFS mod code on fixtures that
the Rust test wrote, so the Rust port can compare the resulting trees.

usage: mods_cases.py spec.json   (spec names the fixtures, the output root and
the substitute catalogue contracts, as test_ui_texture_pack.py and
test_romfs_mods.py do).
"""

import dataclasses
import hashlib
import json
import sys
from pathlib import Path

RELEASE = Path(__file__).resolve().parents[5] / "triaevum_release"
sys.path.insert(0, str(RELEASE))
sys.path.insert(0, str(RELEASE.parents[1]))

import romfs_mods  # noqa: E402
import ui_texture_pack  # noqa: E402
from romfs_mods import ArchiveContract  # noqa: E402


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def run(spec):
    fixtures = Path(spec["fixtures"])
    out = Path(spec["out"])
    out.mkdir(parents=True, exist_ok=True)
    results = {}
    errors = {}

    pack = dataclasses.replace(
        ui_texture_pack.VANILLA_UI_HUD,
        members={name: (size, digest) for name, size, digest in spec["ui_members"]})
    ui_zip = fixtures / "ui.zip"
    ui_contract = ArchiveContract(pack.archive_kind, ui_zip.name, ui_zip.stat().st_size, sha(ui_zip))
    ui_recipe = {"optional_inputs": {pack.archive_kind: {
        "name": ui_contract.name, "bytes": ui_contract.bytes, "sha256": ui_contract.sha256}}}

    mod = dataclasses.replace(
        romfs_mods.VANILLA_PLUS_LINK, mod_id="fixture", archive_kind="fixture_archive", version="0.1",
        downloads=("https://example.invalid/primary",),
        members={name: (size, digest) for name, size, digest in spec["mod_members"]})
    romfs_mods.CATALOG["fixture"] = mod
    mod_zip = fixtures / "mod.zip"
    mod_contract = ArchiveContract(mod.archive_kind, mod_zip.name, mod_zip.stat().st_size, sha(mod_zip))
    mod_recipe = {"optional_inputs": {mod.archive_kind: {
        "name": mod_contract.name, "bytes": mod_contract.bytes, "sha256": mod_contract.sha256}}}
    romfs = fixtures / "romfs.bin"
    prefix, view = spec["prefix"], spec["view_size"]
    romfs_sha = sha(romfs)

    def guarded(name, action):
        try:
            return action()
        except Exception as exc:  # The message is the contract.
            errors[name] = str(exc)
            return None

    def relative(path, base):
        return None if path is None else Path(path).relative_to(base).as_posix()

    # --- UI pack --------------------------------------------------------------
    base = out / "ui_archive"
    directory = guarded("ui_archive", lambda: ui_texture_pack.extract_archive(
        ui_zip, base, ui_contract, pack))
    results["ui_archive"] = {"value": relative(directory, base),
                             "prepared": relative(ui_texture_pack.prepared_pack(base, pack), base),
                             "atlas": relative(ui_texture_pack.ui_atlas_hd(directory), base)}

    for index, level in enumerate(spec["ui_levels"]):
        base = out / f"ui_folder_{index}"
        directory = guarded(f"ui_folder_{index}",
                            lambda: ui_texture_pack.import_extracted(fixtures / level, base, pack))
        results[f"ui_folder_{index}"] = {"value": relative(directory, base)}

    base = out / "ui_prepare"
    (base / "downloads").mkdir(parents=True)
    (base / "downloads" / ui_zip.name).write_bytes(ui_zip.read_bytes())
    package = out / "ui_prepare_package"
    package.mkdir()
    reports = []
    directory = ui_texture_pack.prepare_ui_texture_pack(
        root=package, data_root=base, recipe=ui_recipe, report=lambda stage, text: reports.append([stage, text]),
        pack=pack)
    results["ui_prepare"] = {"value": relative(directory, base), "reports": reports,
                             "status": ui_texture_pack.pack_status(package, base, ui_contract, pack)}
    results["ui_prepare_none"] = {"value": ui_texture_pack.prepare_ui_texture_pack(
        root=package, data_root=out / "ui_prepare_none", recipe={}, pack=pack) is None,
        "status": ui_texture_pack.pack_status(package, out / "ui_prepare_none", ui_contract, pack)}

    # Errors.
    wrong = dataclasses.replace(ui_contract, sha256="0" * 64)
    guarded("ui_wrong_archive", lambda: ui_texture_pack.extract_archive(
        ui_zip, out / "ui_err1", wrong, pack))
    atlas = ui_texture_pack.UI_ATLAS_HD_TEXTURE
    changed = dataclasses.replace(pack, members={**pack.members, atlas: (3, "0" * 64)})
    guarded("ui_wrong_member", lambda: ui_texture_pack.extract_archive(
        ui_zip, out / "ui_err2", ui_contract, changed))
    extra = dataclasses.replace(pack, members={**pack.members, "UI/missing.png": (1, "0" * 64)})
    guarded("ui_missing_member", lambda: ui_texture_pack.extract_archive(
        ui_zip, out / "ui_err3", ui_contract, extra))
    wrong_hash = dataclasses.replace(pack, members={**pack.members, atlas: (pack.members[atlas][0], "0" * 64)})
    guarded("ui_wrong_hash", lambda: ui_texture_pack.extract_archive(
        ui_zip, out / "ui_err4", ui_contract, wrong_hash))
    guarded("ui_folder_missing", lambda: ui_texture_pack.import_extracted(fixtures, out / "ui_err5", pack))
    guarded("ui_folder_differs", lambda: ui_texture_pack.import_extracted(
        fixtures / "ui_folder_bad", out / "ui_err6", pack))

    # --- RomFS mod ------------------------------------------------------------
    base = out / "mod_folder"
    package = out / "mod_package"
    package.mkdir()
    reports = []
    calls = []

    def view_of():
        calls.append(1)
        return prefix, view

    arguments = dict(root=package, data_root=base, recipe=mod_recipe, romfs=romfs, romfs_sha256=romfs_sha,
                     service_view=view_of, report=lambda stage, text: reports.append([stage, text]))
    first = romfs_mods.prepare_romfs_mods(**arguments)  # Nothing enabled.
    results["mod_inert"] = {"value": first is None, "calls": len(calls), "reports": list(reports)}
    results["mod_set"] = {"enabled": list(romfs_mods.set_mod_enabled(base, "fixture", True))}
    results["mod_set_twice"] = {"enabled": list(romfs_mods.set_mod_enabled(base, "fixture", True))}
    reports.clear()
    second = romfs_mods.prepare_romfs_mods(**arguments)  # Enabled but not downloaded.
    results["mod_missing"] = {"value": second is None, "reports": list(reports)}
    members = romfs_mods.import_extracted(fixtures / "mod_folder", base, mod)
    results["mod_import"] = {"members": sorted(members), "status": romfs_mods.mod_status(
        package, base, mod, mod_contract)}
    reports.clear()
    overlay = romfs_mods.prepare_romfs_mods(**arguments)
    results["mod_prepare"] = {"value": relative(overlay, base), "reports": list(reports)}
    stamp = overlay.stat().st_mtime_ns
    reports.clear()
    again = romfs_mods.prepare_romfs_mods(**arguments)
    results["mod_reuse"] = {"same": again == overlay, "untouched": overlay.stat().st_mtime_ns == stamp,
                            "reports": list(reports)}

    # The archive route through libarchive (a ZIP stands in for the RAR).
    tool = romfs_mods.find_bsdtar()
    results["bsdtar"] = {"available": tool is not None}
    if tool is not None:
        base = out / "mod_archive"
        (base / "downloads").mkdir(parents=True)
        (base / "downloads" / mod_zip.name).write_bytes(mod_zip.read_bytes())
        romfs_mods.set_mod_enabled(base, "fixture", True)
        reports = []
        overlay = romfs_mods.prepare_romfs_mods(
            root=package, data_root=base, recipe=mod_recipe, romfs=romfs, romfs_sha256=romfs_sha,
            service_view=view_of, report=lambda stage, text: reports.append([stage, text]))
        results["mod_archive"] = {"value": relative(overlay, base), "reports": reports}
        wrong = dataclasses.replace(mod_contract, sha256="0" * 64)
        guarded("mod_wrong_archive", lambda: romfs_mods.extract_archive(
            mod_zip, out / "mod_err1", mod, wrong, bsdtar=tool))

    # Overlay variants built directly.
    sources = fixtures / "mod_folder"
    for name, paths in spec["overlay_variants"].items():
        replacements = {path: sources / path for path in paths}
        target = out / "overlays" / f"{name}.o3ro"
        summary = guarded(f"overlay_{name}", lambda: romfs_mods.build_romfs_overlay(
            romfs, service_offset=prefix, service_size=view, replacements=replacements, output=target))
        results[f"overlay_{name}"] = {"summary": summary}
    guarded("overlay_missing_path", lambda: romfs_mods.build_romfs_overlay(
        romfs, service_offset=prefix, service_size=view, replacements={"actor/missing.zar": sources / "actor/a.zar"},
        output=out / "overlays" / "missing.o3ro"))
    guarded("overlay_empty", lambda: romfs_mods.build_romfs_overlay(
        romfs, service_offset=prefix, service_size=view, replacements={}, output=out / "overlays" / "e.o3ro"))
    guarded("overlay_outside", lambda: romfs_mods.build_romfs_overlay(
        romfs, service_offset=prefix, service_size=10**9, replacements={"actor/a.zar": sources / "actor/a.zar"},
        output=out / "overlays" / "o.o3ro"))
    guarded("overlay_not_romfs", lambda: romfs_mods.build_romfs_overlay(
        fixtures / "mod.zip", service_offset=0, service_size=mod_zip.stat().st_size,
        replacements={"actor/a.zar": sources / "actor/a.zar"}, output=out / "overlays" / "n.o3ro"))
    guarded("overlay_zero_payload", lambda: romfs_mods.build_romfs_overlay(
        romfs, service_offset=prefix, service_size=view, replacements={"actor/a.zar": fixtures / "empty.bin"},
        output=out / "overlays" / "z.o3ro"))

    # Settings and archive-contract errors.
    base = out / "settings"
    guarded("set_unknown", lambda: romfs_mods.set_mod_enabled(base, "unknown", True))
    (base / "config").mkdir(parents=True)
    (base / "config" / "romfs_mods.json").write_text('{"enabled": "yes"}', encoding="utf-8")
    guarded("settings_malformed", lambda: romfs_mods.enabled_mods(base))
    (base / "config" / "romfs_mods.json").write_text('{"format":"triaevum_romfs_mods_v1","enabled":["x","fixture"]}',
                                                   encoding="utf-8")
    results["settings_filtered"] = {"enabled": list(romfs_mods.enabled_mods(base))}
    (base / "config" / "romfs_mods.json").write_text("{", encoding="utf-8")
    guarded("settings_not_json", lambda: romfs_mods.enabled_mods(base))
    disagree = {"recipes": [mod_recipe, {"optional_inputs": {mod.archive_kind: {
        "name": "b.rar", "bytes": 1, "sha256": "AB"}}}]}
    guarded("recipes_disagree", lambda: romfs_mods.archive_contract(disagree, mod))
    results["contract_lower"] = {"value": dataclasses.astuple(romfs_mods.archive_contract(
        {"optional_inputs": {mod.archive_kind: {"name": "x", "bytes": 7, "sha256": "ABCDEF"}}}, mod))}

    # import_archive.
    base = out / "import_archive"
    picked = out / "picked.bin"
    picked.write_bytes(b"original archive bytes")
    contract = ArchiveContract(mod.archive_kind, "fixture.rar", 22, hashlib.sha256(b"original archive bytes").hexdigest())
    guarded("import_archive_missing", lambda: romfs_mods.import_archive(out / "nope.bin", base, mod, contract))
    picked.write_bytes(b"original archive byteX")
    guarded("import_archive_wrong", lambda: romfs_mods.import_archive(picked, base, mod, contract))
    picked.write_bytes(b"original archive bytes")
    stored = romfs_mods.import_archive(picked, base, mod, contract)
    results["import_archive"] = {"value": relative(stored, base), "bytes": stored.read_bytes().hex()}

    (out / "results.json").write_text(json.dumps({"results": results, "errors": errors}, sort_keys=True, indent=1),
                                      encoding="utf-8")


if __name__ == "__main__":
    run(json.loads(Path(sys.argv[1]).read_text(encoding="utf-8")))
