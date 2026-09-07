"""Public setup metadata allowlist; never includes the downloaded SDK payloads."""

from pathlib import Path

try:
    from .common import load_json_object, normalize_relative_path
    from .toolchain_setup import load_setup
    from .toolchain_download import validate_payload
except ImportError:
    from common import load_json_object, normalize_relative_path
    from toolchain_setup import load_setup
    from toolchain_download import validate_payload


def setup_layout(descriptor: Path) -> list[dict]:
    setup = load_setup(descriptor)
    if setup is None:
        raise ValueError("Requested toolchain setup descriptor is missing")
    config = load_json_object(descriptor)
    if config["clang_resource"] != "clang" or config["clang_version"] != "22":
        raise ValueError("Toolchain setup must use the bundled Clang 22 resource layout")
    plan = setup.plan
    if plan.get("format") != "triaevum_windows_toolchain_acquisition_v1":
        raise ValueError("Unsupported public toolchain acquisition plan")
    if "installed_metadata" in plan:
        raise ValueError("Public setup cannot include private installed-metadata paths")
    if plan.get("scope") != "private_acquisition_not_public_redistribution" or not plan.get("license_acceptance_required"):
        raise ValueError("Toolchain setup must require private acquisition and license acceptance")
    payloads = plan.get("payloads", [])
    if not payloads or not plan.get("sdk_cabinets"):
        raise ValueError("Incomplete public acquisition plan")
    for payload in [*payloads, *plan["sdk_cabinets"].values()]:
        validate_payload(payload)
    names = ["toolchain-setup.json", normalize_relative_path(config["plan"]),
             normalize_relative_path(config["license"])]
    if len({name.casefold() for name in names}) != len(names):
        raise ValueError("Overlapping public setup destinations")
    if names[1] != "toolchain-plan.json" or names[2] != "toolchain-license.txt":
        raise ValueError("Public setup must use the canonical plan and license filenames")
    return [{"source": str(source.resolve()), "path": "forge/" + name,
             "role": "forge_toolchain_setup"} for source, name in zip(
                 [descriptor, descriptor.parent / names[1], descriptor.parent / names[2]], names)]
