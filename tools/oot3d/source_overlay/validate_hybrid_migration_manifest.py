#!/usr/bin/env python3
"""Validate the source-owner inventory used by the hybrid whole-AOT lane."""

from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
MANIFEST_PATH = Path(__file__).with_name("hybrid_migration_manifest.json")


def fail(message: str) -> None:
    raise RuntimeError(message)


def load_json(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as stream:
        return json.load(stream)


def parse_address(value: str) -> int:
    address = int(value, 0)
    if address == 0 or address & 3:
        fail(f"invalid aligned guest entry: {value}")
    return address


def validate_entry_set(name: str, entry_set: dict) -> set[int]:
    raw_entries = entry_set.get("entries", [])
    expected_count = entry_set.get("entry_count")
    if expected_count != len(raw_entries):
        fail(
            f"{name} declares {expected_count} entries but contains "
            f"{len(raw_entries)}"
        )
    addresses: list[int] = []
    for entry in raw_entries:
        value = entry if isinstance(entry, str) else entry["address"]
        addresses.append(parse_address(value))
    if len(set(addresses)) != len(addresses):
        fail(f"{name} contains duplicate guest entries")
    return set(addresses)


def validate_baseline(manifest: dict) -> None:
    commit = manifest["qualified_baseline"]["source_commit"]
    result = subprocess.run(
        ["git", "cat-file", "-e", f"{commit}^{{commit}}"],
        cwd=ROOT,
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        fail(f"qualified whole-AOT baseline commit is unavailable: {commit}")


def validate_hot_overlay(entry_set: dict, addresses: set[int]) -> None:
    source_root = ROOT / entry_set["implementation"]
    implementation_files = [
        path
        for pattern in ("oot3d_source_overlay_*.cpp", "oot3d_source_overlay_*.h")
        for path in source_root.glob(pattern)
        if "_tests." not in path.name
    ]
    source_text = "\n".join(
        path.read_text(encoding="utf-8", errors="ignore")
        for path in implementation_files
    ).lower()
    for address in addresses:
        token = f"0x{address:08x}"
        if token not in source_text:
            fail(f"hot overlay entry {token} is absent from its implementation")
    for entry in entry_set["entries"]:
        for evidence_key in ("source_contract", "verification"):
            evidence = entry.get(evidence_key)
            if evidence is not None and not (ROOT / evidence).is_file():
                fail(
                    f"hot overlay entry {entry['address']} has missing "
                    f"{evidence_key}: {evidence}"
                )
    if not (ROOT / entry_set["verification"]).is_file():
        fail("hot overlay verification source is missing")


def validate_source_profile(entry_set: dict, addresses: set[int]) -> None:
    if not (ROOT / entry_set["selection_contract"]).is_file() or not (
        ROOT / entry_set["verification"]
    ).is_file():
        fail("source gameplay profile contract or test is missing")

    semantic_manifest = load_json(
        ROOT / "tools/oot3d/source_integration/consumer_semantic_overlays.json"
    )
    reviewed = {
        parse_address(entry["entry"])
        for entry in semantic_manifest["entries"]
        if entry["status"] == "reviewed_semantic"
    }
    if parse_address("0x00417014") not in reviewed:
        fail("GameState_Update is no longer reviewed semantic source")

    for entry in entry_set["entries"]:
        package = entry.get("source_import")
        if package is None:
            continue
        import_manifest_path = (
            ROOT / "tools/oot3d/source_imports" / package / "import_manifest.json"
        )
        if not import_manifest_path.is_file():
            fail(f"source owner import is missing: {package}")
        imported = load_json(import_manifest_path)
        imported_entry = parse_address(imported["owner_entry"])
        declared_entry = parse_address(entry["address"])
        if imported_entry != declared_entry:
            fail(
                f"source import {package} entry mismatch: "
                f"0x{imported_entry:08X} != 0x{declared_entry:08X}"
            )
        if "native30_accepted" not in imported.get("runtime_activation", ""):
            fail(f"source import is not Native30 accepted: {package}")

    if len(addresses) != 8:
        fail("source gameplay profile no longer contains eight owners")


def validate_csab(entry_set: dict, addresses: set[int]) -> None:
    package = entry_set["source_import"]
    imported = load_json(
        ROOT / "tools/oot3d/source_imports" / package / "import_manifest.json"
    )
    imported_addresses = {
        parse_address(entry["entry"])
        for entry in imported["canonical_entries"]
    }
    if imported_addresses != addresses:
        fail("CSAB source leaf entries differ from their pinned import")
    if "native30_accepted" not in imported.get("runtime_activation", ""):
        fail("CSAB source leaves are no longer Native30 accepted")


def validate_typed_gameplay(entry_set: dict, addresses: set[int]) -> None:
    semantic = load_json(ROOT / entry_set["manifest"])
    reviewed = {
        parse_address(entry["entry"])
        for entry in semantic["entries"]
        if entry["status"] == "reviewed_semantic"
    }
    if reviewed != addresses:
        fail(
            "reviewed typed gameplay inventory changed: "
            f"missing={sorted(addresses - reviewed)}, "
            f"unexpected={sorted(reviewed - addresses)}"
        )


def validate_preserved_sets(manifest: dict, hot: set[int]) -> tuple[int, int]:
    pending = manifest["preserved_pending_sets"]
    boot = pending["source_native_boot_contracts"]
    overlays = load_json(ROOT / boot["manifest"])["overlays"]
    if len(overlays) != boot["entry_count"]:
        fail("source-native boot contract count changed without inventory update")
    addresses = [parse_address(entry["guest_address"]) for entry in overlays]
    if len(set(addresses)) != len(addresses):
        fail("source-native boot contracts contain duplicate guest entries")
    promoted = {parse_address(value) for value in boot["promoted_entries"]}
    if not promoted.issubset(set(addresses)):
        fail("promoted boot contracts are absent from the preserved manifest")
    if not promoted.issubset(hot):
        fail("promoted boot contracts are absent from the active hot overlay")
    pending_count = len(addresses) - len(promoted)
    if pending_count != boot["pending_entry_count"]:
        fail("source-native pending boot contract count is stale")
    startup_manifest = ROOT / pending["source_native_startup_closure"]["manifest"]
    if not startup_manifest.is_file():
        fail("preserved source-native startup import is missing")
    return len(addresses), pending_count


def validate_launcher(manifest: dict) -> None:
    defaults = manifest["migration_defaults"]
    if not defaults["whole_aot_fallback"]:
        fail("whole-AOT fallback must remain enabled during migration")
    if not defaults["source_overlay_dll"]:
        fail("the hot source-overlay DLL must remain enabled")
    unexpectedly_active = [
        name
        for name in (
            "source_gameplay_profile",
            "source_csab_curves",
            "typed_gameplay",
            "manual_cpp_services",
        )
        if defaults[name]
    ]
    if unexpectedly_active:
        fail(
            "qualified host cannot activate pending source sets: "
            + ", ".join(unexpectedly_active)
        )
    if defaults["mass_aot"] or defaults["true_aot_blocks"]:
        fail("mass/true AOT must not obscure hybrid owner accounting")
    if manifest["dispatch_precedence"][-1] != "whole_aot_fallback":
        fail("whole-AOT is not the final dispatch fallback")

    launcher = (
        ROOT / "scripts/oot3d/Invoke-Oot3dSourceOverlayGame.ps1"
    ).read_text(encoding="utf-8")
    required_tokens = (
        "DisableTypedGameplay",
        "DisableManualCompiledFunctions",
        "DisableTrueAotBlocks",
        "DisableMassAot",
    )
    for token in required_tokens:
        if token not in launcher:
            fail(f"hybrid launcher is missing migration control: {token}")
    unsupported_tokens = (
        "EnableSourceGameplayProfile",
        "EnableSourceCsabCurves",
    )
    for token in unsupported_tokens:
        if token in launcher:
            fail(
                "qualified hybrid executable cannot activate launcher control: "
                f"{token}"
            )


def main() -> int:
    manifest = load_json(MANIFEST_PATH)
    if manifest.get("format") != "oot3d_hybrid_source_migration_v1":
        fail("unsupported hybrid migration manifest format")

    validate_baseline(manifest)
    promoted = manifest["promoted_sets"]
    pending = manifest["accepted_pending_sets"]
    if promoted["hot_source_overlay_dll"].get("status") != "active":
        fail("hot source-overlay inventory is not marked active")
    incorrectly_active = [
        name
        for name, entry_set in pending.items()
        if entry_set.get("status") == "active"
    ]
    if incorrectly_active:
        fail(
            "accepted source sets are incorrectly marked active: "
            + ", ".join(incorrectly_active)
        )
    hot = validate_entry_set(
        "hot_source_overlay_dll", promoted["hot_source_overlay_dll"]
    )
    profile = validate_entry_set(
        "validated_source_gameplay_profile",
        pending["validated_source_gameplay_profile"],
    )
    csab = validate_entry_set(
        "validated_csab_source_leaves",
        pending["validated_csab_source_leaves"],
    )
    typed = validate_entry_set(
        "reviewed_typed_gameplay", pending["reviewed_typed_gameplay"]
    )
    validate_hot_overlay(promoted["hot_source_overlay_dll"], hot)
    validate_source_profile(pending["validated_source_gameplay_profile"], profile)
    validate_csab(pending["validated_csab_source_leaves"], csab)
    validate_typed_gameplay(pending["reviewed_typed_gameplay"], typed)
    preserved_boot, pending_boot = validate_preserved_sets(manifest, hot)
    validate_launcher(manifest)

    print(
        json.dumps(
            {
                "format": "oot3d_hybrid_source_migration_validation_v1",
                "active_hot_overlay_entries": len(hot),
                "accepted_source_gameplay_owners": len(profile),
                "source_gameplay_owners_pending_hot_composition": len(
                    profile - hot
                ),
                "accepted_csab_source_leaves": len(csab),
                "csab_source_leaves_pending_hot_composition": len(csab - hot),
                "reviewed_typed_entries": len(typed),
                "reviewed_typed_entries_pending_hot_composition": len(
                    typed - hot
                ),
                "preserved_boot_contracts": preserved_boot,
                "preserved_pending_boot_contracts": pending_boot,
                "whole_aot_fallback": True,
            },
            indent=2,
        )
    )
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (KeyError, OSError, ValueError, RuntimeError) as error:
        print(f"hybrid migration manifest validation failed: {error}", file=sys.stderr)
        sys.exit(1)
