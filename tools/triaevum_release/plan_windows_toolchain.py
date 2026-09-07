"""Derive a pinned minimal clang dependency acquisition plan from a VS catalog.

No SDK payload is installed or redistributed by this planning operation.
"""

import argparse
from pathlib import Path, PureWindowsPath

try:
    from .common import atomic_write_json, load_json_object, sha256_file
    from .toolchain_download import validate_payload
    from .msi_metadata import read_cabinets, resolve_cabinet_payloads
except ImportError:
    from common import atomic_write_json, load_json_object, sha256_file
    from toolchain_download import validate_payload
    from msi_metadata import read_cabinets, resolve_cabinet_payloads


def create_plan(catalog: Path, expected_sha256: str, msvc_package_version: str,
                sdk_build: str) -> dict:
    if sha256_file(catalog) != expected_sha256.lower():
        raise ValueError("VS catalog does not match the pinned SHA-256")
    packages = load_json_object(catalog).get("packages")
    if not isinstance(packages, list):
        raise ValueError("Invalid VS package catalog")
    def package(identifier):
        matches = [entry for entry in packages if entry.get("id", "").casefold() == identifier.casefold()]
        if len(matches) != 1:
            raise ValueError(f"Missing or ambiguous Microsoft package: {identifier}")
        return matches[0]

    selected = []
    for suffix in ("CRT.Headers.base", "CRT.x64.Desktop.base"):
        entry = package(f"Microsoft.VC.{msvc_package_version}.{suffix}")
        for payload in entry["payloads"]:
            validate_payload(payload)
            selected.append({**payload, "package": entry["id"], "version": entry["version"], "kind": "vsix"})
    sdk = package(f"Win11SDK_10.0.{sdk_build}")
    cabinets = {}
    for payload in sdk["payloads"]:
        name = PureWindowsPath(payload["fileName"]).name
        if name.lower().endswith(".cab"):
            validate_payload(payload)
            if name.casefold() in cabinets:
                raise ValueError("Ambiguous SDK cabinet name")
            cabinets[name.casefold()] = payload
        elif name.endswith(".msi") and (
            ("Headers" in name and " arm64-" not in name) or name in ("Windows SDK Desktop Libs x64-x86_en-us.msi",
                                           "Windows SDK for Windows Store Apps Libs-x86_en-us.msi")
        ):
            validate_payload(payload)
            selected.append({**payload, "package": sdk["id"], "version": sdk["version"], "kind": "msi"})
    names = {PureWindowsPath(entry["fileName"]).name for entry in selected}
    required = {"Universal CRT Headers Libraries and Sources-x86_en-us.msi",
                "Windows SDK for Windows Store Apps Libs-x86_en-us.msi",
                "Windows SDK Desktop Libs x64-x86_en-us.msi",
                "Windows SDK for Windows Store Apps Headers-x86_en-us.msi"}
    if not required.issubset(names) or not cabinets:
        raise ValueError("SDK catalog is missing required headers/libraries/cabinets")
    return {"format": "triaevum_windows_toolchain_acquisition_v1",
            "catalog_sha256": expected_sha256.lower(), "msvc_package_version": msvc_package_version,
            "sdk_build": sdk_build, "payloads": selected, "sdk_cabinets": cabinets,
            "license_acceptance_required": True,
            "stage": "msi_cabinet_resolution_required",
            "license_information": "https://visualstudio.microsoft.com/license-terms/",
            "scope": "private_acquisition_not_public_redistribution"}


def resolve_plan_cabinets(plan: dict, cache: Path) -> dict:
    required = {}
    for payload in plan["payloads"]:
        if payload["kind"] != "msi":
            continue
        digest, size, _ = validate_payload(payload)
        local = cache / digest
        if local.is_symlink() or not local.is_file() or local.stat().st_size > size or sha256_file(local) != digest:
            raise ValueError("MSI must be present and verified before cabinet resolution: " + payload["fileName"])
        for cabinet in resolve_cabinet_payloads(read_cabinets(local), plan["sdk_cabinets"]):
            required[cabinet["sha256"]] = cabinet
    return {**plan, "required_cabinets": list(required.values()),
            "stage": "payload_acquisition_required"}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--catalog", type=Path, required=True)
    parser.add_argument("--catalog-sha256", required=True)
    parser.add_argument("--msvc-package-version", required=True)
    parser.add_argument("--sdk-build", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--resolve-cache", type=Path,
                        help="Resolve Media dependencies from verified MSI files already in this cache")
    args = parser.parse_args()
    plan = create_plan(args.catalog, args.catalog_sha256, args.msvc_package_version, args.sdk_build)
    if args.resolve_cache:
        plan = resolve_plan_cabinets(plan, args.resolve_cache)
    atomic_write_json(args.output, plan)
    print({"payloads": len(plan["payloads"]), "initial_bytes": sum(p["size"] for p in plan["payloads"]),
           "cabinet_catalog_entries": len(plan["sdk_cabinets"]), "plan": str(args.output)})
