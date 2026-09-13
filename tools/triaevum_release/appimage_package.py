"""Publisher-only AppDir staging from the audited Linux release inventory.

The existing importer and frozen Forge own activation. No ROM, user state,
host drivers or build tools are added to the payload by this wrapper.
"""

import argparse
from pathlib import Path
import shutil

from .audit_release import DEFAULT_POLICY, audit_release
from .common import load_json_object
from .release_platform import LINUX


ASSETS = Path(__file__).resolve().parent / "appimage"
APP_ID = "io.github.coccofresco.TriAevum"
EXECUTABLE_ROLES = {"runtime_executable", "forge_executable", "forge_tool", "shader_preparation_tool"}


def stage_appimage(package: Path, output: Path, *, policy_path: Path = DEFAULT_POLICY) -> Path:
    package = package.resolve()
    output = output.absolute()
    if output.exists() or output.is_symlink() or output.resolve().is_relative_to(package):
        raise ValueError("AppDir requires a new directory outside the package")
    result = audit_release(package, policy_path=policy_path)
    if not result.ok:
        raise ValueError("AppImage package audit failed: " + "; ".join(result.errors))
    manifest = load_json_object(package / "release-manifest.json")
    if manifest["release"].get("target") != LINUX.target:
        raise ValueError("AppImage requires an audited Linux x86-64 package")
    output.mkdir(parents=True)
    try:
        payload = output / "usr/lib/triaevum"
        for entry in [*manifest["files"], {"path": "release-manifest.json", "role": "manifest"}]:
            destination = payload / entry["path"]
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(package / entry["path"], destination)
            destination.chmod(0o755 if entry["role"] in EXECUTABLE_ROLES else 0o644)
        copied = audit_release(payload, policy_path=policy_path)
        if not copied.ok:
            raise ValueError("Copied AppImage inventory failed audit: " + "; ".join(copied.errors))
        for name in ("AppRun", f"{APP_ID}.desktop", f"{APP_ID}.svg"):
            # Script assets are platform-independent, including checkout line endings.
            (output / name).write_text((ASSETS / name).read_text(encoding="utf-8"), encoding="utf-8", newline="\n")
        (output / "AppRun").chmod(0o755)
    except BaseException:
        shutil.rmtree(output)
        raise
    return output


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        print(stage_appimage(args.package, args.output))
    except (OSError, ValueError) as exc:
        parser.exit(1, f"TriAevum AppImage staging failed: {exc}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
