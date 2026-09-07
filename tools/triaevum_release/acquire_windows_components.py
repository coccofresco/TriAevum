"""Acquire and extract planned private dependencies without running installers."""

import argparse
import tempfile
from pathlib import Path, PureWindowsPath

try:
    from .common import atomic_write_json, load_json_object
    from .toolchain_download import acquire_payload, validate_payload
    from .plan_windows_toolchain import resolve_plan_cabinets
    from .msi_extract import extract_msi_package
    from .vsix_extract import extract_vsix
except ImportError:
    from common import atomic_write_json, load_json_object
    from toolchain_download import acquire_payload, validate_payload
    from plan_windows_toolchain import resolve_plan_cabinets
    from msi_extract import extract_msi_package
    from vsix_extract import extract_vsix


def acquire_components(plan: dict, cache: Path, output: Path, *, accept_licenses: bool,
                       progress=None) -> dict:
    if not accept_licenses:
        raise ValueError("Applicable Microsoft licenses must be explicitly accepted")
    if (plan.get("format") != "triaevum_windows_toolchain_acquisition_v1"
            or plan.get("scope") != "private_acquisition_not_public_redistribution"
            or not plan.get("license_acceptance_required")):
        raise ValueError("Unsupported private acquisition plan")
    payloads = plan.get("payloads")
    if not isinstance(payloads, list) or not payloads:
        raise ValueError("Empty acquisition plan")
    for payload in payloads:
        validate_payload(payload)
        if payload.get("kind") not in ("msi", "vsix"):
            raise ValueError("Unsupported acquisition payload kind")
    output = output.resolve()
    if output.exists():
        raise ValueError("Component acquisition requires a new output directory")
    def report(stage, name):
        if progress is not None:
            progress(stage, name)
    for payload in payloads:
        report("download", payload["fileName"])
        acquire_payload(payload, cache)
    # Never trust a previously supplied list of resolved CAB dependencies.
    resolved = resolve_plan_cabinets(plan, cache)
    cabinets = {}
    for payload in resolved["required_cabinets"]:
        report("download", payload["fileName"])
        path = acquire_payload(payload, cache)
        name = PureWindowsPath(payload["fileName"]).name
        if name.casefold() in {key.casefold() for key in cabinets}:
            raise ValueError("Ambiguous acquired cabinet name")
        cabinets[name] = (path, validate_payload(payload)[0])
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".sdk-components-", dir=output.parent) as temporary:
        staging = Path(temporary).resolve()
        if not staging.is_relative_to(output.parent):
            raise ValueError("Component staging escaped its parent")
        tree = staging / "tree"
        tree.mkdir()
        components = []
        for index, payload in enumerate(payloads):
            report("extract", payload["fileName"])
            digest = validate_payload(payload)[0]
            relative = f"components/{index:03d}"
            destination = tree / relative
            if payload["kind"] == "msi":
                receipt = extract_msi_package(cache / digest, digest, cabinets, destination)
            else:
                receipt = extract_vsix(cache / digest, digest, destination)
            components.append({"payload": payload, "path": relative, "extraction": receipt})
        result = {"format": "triaevum_windows_components_v1", "plan": resolved,
                  "license_acceptance": "explicit_caller_confirmation",
                  "status": "extracted_not_activated", "components": components}
        atomic_write_json(tree / "components.json", result)
        tree.replace(output)
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plan", type=Path, required=True)
    parser.add_argument("--cache", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--accept-microsoft-licenses", action="store_true")
    args = parser.parse_args()
    result = acquire_components(load_json_object(args.plan), args.cache, args.output,
                                accept_licenses=args.accept_microsoft_licenses,
                                progress=lambda stage, name: print(stage, name, flush=True))
    print({"status": result["status"], "components": len(result["components"])})
