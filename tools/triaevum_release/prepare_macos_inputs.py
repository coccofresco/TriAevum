"""Extract hash-pinned public release inputs for the local macOS build.

No ROM or Windows executable is used as game input. The alpha 1c archive is
needed only for the verified USA normalization recipe absent from alpha 2.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import subprocess
import zipfile
from pathlib import Path, PurePosixPath

RELEASES = {
    "2": "06ac2c799575ade3b4eccdd1046ae6e9bf5f18eb8f40f7ce53829c7f11bde01c",
    "1c": "400bac880b0f10ef982417044e0a2a0581bd40bf9fe766ef0e16008a9e681eb7",
}


def download(build: Path, version: str) -> Path:
    path = build / f"upstream-alpha{version}-windows.zip"
    if not path.exists():
        temporary = path.with_suffix(".downloading")
        url = ("https://github.com/coccofresco/TriAevum/releases/download/"
               f"v0.6.0-alpha.{version}/TriAevum-v0.6.0-alpha.{version}-Windows-x64.zip")
        subprocess.run(["curl", "--fail", "--location", "--retry", "3", url,
                        "--output", str(temporary)], check=True)
        if hashlib.sha256(temporary.read_bytes()).hexdigest() != RELEASES[version]:
            raise ValueError(f"Release archive checksum mismatch: {version}")
        temporary.replace(path)
    if hashlib.sha256(path.read_bytes()).hexdigest() != RELEASES[version]:
        raise ValueError(f"Release archive checksum mismatch: {path}")
    return path


def members(archive: Path):
    with zipfile.ZipFile(archive) as source:
        for name in source.namelist():
            relative = name.removeprefix("Windows/")
            parts = PurePosixPath(relative)
            if parts.is_absolute() or ".." in parts.parts:
                raise ValueError("Invalid archive path")
            if not name.endswith("/"):
                yield relative, source.read(name)


def prepare(build: Path) -> None:
    build = build.resolve()
    build.mkdir(parents=True, exist_ok=True)
    installation = build / "installation"
    translated = build / "translated-title-alpha2"
    for name, payload in members(download(build, "2")):
        if name.startswith(("recipes/", "resources/", "forge/shader-corpus/")):
            # The Windows catalog does not describe a Mac title or helper.
            if name == "recipes/precompiled-titles.json":
                continue
            destination = installation / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(payload)
        elif name.endswith("-translated.zip") and name.startswith("source/titles/"):
            archive = build / "alpha2-translated-title.zip"
            archive.write_bytes(payload)
            for source_name, source_data in members(archive):
                # The title CMake project verifies the source manifest hashes.
                if "/" in source_name or not source_name.endswith((".cpp", ".h", ".json", ".md")):
                    raise ValueError("Unexpected translated title entry")
                translated.mkdir(exist_ok=True)
                (translated / source_name).write_bytes(source_data)
    recipes_path = installation / "recipes/oot3d.json"
    recipes = json.loads(recipes_path.read_text())
    for name, payload in members(download(build, "1c")):
        if name == "recipes/oot3d.json":
            previous = json.loads(payload)
            recipes["recipes"] += [r for r in previous["recipes"] if r.get("input_adapter")]
        elif name.startswith("recipes/adapters/"):
            destination = installation / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(payload)
    recipes_path.write_text(json.dumps(recipes, indent=2) + "\n")
    root = Path(__file__).resolve().parents[2]
    shutil.copytree(root / "runtime/three_ds_recomp/src/fast/shaders",
                    installation / "resources/shaders", dirs_exist_ok=True)
    print(f"Prepared alpha 2 title, shaders and recipes in {build}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", required=True, type=Path)
    prepare(parser.parse_args().build)
