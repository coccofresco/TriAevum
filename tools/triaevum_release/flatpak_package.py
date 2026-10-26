"""Stage Flatpak build inputs from an already audited Linux release inventory."""

import argparse
import json
from pathlib import Path
import shutil

try:
    from .audit_release import DEFAULT_POLICY, audit_release
    from .common import load_json_object, atomic_write_json, release_manifest_path
    from .release_platform import LINUX
    from .host_layout import FLATPAK_ID
except ImportError:
    from audit_release import DEFAULT_POLICY, audit_release
    from common import load_json_object, atomic_write_json, release_manifest_path
    from release_platform import LINUX
    from host_layout import FLATPAK_ID


ASSETS = Path(__file__).resolve().parent / "flatpak"
EXECUTABLE_ROLES = ("runtime_executable", "forge_executable", "forge_command_line", "forge_tool",
                    "shader_preparation_tool")
# The PyInstaller Forge's Tk window reaches the file chooser portal through
# this helper; the Rust Forge's window talks to the portal itself.
FILE_CHOOSER = "file_chooser.c"


def build_manifest(forge_profile: str) -> str:
    template = (ASSETS / f"{FLATPAK_ID}.json").read_text(encoding="utf-8")
    if forge_profile != "rust":
        return template
    manifest = json.loads(template)
    module = manifest["modules"][0]
    commands = [command for command in module["build-commands"] if "file_chooser" not in command
                and "file-chooser" not in command]
    module["build-commands"] = [
        command + " /app/lib/triaevum/forge/triaevum-forge" if command.startswith("chmod 755 ") else command
        for command in commands]
    module["sources"] = [source for source in module["sources"] if source.get("path") != FILE_CHOOSER]
    return json.dumps(manifest, indent=2) + "\n"


def stage_flatpak(package: Path, output: Path, *, policy_path: Path = DEFAULT_POLICY) -> Path:
    package = package.resolve()
    output = output.absolute()
    if output.exists() or output.is_symlink() or output.resolve().is_relative_to(package):
        raise ValueError("Flatpak build inputs require a new directory outside the package")
    audit = audit_release(package, policy_path=policy_path)
    if not audit.ok:
        raise ValueError("Flatpak package audit failed:\n" + "\n".join(audit.errors))
    manifest = load_json_object(release_manifest_path(package))
    if manifest["release"].get("target") != LINUX.target:
        raise ValueError("Flatpak requires an audited Linux x86-64 package")
    output.mkdir(parents=True)
    try:
        for entry in [*manifest["files"], {"path": "release-manifest.json", "role": "manifest"}]:
            relative = entry["path"]
            destination = output / "package" / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(package / relative, destination)
            destination.chmod(0o755 if entry["role"] in EXECUTABLE_ROLES else 0o644)
        # Recheck after copying; never silently publish changed or extra inputs.
        copied_audit = audit_release(output / "package", policy_path=policy_path)
        if not copied_audit.ok:
            raise ValueError("Copied Flatpak inventory failed audit: " + "; ".join(copied_audit.errors))
        forge_profile = copied_audit.summary["forge_profile"]
        (output / f"{FLATPAK_ID}.json").write_text(build_manifest(forge_profile), encoding="utf-8", newline="\n")
        for name in (f"{FLATPAK_ID}.desktop", "triaevum",
                     *((FILE_CHOOSER,) if forge_profile == "python_pyinstaller" else ())):
            shutil.copyfile(ASSETS / name, output / name)
        atomic_write_json(output / "flatpak-staging.json", {
            "format": "triaevum_flatpak_staging_v1",
            "package_qualification": manifest["release"].get("qualification", "unspecified"),
            "forge_profile": forge_profile,
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
