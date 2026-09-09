"""Import the pinned, renderer-independent Azahar Android input overlay.

Reads Git objects, not the donor worktree. It never modifies the donor. Runtime
builds consume the checked-in result and do not need Azahar or this importer.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
DEST = ROOT / "ports/android/controls"
REVISION = "beb5681ee7f85586501b16b083a961b707092cd7"
BASE = "src/android/app/src/main/"
JAVA = BASE + "java/org/citra/citra_emu/"
PACKAGE = "org.triaevum.android.controls"
OUT_JAVA = "src/main/java/org/triaevum/android/controls/"
OVERLAY_FILES = (
    "InputOverlay.kt", "InputOverlayDrawableButton.kt",
    "InputOverlayDrawableDpad.kt", "InputOverlayDrawableJoystick.kt",
)


def adapt_kotlin(source: str) -> str:
    # Only ownership/import changes. Drawing, hit testing, preference keys,
    # pointer tracking, dimensions and Android feedback stay donor-owned.
    source = "// TriAevum adaptation, 2026-09-09: namespace and host boundary; see donor_manifest.json.\n" + source
    source = source.replace("package org.citra.citra_emu.overlay", f"package {PACKAGE}")
    source = source.replace("package org.citra.citra_emu.utils", f"package {PACKAGE}")
    for line in source.splitlines(keepends=True):
        if line.startswith("import org.citra.citra_emu."):
            source = source.replace(line, "")
    source = source.replace("NativeLibrary.ButtonType", "AzaharButtonIds")
    source = source.replace("NativeLibrary.ButtonState", "AzaharButtonState")
    source = source.replace("NativeLibrary.", "OverlayHost.")
    source = source.replace("CitraApplication.appContext", "OverlayHost.appContext")
    source = source.replace("TurboHelper.toggleTurbo", "OverlayHost.toggleTurbo")
    source = source.replace(
        "    private val settingsViewModel = OverlayHost.sEmulationActivity.get()!!.settingsViewModel\n", "")
    return source


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("donor", type=Path)
    parser.add_argument("--git", default="git")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    entries: dict[str, dict[str, str]] = {}
    outputs: dict[str, bytes] = {}

    def git(*command: str) -> bytes:
        return subprocess.check_output([args.git, "-C", str(args.donor), *command])

    def read(path: str) -> bytes:
        data = git("show", f"{REVISION}:{path}")
        entries[path] = {"sha256": hashlib.sha256(data).hexdigest()}
        return data

    def emit(path: str, data: str | bytes) -> None:
        outputs[path] = data.encode("utf-8") if isinstance(data, str) else data

    all_kotlin = ""
    for name in OVERLAY_FILES:
        source = read(JAVA + "overlay/" + name).decode("utf-8")
        all_kotlin += source
        emit(OUT_JAVA + name, adapt_kotlin(source))
    settings = read(JAVA + "utils/EmulationMenuSettings.kt").decode("utf-8")
    settings = adapt_kotlin(settings)
    settings = settings.replace("import androidx.drawerlayout.widget.DrawerLayout\n", "")
    # Drawer locking belongs to Azahar's emulation activity, not the overlay.
    settings = settings[:settings.index("    var drawerLockMode:")] + "}\n"
    emit(OUT_JAVA + "EmulationMenuSettings.kt", settings)

    native = read(JAVA + "NativeLibrary.kt").decode("utf-8")
    # Evidence for the hand-maintained axis normalization in AzaharInputAdapter.
    read(BASE + "jni/native.cpp")
    notice = native[:native.index("package ")]
    constants = []
    for old, new in (("ButtonType", "AzaharButtonIds"), ("ButtonState", "AzaharButtonState")):
        match = re.search(r"    object " + old + r" \{[^}]+\}", native)
        if not match:
            raise RuntimeError(f"Missing donor constants: {old}")
        constants.append("\n".join(line[4:] for line in match[0].splitlines()).replace(old, new, 1))
    emit(OUT_JAVA + "AzaharButtonIds.kt", "// Extracted for TriAevum, 2026-09-09; values unchanged.\n" + notice + f"package {PACKAGE}\n\n" + "\n\n".join(constants) + "\n")

    paths = git("ls-tree", "-r", "--name-only", REVISION, "--", BASE + "res").decode().splitlines()
    drawables = set(re.findall(r"R\.drawable\.(\w+)", all_kotlin))
    copied = set()
    for path in paths:
        relative = path.removeprefix(BASE)
        if relative.startswith("res/drawable") and Path(path).stem in drawables:
            data = read(path)
            if path.endswith(".xml") and re.search(rb"@(?!android:)[a-z]+/", data):
                raise RuntimeError(f"Resolve transitive resource before import: {path}")
            emit("src/main/" + relative, data)
            copied.add(Path(path).stem)
    if drawables != copied:
        raise RuntimeError(f"Missing drawables: {sorted(drawables - copied)}")
    integer_names = set(re.findall(r"R\.integer\.(\w+)", all_kotlin))
    integers = ET.fromstring(read(BASE + "res/values/integers.xml"))
    values = ET.Element("resources")
    for element in integers:
        if element.get("name") in integer_names:
            values.append(element)
    if {value.get("name") for value in values} != integer_names:
        raise RuntimeError("Incomplete overlay layout defaults")
    ET.indent(values, space="    ")
    emit("src/main/res/values/azahar_overlay_integers.xml", ET.tostring(values, encoding="utf-8", xml_declaration=True) + b"\n")
    emit("license.txt", read("license.txt"))
    manifest = {
        "schema": 1, "donor": "https://github.com/azahar-emu/azahar",
        "snapshot_commit": REVISION,
        "overlay_last_change": "3716f6b9b63ee5271fe5a1c4327972e02158834d",
        "license": "GPL-2.0-or-later",
        "adaptations": ["namespace", "host_callbacks_and_application_context",
                        "remove_unused_activity_viewmodel", "exclude_activity_drawer_setting",
                        "extract_overlay_only_resources_and_button_constants"],
        "sources": entries,
        "outputs": {name: hashlib.sha256(data).hexdigest() for name, data in sorted(outputs.items())},
    }
    emit("donor_manifest.json", json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    for name, data in outputs.items():
        target = DEST / name
        if args.check:
            if not target.is_file() or target.read_bytes() != data:
                raise RuntimeError(f"Donor import drift: {name}")
        else:
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
    print(f"{'Verified' if args.check else 'Imported'} {len(outputs)} overlay files; donor read-only")


if __name__ == "__main__":
    main()
