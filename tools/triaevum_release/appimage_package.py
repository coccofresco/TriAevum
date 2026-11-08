"""Publisher-only AppDir staging from the audited Linux release inventory.

The existing importer and frozen Forge own activation. No ROM, user state,
host drivers or build tools are added to the payload by this wrapper.
"""

import argparse
import json
from pathlib import Path
import shutil

from .audit_release import DEFAULT_POLICY, audit_release
from .common import load_json_object, release_manifest_path
from .release_platform import LINUX
from .linux_elf_audit import audit_native_elfs


ASSETS = Path(__file__).resolve().parent / "appimage"
APP_ID = "io.github.coccofresco.TriAevum"
# The desktop entry's icons (branding/README.md): the game icon, and the
# Forge icon of the "Prepare or repair game" action.
ICONS = Path(__file__).resolve().parent / "branding/linux/hicolor"
APPDIR_ICON_SIZE = "256x256"
REPOSITORY = Path(__file__).resolve().parents[2]
# Per title (owner decisions 2026-11-08): the desktop entry and the hicolor
# icons of the title's product, under its application ID. OoT3D's are the
# assets above; the MM3D facts live in titles/mm3d (generate_branding.py
# writes the icons, forge-catalog title.rs MM3D.package_identity is the ID).
MM3D_APP_ID = "io.github.coccofresco.TriAevum.MM3D"
TITLES = {
    "oot3d": {"app_id": APP_ID, "desktop": ASSETS / f"{APP_ID}.desktop", "icons": ICONS},
    "mm3d": {"app_id": MM3D_APP_ID,
             "desktop": REPOSITORY / f"titles/mm3d/linux/appimage/{MM3D_APP_ID}.desktop",
             "icons": REPOSITORY / "titles/mm3d/branding/linux/hicolor"},
}
EXECUTABLE_ROLES = {"runtime_executable", "forge_executable", "forge_command_line", "forge_tool",
                    "shader_preparation_tool"}


def product_name(title: str) -> str:
    facts = json.loads((REPOSITORY / "titles" / title / "product.json").read_text(encoding="utf-8"))
    if facts.get("title") != title or not isinstance(facts.get("product_name"), str):
        raise ValueError(f"Invalid product facts for {title}")
    return facts["product_name"]


def desktop_entry(title: str) -> str:
    """The title's desktop entry, checked against its product facts: it names
    the product, uses the title's icons and starts the bundled Forge (AppRun),
    which plays the installed game like OoT3D's entry."""
    facts = TITLES[title]
    text = facts["desktop"].read_text(encoding="utf-8")
    lines = text.splitlines()
    name = next((line[5:] for line in lines if line.startswith("Name=")), "")
    if product_name(title) not in name:
        raise ValueError(f"The {title} desktop entry does not name its product: {name!r}")
    icons = {line[5:] for line in lines if line.startswith("Icon=")}
    if icons != {facts["app_id"], facts["app_id"] + ".Forge"}:
        raise ValueError(f"The {title} desktop entry uses other icons: {sorted(icons)}")
    if any(line.startswith("Exec=") and not line.startswith("Exec=AppRun") for line in lines):
        raise ValueError(f"The {title} desktop entry must start AppRun")
    return text


def install_icons(appdir: Path, title: str = "oot3d") -> None:
    """The hicolor icons under usr/share, and the AppDir's own icon and .DirIcon."""
    icons, app_id = TITLES[title]["icons"], TITLES[title]["app_id"]
    for icon in sorted(icons.glob("*/apps/*.png")):
        destination = appdir / "usr/share/icons/hicolor" / icon.relative_to(icons)
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(icon, destination)
    root = icons / APPDIR_ICON_SIZE / "apps" / f"{app_id}.png"
    shutil.copyfile(root, appdir / f"{app_id}.png")
    shutil.copyfile(root, appdir / ".DirIcon")


def stage_appimage(package: Path, output: Path, *, policy_path: Path = DEFAULT_POLICY,
                   title: str = "oot3d") -> Path:
    if title not in TITLES:
        raise ValueError(f"Unknown title: {title}")
    desktop_text = desktop_entry(title)
    package = package.resolve()
    output = output.absolute()
    if output.exists() or output.is_symlink() or output.resolve().is_relative_to(package):
        raise ValueError("AppDir requires a new directory outside the package")
    result = audit_release(package, policy_path=policy_path)
    if not result.ok:
        raise ValueError("AppImage package audit failed: " + "; ".join(result.errors))
    manifest = load_json_object(release_manifest_path(package))
    if manifest["release"].get("target") != LINUX.target:
        raise ValueError("AppImage requires an audited Linux x86-64 package")
    audit_native_elfs(package, frozen_forge=result.summary["forge_profile"] == "python_pyinstaller")
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
        # Script assets are platform-independent, including checkout line endings.
        (output / "AppRun").write_text((ASSETS / "AppRun").read_text(encoding="utf-8"), encoding="utf-8", newline="\n")
        (output / f"{TITLES[title]['app_id']}.desktop").write_text(desktop_text, encoding="utf-8", newline="\n")
        (output / "AppRun").chmod(0o755)
        install_icons(output, title)
    except BaseException:
        shutil.rmtree(output)
        raise
    return output


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--title", choices=tuple(TITLES), default="oot3d")
    args = parser.parse_args()
    try:
        print(stage_appimage(args.package, args.output, title=args.title))
    except (OSError, ValueError) as exc:
        parser.exit(1, f"TriAevum AppImage staging failed: {exc}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
