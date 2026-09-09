"""Stage Flatpak build inputs from an already audited Linux release inventory."""

import argparse
import json
from pathlib import Path
import shutil

try:
    from .audit_release import DEFAULT_POLICY, audit_release
    from .common import load_json_object, atomic_write_json
    from .release_platform import LINUX
    from .host_layout import FLATPAK_ID
except ImportError:
    from audit_release import DEFAULT_POLICY, audit_release
    from common import load_json_object, atomic_write_json
    from release_platform import LINUX
    from host_layout import FLATPAK_ID


ASSETS = Path(__file__).resolve().parent / "flatpak"


def stage_flatpak(package: Path, output: Path, *, policy_path: Path = DEFAULT_POLICY) -> Path:
    package = package.resolve()
    output = output.absolute()
    if output.exists() or output.is_symlink() or output.resolve().is_relative_to(package):
        raise ValueError("Flatpak build inputs require a new directory outside the package")
    audit = audit_release(package, policy_path=policy_path)
    if not audit.ok:
        raise ValueError("Flatpak package audit failed:\n" + "\n".join(audit.errors))
    manifest = load_json_object(package / "release-manifest.json")
    if manifest["release"].get("target") != LINUX.target:
        raise ValueError("Flatpak requires an audited Linux x86-64 package")
    output.mkdir(parents=True)
    try:
        for entry in [*manifest["files"], {"path": "release-manifest.json", "role": "manifest"}]:
            relative = entry["path"]
            destination = output / "package" / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(package / relative, destination)
            destination.chmod(0o755 if entry["role"] in ("runtime_executable", "forge_executable") else 0o644)
        # Recheck after copying; never silently publish changed or extra inputs.
        copied_audit = audit_release(output / "package", policy_path=policy_path)
        if not copied_audit.ok:
            raise ValueError("Copied Flatpak inventory failed audit: " + "; ".join(copied_audit.errors))
        for name in (f"{FLATPAK_ID}.json", f"{FLATPAK_ID}.desktop", "file_chooser.c", "triaevum"):
            shutil.copyfile(ASSETS / name, output / name)
        atomic_write_json(output / "flatpak-staging.json", {
            "format": "triaevum_flatpak_staging_v1",
            "package_qualification": manifest["release"].get("qualification", "unspecified"),
            "flatpak_qualified": False,
            "source_manifest": "package/release-manifest.json",
        })
    except BaseException:
        shutil.rmtree(output)
        raise
    return output / f"{FLATPAK_ID}.json"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        print(json.dumps({"manifest": str(stage_flatpak(args.package, args.output))}))
    except (OSError, ValueError) as exc:
        parser.exit(1, f"TriAevum Flatpak staging failed: {exc}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
